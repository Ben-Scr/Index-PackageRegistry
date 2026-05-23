using System;

namespace Index.Tilemap2D;

// Tilemap2D — managed handle.
//
// This is a thin wrapper over the native `Index::Tilemap2D::Tilemap2DComponent`
// living in Pkg.Index.Tilemap2D.Native. Storage and every read/write goes
// through the native side via DllImport — there is NO parallel managed
// chunk store. The class subclasses `Index.Component`, so the standard
// `entity.GetComponent<Tilemap2D>()` lookup works once the
// component-name registration is in place on the C# side.
//
// API parity with the native side: SetTile / SetTiles (span) / GetTile /
// ClearTile, plus a Texture-asset-style accessor for the active tileset.
public class Tilemap2D : Component
{
    public Texture? Tileset
    {
        get => Texture.FromAssetUUID(NativeBindings.GetTilesetTexture(Entity.ID));
        set => NativeBindings.SetTilesetTexture(Entity.ID, value?.UUID ?? 0);
    }

    // Tilemap-wide color tint. The renderer multiplies this with each
    // tile's per-cell TileColor, so it acts as a global modulation
    // (alpha-fade the whole tilemap, color-shift it, etc.) without
    // touching individual tile data. Default = white = no-op tint.
    // This is also the only field the editor inspector exposes — every
    // other tilemap edit (paint, erase, set tileset) flows through this
    // class's methods or future editor paint tooling.
    public unsafe Color Color
    {
        get
        {
            float r, g, b, a;
            NativeBindings.GetColor(Entity.ID, &r, &g, &b, &a);
            return new Color(r, g, b, a);
        }
        set => NativeBindings.SetColor(Entity.ID, value.R, value.G, value.B, value.A);
    }

    // Tilemap geometry knobs (see the C++ header for the layout formula).
    // Properties round-trip the underlying field through the native side so
    // the managed C# side never holds parallel state.
    public unsafe Vector2 TileAnchor
    {
        get
        {
            float x, y;
            NativeBindings.GetTileAnchor(Entity.ID, &x, &y);
            return new Vector2(x, y);
        }
        set => NativeBindings.SetTileAnchor(Entity.ID, value.X, value.Y);
    }

    public unsafe Vector2 CellSize
    {
        get
        {
            float x, y;
            NativeBindings.GetCellSize(Entity.ID, &x, &y);
            return new Vector2(x, y);
        }
        set => NativeBindings.SetCellSize(Entity.ID, value.X, value.Y);
    }

    public unsafe Vector2 CellSpace
    {
        get
        {
            float x, y;
            NativeBindings.GetCellSpace(Entity.ID, &x, &y);
            return new Vector2(x, y);
        }
        set => NativeBindings.SetCellSpace(Entity.ID, value.X, value.Y);
    }

    public unsafe void SetTile(Vector2Int cell, Tile tile)
    {
        NativeBindings.SetTile(Entity.ID, cell.X, cell.Y, &tile);
    }

    // Span-based batch. The caller's spans must have equal length. Pinning
    // is done with `fixed` so the native side gets stable pointers for the
    // duration of the call; zero allocations on the managed side.
    public unsafe void SetTiles(ReadOnlySpan<Vector2Int> cells, ReadOnlySpan<Tile> tiles)
    {
        if (cells.Length != tiles.Length) return;
        if (cells.IsEmpty) return;

        fixed (Vector2Int* cellsPtr = cells)
        fixed (Tile* tilesPtr = tiles)
        {
            NativeBindings.SetTiles(Entity.ID, cellsPtr, tilesPtr, cells.Length);
        }
    }

    // Uniform-tile overload — writes the same `tile` to every cell in the
    // span. Useful for "fill brush" / "paint rectangle" tools where every
    // painted cell shares one tile definition. Same chunk-batched native
    // path as SetTiles(span, span); zero managed allocation.
    public unsafe void SetTiles(ReadOnlySpan<Vector2Int> cells, Tile tile)
    {
        if (cells.IsEmpty) return;
        fixed (Vector2Int* cellsPtr = cells)
        {
            NativeBindings.SetTilesUniform(Entity.ID, cellsPtr, cells.Length, &tile);
        }
    }

    public unsafe Tile GetTile(Vector2Int cell)
    {
        Tile result = default;
        NativeBindings.GetTile(Entity.ID, cell.X, cell.Y, &result);
        return result;
    }

    // Returns true when the cell was occupied — distinguishes "unset" from
    // "explicitly default-valued" for callers that need it. Mirrors the
    // native int return convention.
    public unsafe bool TryGetTile(Vector2Int cell, out Tile tile)
    {
        Tile result = default;
        int occupied = NativeBindings.GetTile(Entity.ID, cell.X, cell.Y, &result);
        tile = result;
        return occupied != 0;
    }

    public void ClearTile(Vector2Int cell)
    {
        NativeBindings.ClearTile(Entity.ID, cell.X, cell.Y);
    }

    // Clear a batch of tiles by their cell coordinates. Same chunk-batched
    // native path as SetTiles. Cells in chunks that don't exist are
    // silently skipped (matches ClearTile semantics).
    public unsafe void ClearTiles(ReadOnlySpan<Vector2Int> cells)
    {
        if (cells.IsEmpty) return;
        fixed (Vector2Int* cellsPtr = cells)
        {
            NativeBindings.ClearTiles(Entity.ID, cellsPtr, cells.Length);
        }
    }

    // Remove every tile from this tilemap. Drops every chunk so the
    // post-Clear footprint matches a freshly-default-constructed component.
    public void Clear()
    {
        NativeBindings.Clear(Entity.ID);
    }

    // ── Cell ↔ world conversions ────────────────────────────────────
    // Match the formula the native renderer uses (see
    // Tilemap2DComponent.hpp, "Geometry" docstring):
    //
    //   cellPitch = (CellSize + CellSpace) * Transform.Scale
    //   localPos  = cell * cellPitch - TileAnchor
    //   worldPos  = Transform.Position + Rotate(localPos, Transform.Rotation)
    //
    // Both helpers return the *centre* of the cell in world space, NOT
    // a corner — same convention as Unity's
    // `Tilemap.GetCellCenterWorld`. Inverse round-trips when the
    // tilemap has a uniform Transform2D scale (no shear) and the input
    // coordinate falls within the tile-pitch grid; CellSpace > 0 means
    // points that land in the gap between tiles get mapped to the
    // nearest cell (Unity does the same).

    public Vector2 CellToWorld(Vector2Int cell)
    {
        Transform2D transform = Entity.Transform;
        Vector2 scale = transform.Scale;
        Vector2 cellSize = CellSize;
        Vector2 cellSpace = CellSpace;
        Vector2 anchor = TileAnchor;

        float pitchX = (cellSize.X + cellSpace.X) * scale.X;
        float pitchY = (cellSize.Y + cellSpace.Y) * scale.Y;

        float localX = cell.X * pitchX - anchor.X;
        float localY = cell.Y * pitchY - anchor.Y;

        // Apply the entity's rotation around its origin and translate.
        // Rotation is in radians and follows the same +CCW convention as
        // Transform2D / the renderer.
        float rotation = transform.Rotation;
        float worldX, worldY;
        if (rotation != 0.0f)
        {
            float cs = Mathf.Cos(rotation);
            float sn = Mathf.Sin(rotation);
            worldX = transform.Position.X + localX * cs - localY * sn;
            worldY = transform.Position.Y + localX * sn + localY * cs;
        }
        else
        {
            worldX = transform.Position.X + localX;
            worldY = transform.Position.Y + localY;
        }
        return new Vector2(worldX, worldY);
    }

    public Vector2Int WorldToCell(Vector2 world)
    {
        Transform2D transform = Entity.Transform;
        Vector2 scale = transform.Scale;
        Vector2 cellSize = CellSize;
        Vector2 cellSpace = CellSpace;
        Vector2 anchor = TileAnchor;

        // Strip the entity's translation, then rotate by -Rotation to
        // land back in entity-local space (Rotate is its own inverse
        // under sign flip on `rotation`).
        float dx = world.X - transform.Position.X;
        float dy = world.Y - transform.Position.Y;
        float rotation = transform.Rotation;
        float localX, localY;
        if (rotation != 0.0f)
        {
            float cs = Mathf.Cos(-rotation);
            float sn = Mathf.Sin(-rotation);
            localX = dx * cs - dy * sn;
            localY = dx * sn + dy * cs;
        }
        else
        {
            localX = dx;
            localY = dy;
        }

        // Pitch == 0 (degenerate CellSize+CellSpace+Scale) would divide
        // by zero — return (0, 0) rather than NaN-cast-to-int. A real
        // tilemap with a zero pitch can't render, so the value the
        // caller gets back is moot — we just need to avoid crashing.
        float pitchX = (cellSize.X + cellSpace.X) * scale.X;
        float pitchY = (cellSize.Y + cellSpace.Y) * scale.Y;
        if (pitchX == 0.0f || pitchY == 0.0f)
        {
            return new Vector2Int(0, 0);
        }

        // Cell N's centre is at localPos = N*pitch - anchor, so each
        // cell occupies [(N - 0.5) * pitch - anchor, (N + 0.5) * pitch
        // - anchor] along each axis. Adding 0.5 before flooring rounds
        // to the nearest cell centre — correct for both positive and
        // negative coordinates without the banker's-rounding hazard of
        // `Math.Round`.
        float cellFx = (localX + anchor.X) / pitchX + 0.5f;
        float cellFy = (localY + anchor.Y) / pitchY + 0.5f;
        int cellX = (int)Mathf.Floor(cellFx);
        int cellY = (int)Mathf.Floor(cellFy);
        return new Vector2Int(cellX, cellY);
    }
}
