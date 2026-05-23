// extern "C" P/Invoke surface bridging the csharp layer
// (`Pkg.Index.Tilemap2D`) into this native layer
// (`Pkg.Index.Tilemap2D.Native`).
//
// Entity resolution
// -----------------
// The C# side passes the same `ulong` entity ID it already uses everywhere
// else (runtime UUID; falls back to raw entity handle). We walk every
// loaded scene to find the owning scene+handle. This duplicates the
// engine-internal `ResolveEntityReference` from ScriptBindings.cpp; it's
// fine to keep a tiny copy because the alternative is exposing that helper
// from Index-Engine, which would creep public surface for one package.
//
// Marshalling
// -----------
// Primitive structs (Tile, glm::ivec2) are POD; C# layouts the same fields
// in the same order with `[StructLayout(LayoutKind.Sequential)]`. Spans are
// pointer + length — the C# side pins ReadOnlySpan<T> via `fixed`. Zero
// allocation per call on either side.

#include "IndexTilemap2DApi.hpp"
#include "Tilemap2DComponent.hpp"

#include "Components/General/UUIDComponent.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#include <glm/vec2.hpp>

#include <cstdint>

namespace {
	using namespace Index;
	using namespace Index::Tilemap2D;

	// Resolve a C# entity ID (runtime UUID OR raw entity handle) to the
	// Scene + Handle that owns it. Returns nullptr-Scene if not found in
	// any loaded scene.
	bool ResolveEntity(uint64_t entityId, Scene*& outScene, EntityHandle& outHandle) {
		outScene = nullptr;
		outHandle = entt::null;
		if (entityId == 0) return false;

		bool found = false;
		SceneManager::Get().ForeachLoadedScene([&](Scene& scene) {
			if (found) return;
			EntityHandle resolved = entt::null;
			if (scene.TryResolveRuntimeID(entityId, resolved)) {
				outScene = &scene;
				outHandle = resolved;
				found = true;
				return;
			}
			// Walk UUIDComponent for older identity references — same fallback
			// the engine's binding helper uses.
			auto view = scene.GetRegistry().view<UUIDComponent>();
			for (EntityHandle handle : view) {
				if (static_cast<uint64_t>(view.get<UUIDComponent>(handle).Id) == entityId) {
					outScene = &scene;
					outHandle = handle;
					found = true;
					return;
				}
			}
		});
		return found;
	}

	// Common-prefix accessor: resolve and return the Tilemap2DComponent
	// pointer or nullptr. Used by every binding below to keep them tiny.
	Tilemap2DComponent* GetTilemap(uint64_t entityId) {
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntity(entityId, scene, handle)) return nullptr;
		if (!scene->HasComponent<Tilemap2DComponent>(handle)) return nullptr;
		return &scene->GetComponent<Tilemap2DComponent>(handle);
	}
}

// ── Bindings (exported as plain C symbols for DllImport) ──────────────

extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_SetTile(uint64_t entityId, int cellX, int cellY, const Tile* tile) {
	if (!tile) return;
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	tm->SetTile(glm::ivec2{ cellX, cellY }, *tile);
}

extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_SetTiles(uint64_t entityId, const glm::ivec2* cells, const Tile* tiles, int32_t count) {
	if (count <= 0 || !cells || !tiles) return;
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	tm->SetTiles(
		std::span<const glm::ivec2>(cells, static_cast<std::size_t>(count)),
		std::span<const Tile>(tiles, static_cast<std::size_t>(count)));
}

// Returns 1 when the cell is occupied (and outTile is filled), 0 when it
// is unset (outTile filled with the default-constructed sentinel). The
// distinction matters for callers who want to differentiate "empty cell"
// from "explicitly set to default" — see Tilemap2DComponent.hpp.
extern "C" INDEX_PACKAGE_API
int32_t Index_Tilemap2D_GetTile(uint64_t entityId, int cellX, int cellY, Tile* outTile) {
	if (!outTile) return 0;
	*outTile = Tile{};
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return 0;

	const glm::ivec2 cell{ cellX, cellY };
	const auto chunkCoord = Tilemap2DComponent::CellToChunk(cell);
	auto it = tm->Chunks.find(chunkCoord);
	if (it == tm->Chunks.end()) return 0;
	const int local = Tilemap2DComponent::CellToLocal(cell);
	if (!it->second->IsCellOccupied(local)) return 0;
	*outTile = it->second->Tiles[local];
	return 1;
}

extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_ClearTile(uint64_t entityId, int cellX, int cellY) {
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	tm->ClearTile(glm::ivec2{ cellX, cellY });
}

extern "C" INDEX_PACKAGE_API
uint64_t Index_Tilemap2D_GetTilesetTexture(uint64_t entityId) {
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return 0;
	return static_cast<uint64_t>(tm->TilesetTexture);
}

extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_SetTilesetTexture(uint64_t entityId, uint64_t assetGuid) {
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	tm->TilesetTexture = AssetGUID(assetGuid);
}

// Tilemap-wide color tint. The renderer multiplies this with each tile's
// per-cell TileColor so it acts as global modulation — alpha-fade the whole
// tilemap, color-shift it, etc. — without touching per-tile data. Default
// is white = no-op tint.
extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_GetColor(uint64_t entityId, float* outR, float* outG, float* outB, float* outA) {
	if (!outR || !outG || !outB || !outA) return;
	*outR = *outG = *outB = *outA = 1.0f;
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	*outR = tm->Color.r;
	*outG = tm->Color.g;
	*outB = tm->Color.b;
	*outA = tm->Color.a;
}

extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_SetColor(uint64_t entityId, float r, float g, float b, float a) {
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	tm->Color = Index::Color(r, g, b, a);
}

// ── Bulk-clear surface ───────────────────────────────────────────────

extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_Clear(uint64_t entityId) {
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	tm->Clear();
}

extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_ClearTiles(uint64_t entityId, const glm::ivec2* cells, int32_t count) {
	if (count <= 0 || !cells) return;
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	tm->ClearTiles(std::span<const glm::ivec2>(cells, static_cast<std::size_t>(count)));
}

// Single-tile, multi-cell variant of SetTiles. Same chunk-batched write as
// the span-of-tiles overload.
extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_SetTilesUniform(uint64_t entityId, const glm::ivec2* cells, int32_t count, const Tile* tile) {
	if (count <= 0 || !cells || !tile) return;
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	tm->SetTiles(std::span<const glm::ivec2>(cells, static_cast<std::size_t>(count)), *tile);
}

// ── Tilemap geometry knobs ───────────────────────────────────────────
// TileAnchor / CellSize / CellSpace round-trip as (x, y) pairs of floats.
// The renderer composes them into a per-tile transform — see PackageEntry.cpp.

extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_GetTileAnchor(uint64_t entityId, float* outX, float* outY) {
	if (!outX || !outY) return;
	*outX = 0.0f; *outY = 0.0f;
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	*outX = tm->TileAnchor.x;
	*outY = tm->TileAnchor.y;
}

extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_SetTileAnchor(uint64_t entityId, float x, float y) {
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	tm->TileAnchor = glm::vec2(x, y);
}

extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_GetCellSize(uint64_t entityId, float* outX, float* outY) {
	if (!outX || !outY) return;
	*outX = 1.0f; *outY = 1.0f;
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	*outX = tm->CellSize.x;
	*outY = tm->CellSize.y;
}

extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_SetCellSize(uint64_t entityId, float x, float y) {
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	tm->CellSize = glm::vec2(x, y);
}

extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_GetCellSpace(uint64_t entityId, float* outX, float* outY) {
	if (!outX || !outY) return;
	*outX = 0.0f; *outY = 0.0f;
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	*outX = tm->CellSpace.x;
	*outY = tm->CellSpace.y;
}

extern "C" INDEX_PACKAGE_API
void Index_Tilemap2D_SetCellSpace(uint64_t entityId, float x, float y) {
	Tilemap2DComponent* tm = GetTilemap(entityId);
	if (!tm) return;
	tm->CellSpace = glm::vec2(x, y);
}
