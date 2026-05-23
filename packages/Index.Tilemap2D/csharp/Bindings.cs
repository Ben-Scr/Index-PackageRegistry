using System.Runtime.InteropServices;

namespace Index.Tilemap2D;

// Thin DllImport surface bridging Pkg.Index.Tilemap2D into the native
// package Pkg.Index.Tilemap2D.Native. Names match the extern "C"
// definitions in `src/Tilemap2DPInvoke.cpp`.
//
// Marshalling
// -----------
// • Tile and Vector2Int are blittable (sequential layout, primitive
//   fields). They cross the boundary by value or by pointer with no
//   per-call allocation.
// • ReadOnlySpan<T> spans are pinned by the caller (`fixed`) and passed
//   as raw pointer + int32 length — no IntPtr boxing, no array copy.
//
// Runtime discoverability
// -----------------------
// .NET's default DllImport search walks the application directory.
// The native package builds to bin/<config>/Pkg.Index.Tilemap2D.Native/
// (separate folder from the host exe), so bare DllImport will fail to
// resolve the symbol unless the host installs an
// `NativeLibrary.SetDllImportResolver` over package directories. That
// hook lives in the engine's package host, not in this package — flagged
// in the README of this package directory.
internal static unsafe class NativeBindings
{
    private const string Lib = "Pkg.Index.Tilemap2D.Native";

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_SetTile",  CallingConvention = CallingConvention.Cdecl)]
    public static extern void SetTile(ulong entityId, int cellX, int cellY, Tile* tile);

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_SetTiles", CallingConvention = CallingConvention.Cdecl)]
    public static extern void SetTiles(ulong entityId, Vector2Int* cells, Tile* tiles, int count);

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_GetTile",  CallingConvention = CallingConvention.Cdecl)]
    public static extern int  GetTile(ulong entityId, int cellX, int cellY, Tile* outTile);

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_ClearTile", CallingConvention = CallingConvention.Cdecl)]
    public static extern void ClearTile(ulong entityId, int cellX, int cellY);

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_GetTilesetTexture", CallingConvention = CallingConvention.Cdecl)]
    public static extern ulong GetTilesetTexture(ulong entityId);

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_SetTilesetTexture", CallingConvention = CallingConvention.Cdecl)]
    public static extern void SetTilesetTexture(ulong entityId, ulong assetGuid);

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_GetColor", CallingConvention = CallingConvention.Cdecl)]
    public static extern void GetColor(ulong entityId, float* outR, float* outG, float* outB, float* outA);

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_SetColor", CallingConvention = CallingConvention.Cdecl)]
    public static extern void SetColor(ulong entityId, float r, float g, float b, float a);

    // ── Bulk-clear surface ───────────────────────────────────────────

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_Clear", CallingConvention = CallingConvention.Cdecl)]
    public static extern void Clear(ulong entityId);

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_ClearTiles", CallingConvention = CallingConvention.Cdecl)]
    public static extern void ClearTiles(ulong entityId, Vector2Int* cells, int count);

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_SetTilesUniform", CallingConvention = CallingConvention.Cdecl)]
    public static extern void SetTilesUniform(ulong entityId, Vector2Int* cells, int count, Tile* tile);

    // ── Geometry knobs ──────────────────────────────────────────────

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_GetTileAnchor", CallingConvention = CallingConvention.Cdecl)]
    public static extern void GetTileAnchor(ulong entityId, float* outX, float* outY);

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_SetTileAnchor", CallingConvention = CallingConvention.Cdecl)]
    public static extern void SetTileAnchor(ulong entityId, float x, float y);

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_GetCellSize", CallingConvention = CallingConvention.Cdecl)]
    public static extern void GetCellSize(ulong entityId, float* outX, float* outY);

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_SetCellSize", CallingConvention = CallingConvention.Cdecl)]
    public static extern void SetCellSize(ulong entityId, float x, float y);

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_GetCellSpace", CallingConvention = CallingConvention.Cdecl)]
    public static extern void GetCellSpace(ulong entityId, float* outX, float* outY);

    [DllImport(Lib, EntryPoint = "Index_Tilemap2D_SetCellSpace", CallingConvention = CallingConvention.Cdecl)]
    public static extern void SetCellSpace(ulong entityId, float x, float y);
}
