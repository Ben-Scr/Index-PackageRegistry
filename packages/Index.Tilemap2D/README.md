# Index.Tilemap2D — Using the package in a project

A 2D tilemap component (chunked sparse storage), render system, and editor inspector. This file is the user-facing how-to. The architectural notes live in `include/Tilemap2DComponent.hpp`.

---

## ⚠ Current limitation: storage works, rendering is a stub

`Tilemap2DComponent` is fully functional for storage, editing, and now `.scene`/`.prefab` serialization. The visible-on-screen story is **incomplete**: `TilemapRenderSystem::OnPreRender` walks every chunk and counts occupied tiles for the inspector ("Used cells", "Chunks") but doesn't emit any draw calls yet — the engine has no public per-frame instance-submit hook on `Renderer2D` to plug into.

What works today:
- Adding the component, painting cells in the inspector, multi-select editing
- Round-tripping in scene + prefab files (the fix landed alongside this doc)
- Reading/writing tiles from C# via `Tilemap2DComponent.SetTile` / `GetTile` / `SetTiles`
- Statistics (`UsedCellCount`, `ComputeUsedBounds`)

What doesn't work yet:
- Tiles are not drawn on screen at runtime or in the editor viewport
- This is gated on a render-graph extension point (see `TilemapRenderSystem.cpp` line 54 TODO)

If your goal is "see tiles drawn", the package isn't there yet — but everything below still applies, the visible output just lights up later when the renderer hook lands.

---

## 1. Enable the package in your project

Open your project's `index-project.json` and add a top-level `"packages"` array with `"Index.Tilemap2D"` in it:

```json
{
  "name": "MyProject",
  "engineVersion": "2026.1.0",
  "startupScene": "SampleScene",
  "buildScenes": ["SampleScene"],
  "packages": [
    "Index.Tilemap2D"
  ]
}
```

Without this entry the premake package loader skips the package and the native DLL never gets built into your project, so the component won't appear in the Add Component popup.

After editing `index-project.json`, regenerate project files:

```sh
./vendor/bin/premake5.exe vs2022   # Windows
./vendor/bin/premake5    gmake2    # Linux
```

Then rebuild. You should see `Pkg.Index.Tilemap2D.Native` in the build output and a log line on launch:

```
[Pkg.Index.Tilemap2D] Loaded - Tilemap2DComponent registered (chunked sparse, 16x16 per chunk; serialized as 'Tilemap2D').
```

## 2. Add the render system to a scene (optional but recommended for stats)

In your scene-setup C++ (the function that calls `RegisterScene` / `def.AddSystem<...>()`):

```cpp
#include "TilemapRenderSystem.hpp"

void ConfigureScenes(Index::Application& app) {
    auto& def = app.GetSceneManager()->RegisterScene("SampleScene");
    def.AddSystem<Index::Tilemap2D::TilemapRenderSystem>();
    // ... your other systems
}
```

The system is `OnPreRender`-only, so it runs after Update and before render. It currently maintains `m_LastTilesEmitted` and `m_LastChunksWalked` as inspector stats. Once the render hook lands, these become the actual draw counts.

## 3. Add the component to an entity (editor)

1. Select an entity in the Hierarchy.
2. Click "Add Component" in the Inspector.
3. Find "Rendering > Tilemap 2D".

The component appears with these inspector controls:
- **Used cells / Chunks / Bounds** — read-only stats.
- **Tileset** — the active brush texture. Drag a `.png` from the Asset Browser onto the entity, or set it via the texture picker on the Sprite Renderer pattern (a dedicated picker on Tilemap2D is the next polish step; today the field is informational and you'd set it via `Tilemap2DComponent::TilesetTexture` from code).
- **Cell paint** — numeric `X` / `Y` cell coordinates plus a brush color, with `Paint` and `Erase` buttons. Multi-select painting writes to every selected tilemap.
- **Tip** — drag a texture from the Asset Browser onto the entity to set the tileset.

Painted tiles round-trip in `.scene` and `.prefab` files now (you can verify by saving the scene and inspecting the JSON — look for the `"Tilemap2D"` key on the entity, with `tilesetTexture`, `chunks[].cx/cy/occupancy/tiles`).

## 4. Use it from C++ at runtime

```cpp
#include "Tilemap2DComponent.hpp"

void SpawnRoom(Index::Scene& scene, Index::Entity tilemapEntity) {
    auto& tilemap = tilemapEntity.GetComponent<Index::Tilemap2D::Tilemap2DComponent>();

    // Set a single tile.
    Index::Tilemap2D::Tile floor;
    floor.Texture   = tilemap.TilesetTexture;          // reuse the brush texture
    floor.TileColor = Index::Color(0.8f, 0.8f, 0.8f, 1.0f);
    tilemap.SetTile({ 0, 0 }, floor);

    // Batch a row — single hash-map probe per chunk thanks to SetTiles.
    std::vector<glm::ivec2> cells;
    std::vector<Index::Tilemap2D::Tile> tiles;
    for (int x = 1; x < 16; ++x) {
        cells.push_back({ x, 0 });
        tiles.push_back(floor);
    }
    tilemap.SetTiles(cells, tiles);

    // Read & clear.
    Index::Tilemap2D::Tile current = tilemap.GetTile({ 0, 0 });
    tilemap.ClearTile({ 5, 0 });
}
```

`SetTile` is O(1) once the underlying chunk exists; the first write into a new chunk allocates one `unique_ptr<Chunk>`. `SetTiles` groups consecutive writes by chunk so a run of N tiles within the same chunk is one map probe.

## 5. Use it from C# at runtime

```csharp
using Index;
using Index.Tilemap2D;

public class RoomSpawner : EntityScript
{
    [ShowInEditor] private Texture floorTexture;

    public override void OnStart()
    {
        var tilemap = GetComponent<Tilemap2DComponent>();
        if (tilemap == null) return;

        tilemap.Tileset = floorTexture;

        var floor = new Tile {
            Texture   = floorTexture?.UUID ?? 0,
            TileColor = new Color(0.8f, 0.8f, 0.8f, 1f),
        };

        // Single set
        tilemap.SetTile(new Vector2Int(0, 0), floor);

        // Batch (zero managed allocation thanks to ReadOnlySpan)
        Span<Vector2Int> cells = stackalloc Vector2Int[15];
        Span<Tile>       tiles = stackalloc Tile[15];
        for (int x = 1; x <= 15; ++x) {
            cells[x - 1] = new Vector2Int(x, 0);
            tiles[x - 1] = floor;
        }
        tilemap.SetTiles(cells, tiles);

        if (tilemap.TryGetTile(new Vector2Int(0, 0), out var t)) {
            Log.Info($"Cell (0,0) holds texture {t.Texture}, color={t.TileColor}");
        }
    }
}
```

The C# side is a thin DllImport wrapper over the same native chunk store — there is no parallel managed copy of the data. Spans pin via `fixed`; one P/Invoke per `SetTiles` call regardless of N.

## 6. Serialization (just landed)

You don't have to do anything — adding the package via `index-project.json` and the component via the inspector is enough. The tilemap will round-trip in:
- `.scene` files (via `SceneSerializer::SaveToFile` / `LoadFromFile`)
- `.prefab` files (when prefab system is built out — see audit H14)
- Editor play-mode entry/exit (which uses the same disk round-trip)

The JSON shape is documented in `include/Tilemap2DComponent.hpp` ("Serialization layout").

## 7. Troubleshooting

| Symptom | Likely cause |
|---|---|
| "Tilemap 2D" doesn't appear in Add Component popup | Package not in `index-project.json`'s `packages` array, or premake wasn't re-run after editing it. |
| Editor crashes when adding the component | Should be fixed — see audit notes for `PackageImGuiBridge`. If it returns, check `Inspector trace:` log lines for the last successful step. |
| Painted tiles disappear after closing/reopening the scene | Should be fixed — this doc landed alongside the serialization wiring. If it returns, grep the `.scene` file for `"Tilemap2D"` to see whether saving or loading is the failing side. |
| Tiles don't draw on screen | Expected today — see the limitation block at the top. |
| `Tileset` field hard to set in the inspector | The dedicated picker is on the polish list. For now set `tilemap.TilesetTexture` from code, or drag the texture asset onto the entity in the Hierarchy. |

## See also

- `include/Tilemap2DComponent.hpp` — storage strategy and JSON layout.
- `src/TilemapRenderSystem.cpp` — render TODO and per-tile instance-submit shape.
- `index-package.lua` — package manifest (engine_core + csharp layers).
- `Index-Engine/src/Packages/PackageRegistration.hpp` — `RegisterSerializableComponent` helper used by this package's `PackageEntry.cpp`.
