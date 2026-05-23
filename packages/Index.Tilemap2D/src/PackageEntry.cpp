#include "IndexTilemap2DApi.hpp"
#include "Tilemap2DComponent.hpp"
#include "Tilemap2DSerialization.hpp"

#include "Core/Log.hpp"
#include "Inspector/PropertyRegistration.hpp"
#include "Packages/PackageRegistration.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/Json.hpp"

#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Tags.hpp"
#include "Graphics/Gizmo.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Graphics/TextureManager.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <unordered_map>

// Trampolines for the engine's generic serialize/deserialize dispatch. The
// engine calls these as flat function pointers — no Tilemap2D types leak into
// ComponentInfo's signature beyond what AssetGUID/Color already do.
namespace Index::Tilemap2D {
    static Json::Value SerializeAdapter(const Tilemap2DComponent& tilemap) {
        return SerializeTilemap2D(tilemap);
    }
    static void DeserializeAdapter(Tilemap2DComponent& tilemap, const Json::Value& value) {
        DeserializeTilemap2D(value, tilemap);
    }

    // Renderer2D instance contributor — emits one Instance44 per occupied
    // tile so Tilemap2D actually shows up on screen. Registered with
    // Renderer2D::RegisterInstanceContributor in IndexPackage_OnLoad.
    //
    // Texture lookup cache: thread_local + clear() instead of fresh per call.
    // The renderer is single-threaded; one allocation amortised over the
    // process lifetime, replaces a per-frame heap allocation that the audit
    // flagged. Cleared at the top of each call so GUID->handle entries don't
    // leak across frames if a tile's texture is reassigned.
    //
    // Per-tile transform:
    //   cellPitch = (CellSize + CellSpace) * Scale   (spacing between centres)
    //   localPos  = (cell * cellPitch) - TileAnchor  (entity-local position)
    //   worldPos  = Position + Rotate(localPos, Rotation) (rotated as group)
    //   tileSize  = CellSize * Scale                  (per-tile render size)
    //
    // Tile orientation stays at 0 — the *tilemap* rotates as a group around
    // the entity origin (positions transformed by the entity's rotation),
    // but each tile sprite stays axis-aligned. This gives a "rotate the
    // whole grid as one object" effect without each tile spinning around
    // its own centre. One sin+cos is precomputed per entity; the per-tile
    // cost is two muls + four adds.
    //
    // Culling: chunk-level. For unrotated tilemaps an axis-aligned AABB is
    // exact; for rotated tilemaps we compute the rotated 4-corner AABB so
    // chunks that are visible after rotation aren't incorrectly skipped.
    // Per-tile culling would buy little and cost a lot in inner-loop branches.
    //
    // Stats are exposed for the (now no-op) TilemapRenderSystem via static
    // atomics so the inspector / scripts can still read "tiles emitted last
    // frame" without the system maintaining its own redundant walk.
    //
    // Texture fallback: if a tile's per-cell texture is 0 we fall back to the
    // tilemap's brush (TilesetTexture). If both are 0, the renderer's own
    // default-texture fallback in CollectAndRenderInstances catches it.
    static uint32_t s_RendererContributorToken = 0;
    std::atomic<std::size_t> g_LastTilesEmitted{ 0 };
    std::atomic<std::size_t> g_LastChunksWalked{ 0 };

    static void EmitTilemapInstances(const Index::Scene& scene,
                                     const Index::AABB& viewportAABB,
                                     std::vector<Index::Instance44>& outInstances) {
        thread_local std::unordered_map<std::uint64_t, Index::TextureHandle> handleCache;
        handleCache.clear();
        auto resolveHandle = [&](std::uint64_t guid) -> Index::TextureHandle {
            if (guid == 0) return Index::TextureHandle{};
            auto it = handleCache.find(guid);
            if (it != handleCache.end()) return it->second;
            const Index::TextureHandle h = Index::TextureManager::LoadTextureByUUID(guid);
            handleCache.emplace(guid, h);
            return h;
        };

        std::size_t emittedThisCall = 0;
        std::size_t chunksThisCall = 0;

        // Same const_cast pattern Renderer2D uses on the registry — we don't
        // mutate component data, just need a non-const view to satisfy EnTT's
        // lazy storage initialization.
        auto& registry = const_cast<entt::registry&>(scene.GetRegistry());
        auto view = registry.view<Index::Transform2DComponent, Tilemap2DComponent>(
            entt::exclude<Index::DisabledTag>);

        for (auto entity : view) {
            const auto& transform = view.get<Index::Transform2DComponent>(entity);
            const auto& tilemap = view.get<Tilemap2DComponent>(entity);

            const std::uint64_t fallbackGuid = static_cast<std::uint64_t>(tilemap.TilesetTexture);
            const float scaleX = transform.Scale.x;
            const float scaleY = transform.Scale.y;
            const float rotation = transform.Rotation;
            const bool  isRotated = rotation != 0.0f;

            // Geometry (see header docstring): cellPitch is the centre-to-
            // centre spacing including the gap; tileSize is the actual draw
            // extent per tile; anchor offsets the whole grid in local space.
            const float cellPitchX = (tilemap.CellSize.x + tilemap.CellSpace.x) * scaleX;
            const float cellPitchY = (tilemap.CellSize.y + tilemap.CellSpace.y) * scaleY;
            const float tileWidth  = tilemap.CellSize.x * scaleX;
            const float tileHeight = tilemap.CellSize.y * scaleY;
            const float anchorX = tilemap.TileAnchor.x;
            const float anchorY = tilemap.TileAnchor.y;

            // Precompute sin/cos per entity — one trig pair shared across
            // every tile in the entity. Skipped when rotation == 0.
            const float cs = isRotated ? std::cos(rotation) : 1.0f;
            const float sn = isRotated ? std::sin(rotation) : 0.0f;

            // rotateAroundOrigin transforms a point (lx, ly) given in the
            // entity's local frame into world space, accounting for the
            // entity's position + rotation. Inlined as a lambda so the
            // unrotated fast-path can short-circuit.
            auto rotateAroundOrigin = [&](float lx, float ly,
                                          float& outWorldX, float& outWorldY) {
                if (isRotated) {
                    outWorldX = transform.Position.x + lx * cs - ly * sn;
                    outWorldY = transform.Position.y + lx * sn + ly * cs;
                } else {
                    outWorldX = transform.Position.x + lx;
                    outWorldY = transform.Position.y + ly;
                }
            };

            for (const auto& [chunkCoord, chunk] : tilemap.Chunks) {
                if (!chunk) continue;

                // Chunk AABB in entity-local space: bounding box of every
                // possible tile centre in this chunk, expanded by half a
                // tile on each side. For rotated tilemaps we then take the
                // world-space AABB of those four rotated corners.
                const float chunkLx0 = static_cast<float>(chunkCoord.x * kChunkSize) * cellPitchX - anchorX - tileWidth  * 0.5f;
                const float chunkLy0 = static_cast<float>(chunkCoord.y * kChunkSize) * cellPitchY - anchorY - tileHeight * 0.5f;
                const float chunkLx1 = chunkLx0 + cellPitchX * kChunkSize + tileWidth;
                const float chunkLy1 = chunkLy0 + cellPitchY * kChunkSize + tileHeight;

                Index::AABB chunkAABB{};
                if (!isRotated) {
                    chunkAABB.Min = Index::Vec2{
                        transform.Position.x + chunkLx0,
                        transform.Position.y + chunkLy0 };
                    chunkAABB.Max = Index::Vec2{
                        transform.Position.x + chunkLx1,
                        transform.Position.y + chunkLy1 };
                } else {
                    // Rotate all 4 chunk corners and take their world-space AABB.
                    // Six trig-free transforms total (4 corners × 2 axes minus
                    // shared work); cheap per chunk.
                    float wx, wy;
                    rotateAroundOrigin(chunkLx0, chunkLy0, wx, wy);
                    float minX = wx, maxX = wx, minY = wy, maxY = wy;
                    auto fold = [&](float lx, float ly) {
                        rotateAroundOrigin(lx, ly, wx, wy);
                        minX = std::min(minX, wx); maxX = std::max(maxX, wx);
                        minY = std::min(minY, wy); maxY = std::max(maxY, wy);
                    };
                    fold(chunkLx1, chunkLy0);
                    fold(chunkLx1, chunkLy1);
                    fold(chunkLx0, chunkLy1);
                    chunkAABB.Min = Index::Vec2{ minX, minY };
                    chunkAABB.Max = Index::Vec2{ maxX, maxY };
                }
                if (!Index::AABB::Intersects(viewportAABB, chunkAABB)) continue;

                ++chunksThisCall;

                // Iterate set bits of the 256-bit occupancy bitmap rather than
                // every cell — a chunk holding 8 tiles costs 8 inner-loop
                // iterations, not 256.
                for (int wi = 0; wi < 4; ++wi) {
                    std::uint64_t word = chunk->Occupancy[wi];
                    while (word != 0) {
                        const int bit = std::countr_zero(word);
                        word &= word - 1;
                        const int local = wi * 64 + bit;
                        const int lx = local & (kChunkSize - 1);
                        const int ly = local / kChunkSize;
                        const Tile& tile = chunk->Tiles[local];

                        // Tile centre in entity-local space (cell index ×
                        // pitch, anchored, NOT yet rotated).
                        const float localX = static_cast<float>(chunkCoord.x * kChunkSize + lx) * cellPitchX - anchorX;
                        const float localY = static_cast<float>(chunkCoord.y * kChunkSize + ly) * cellPitchY - anchorY;
                        float worldX, worldY;
                        rotateAroundOrigin(localX, localY, worldX, worldY);

                        const std::uint64_t guid = tile.Texture != 0 ? tile.Texture : fallbackGuid;

                        // Final tile color = per-tile color * tilemap-wide tint.
                        // Tilemap.Color defaults to White (1,1,1,1) so the
                        // multiply is a no-op for users who never touched it.
                        const Index::Color tinted{
                            tile.TileColor.r * tilemap.Color.r,
                            tile.TileColor.g * tilemap.Color.g,
                            tile.TileColor.b * tilemap.Color.b,
                            tile.TileColor.a * tilemap.Color.a };

                        outInstances.emplace_back(
                            Index::Vec2{ worldX, worldY },
                            Index::Vec2{ tileWidth, tileHeight },
                            // 0 rotation per tile — the tilemap rotates as a
                            // group via the position transform; each tile
                            // sprite stays axis-aligned.
                            0.0f,
                            tinted,
                            resolveHandle(guid),
                            static_cast<short>(0),
                            static_cast<std::uint8_t>(0));
                        ++emittedThisCall;
                    }
                }
            }
        }

        // Publish the stats. relaxed is fine — these are diagnostics, not
        // synchronization. Last-writer-wins across multi-scene calls (one per
        // contributor invocation per scene); the inspector reading later sees
        // a coherent snapshot of the most recent invocation.
        g_LastTilesEmitted.store(emittedThisCall, std::memory_order_relaxed);
        g_LastChunksWalked.store(chunksThisCall, std::memory_order_relaxed);
    }

    // Editor-only viewport gizmo for the selected entity. Paints a per-cell
    // outline grid so the user can see where tiles will land relative to the
    // entity transform and the configured TileAnchor / CellSize / CellSpace.
    //
    // Range strategy:
    //   • If the tilemap has any tiles, use the used bounds + 1 cell of empty
    //     padding on each side so the surrounding empty layout is also visible.
    //   • If the tilemap is empty, fall back to a small window around (0,0)
    //     so the user can see the cell layout before painting any tiles.
    //   • If the resulting window exceeds k_MaxCellGrid in either dimension,
    //     stop drawing per-cell outlines and switch to chunk-level outlines —
    //     keeps the gizmo readable and within the renderer's vertex budget.
    //
    // Colors:
    //   • Empty cell      — dim cyan.
    //   • Occupied cell   — bright cyan.
    //   • Origin (0,0)    — orange, so the anchor reference is always findable.
    //
    // The whole grid rotates with the entity (matching the renderer's
    // group-rotation behaviour where each tile stays axis-aligned but the
    // grid as a whole turns around the entity origin).
    static void DrawTilemap2DEditorGizmo(Index::Entity entity) {
        using namespace Index;

        // Tilemap2D requires Transform2D for placement; the engine doesn't
        // enforce this as a dependsOn relationship, so we self-guard here.
        if (!entity.HasComponent<Transform2DComponent>() ||
            !entity.HasComponent<Tilemap2DComponent>()) {
            return;
        }

        const auto& transform = entity.GetComponent<Transform2DComponent>();
        const auto& tilemap   = entity.GetComponent<Tilemap2DComponent>();

        const float scaleX = transform.Scale.x;
        const float scaleY = transform.Scale.y;
        const float rotation = transform.Rotation;
        const float rotationDegrees = transform.GetRotationDegrees();
        const bool  isRotated = rotation != 0.0f;

        const float cellPitchX = (tilemap.CellSize.x + tilemap.CellSpace.x) * scaleX;
        const float cellPitchY = (tilemap.CellSize.y + tilemap.CellSpace.y) * scaleY;
        const float tileWidth  = tilemap.CellSize.x * scaleX;
        const float tileHeight = tilemap.CellSize.y * scaleY;
        const float anchorX = tilemap.TileAnchor.x;
        const float anchorY = tilemap.TileAnchor.y;

        // Degenerate geometry would emit zero-area squares (visible as glitchy
        // dots) — bail rather than draw something misleading.
        if (tileWidth <= 0.0f || tileHeight <= 0.0f) return;
        if (cellPitchX <= 0.0f || cellPitchY <= 0.0f) return;

        const float cs = isRotated ? std::cos(rotation) : 1.0f;
        const float sn = isRotated ? std::sin(rotation) : 0.0f;
        auto rotateLocal = [&](float lx, float ly) -> Vec2 {
            if (isRotated) {
                return Vec2{ transform.Position.x + lx * cs - ly * sn,
                             transform.Position.y + lx * sn + ly * cs };
            }
            return Vec2{ transform.Position.x + lx, transform.Position.y + ly };
        };

        // Used-bounds + padding, or a default window around origin when empty.
        constexpr int k_PadCells      = 1;
        constexpr int k_FallbackCells = 2;
        constexpr int k_MaxCellGrid   = 32;

        glm::ivec2 usedMin{ 0, 0 }, usedMax{ -1, -1 };
        const bool hasBounds = tilemap.ComputeUsedBounds(usedMin, usedMax);

        glm::ivec2 minCell, maxCell;
        if (hasBounds) {
            minCell = { usedMin.x - k_PadCells, usedMin.y - k_PadCells };
            maxCell = { usedMax.x + k_PadCells, usedMax.y + k_PadCells };
        } else {
            minCell = { -k_FallbackCells, -k_FallbackCells };
            maxCell = {  k_FallbackCells,  k_FallbackCells };
        }

        const int spanX = maxCell.x - minCell.x + 1;
        const int spanY = maxCell.y - minCell.y + 1;
        const bool drawIndividualCells = (spanX <= k_MaxCellGrid && spanY <= k_MaxCellGrid);

        // Helper: occupancy via chunk bitmap (NOT GetTile().Texture, because a
        // valid tile may legitimately have Texture==0 and rely on the
        // tilemap's fallback TilesetTexture).
        auto isCellOccupied = [&](int cx, int cy) -> bool {
            const glm::ivec2 chunkCoord = Tilemap2DComponent::CellToChunk({ cx, cy });
            const auto it = tilemap.Chunks.find(chunkCoord);
            if (it == tilemap.Chunks.end() || !it->second) return false;
            return it->second->IsCellOccupied(Tilemap2DComponent::CellToLocal({ cx, cy }));
        };

        const Color emptyColor  { 0.35f, 0.55f, 0.75f, 0.45f };
        const Color filledColor { 0.35f, 0.85f, 1.00f, 0.95f };
        const Color originColor { 1.00f, 0.65f, 0.10f, 1.00f };

        if (drawIndividualCells) {
            Gizmo::SetLineWidth(1.0f);
            for (int cy = minCell.y; cy <= maxCell.y; ++cy) {
                for (int cx = minCell.x; cx <= maxCell.x; ++cx) {
                    const float lx = static_cast<float>(cx) * cellPitchX - anchorX;
                    const float ly = static_cast<float>(cy) * cellPitchY - anchorY;
                    const Vec2 worldCenter = rotateLocal(lx, ly);

                    Color color;
                    if (cx == 0 && cy == 0)        color = originColor;
                    else if (isCellOccupied(cx, cy)) color = filledColor;
                    else                            color = emptyColor;
                    Gizmo::SetColor(color);
                    Gizmo::DrawSquare(worldCenter, Vec2{ tileWidth, tileHeight }, rotationDegrees);
                }
            }
        } else {
            // Tilemap is too large for per-cell outlines — switch to chunk
            // outlines so the user still sees the populated regions at a
            // glance. Each chunk's local AABB spans (kChunkSize-1) full
            // cell-pitches plus one tile width (the half-tile margins on each
            // side). The +0.5 cell-pitch centring matches the renderer's
            // chunk AABB calculation (offset by half a tile inward).
            Gizmo::SetLineWidth(1.5f);
            Gizmo::SetColor(filledColor);
            const float chunkW = static_cast<float>(kChunkSize - 1) * cellPitchX + tileWidth;
            const float chunkH = static_cast<float>(kChunkSize - 1) * cellPitchY + tileHeight;
            for (const auto& [chunkCoord, chunk] : tilemap.Chunks) {
                if (!chunk) continue;
                // Chunk-local centre = midpoint between the first and last
                // cell centre in the chunk.
                const float firstCenterLx = static_cast<float>(chunkCoord.x * kChunkSize) * cellPitchX - anchorX;
                const float firstCenterLy = static_cast<float>(chunkCoord.y * kChunkSize) * cellPitchY - anchorY;
                const float lcx = firstCenterLx + static_cast<float>(kChunkSize - 1) * cellPitchX * 0.5f;
                const float lcy = firstCenterLy + static_cast<float>(kChunkSize - 1) * cellPitchY * 0.5f;
                const Vec2 worldCenter = rotateLocal(lcx, lcy);
                Gizmo::DrawSquare(worldCenter, Vec2{ chunkW, chunkH }, rotationDegrees);
            }

            // Origin cell stays visible as a single bright square so the
            // anchor reference is findable even when chunks dominate.
            Gizmo::SetLineWidth(2.0f);
            Gizmo::SetColor(originColor);
            const Vec2 originCenter = rotateLocal(-anchorX, -anchorY);
            Gizmo::DrawSquare(originCenter, Vec2{ tileWidth, tileHeight }, rotationDegrees);
        }
    }
}

extern "C" INDEX_PACKAGE_API int IndexPackage_OnLoad() {
    using namespace Index;

    // Tilemap2DComponent is fully data-driven now: every editable field is a
    // PropertyDescriptor, so we pass nullptr for drawInspector. The editor's
    // auto-drawer fallback walks `info.properties` and dispatches each one
    // through PropertyDrawer::Draw — same multi-edit / mixed-state / drag-drop
    // behaviour as built-in components. The TilesetTexture field uses the
    // unified ReferencePicker (thumbnail style) because it's a TextureRef.
    std::vector<PropertyDescriptor> tilemapProperties = {
        Properties::Make("Color", "Color", &Tilemap2D::Tilemap2DComponent::Color),
        Properties::Make("TileAnchor", "Tile Anchor", &Tilemap2D::Tilemap2DComponent::TileAnchor),
        Properties::Make("CellSize",   "Cell Size",   &Tilemap2D::Tilemap2DComponent::CellSize),
        Properties::Make("CellSpace",  "Cell Space",  &Tilemap2D::Tilemap2DComponent::CellSpace),
        Properties::MakeTextureRef("TilesetTexture", "Tileset",
            [](const Entity& e) -> uint64_t {
                return static_cast<uint64_t>(e.GetComponent<Tilemap2D::Tilemap2DComponent>().TilesetTexture);
            },
            [](Entity& e, uint64_t uuid) {
                e.GetComponent<Tilemap2D::Tilemap2DComponent>().TilesetTexture = AssetGUID(uuid);
            }),
    };

    Package::RegisterSerializableComponent<Tilemap2D::Tilemap2DComponent>(
        /* displayName    */ "Tilemap 2D",
        /* subcategory    */ "Rendering",
        /* serializedName */ "Tilemap2D",
        /* drawInspector  */ nullptr,
        /* serialize      */ &Tilemap2D::SerializeAdapter,
        /* deserialize    */ &Tilemap2D::DeserializeAdapter,
        /* category       */ ComponentCategory::Component,
        /* properties     */ std::move(tilemapProperties));

    // Editor-only viewport gizmo: draws the cell grid for the selected
    // tilemap so the user can see how TileAnchor / CellSize / CellSpace
    // map to world space before painting any tiles.
    Package::SetEditorGizmo<Tilemap2D::Tilemap2DComponent>(&Tilemap2D::DrawTilemap2DEditorGizmo);

    // Tilemap2D belongs to the visual-output exclusion set: an entity carries
    // exactly one of SpriteRenderer / Image / ParticleSystem2D / Tilemap2D.
    // Declaring our half here means the engine's built-in conflict triple
    // (SpriteRenderer ↔ Image ↔ ParticleSystem2D) doesn't have to know about
    // package-side types — each side declares its own slice and the registry
    // looks both ways at filter time.
    Package::DeclareComponentConflict<Tilemap2D::Tilemap2DComponent, SpriteRendererComponent>();
    Package::DeclareComponentConflict<Tilemap2D::Tilemap2DComponent, ImageComponent>();
    Package::DeclareComponentConflict<Tilemap2D::Tilemap2DComponent, ParticleSystem2DComponent>();

    // Wire actual rendering: register an InstanceContributor with Renderer2D so
    // tiles get pushed into the per-frame batch and participate in the same
    // sort + texture-batch path as built-in sprites/particles. Stored token
    // is used by IndexPackage_OnUnload to detach cleanly.
    Tilemap2D::s_RendererContributorToken =
        Renderer2D::RegisterInstanceContributor(&Tilemap2D::EmitTilemapInstances);

    // Note on TilemapRenderSystem: actual rendering now goes through the
    // Renderer2D instance contributor registered above — the system is no
    // longer required for tiles to appear on screen. The system stays
    // available as an optional stat collector (m_LastTilesEmitted /
    // m_LastChunksWalked); attaching it via `def.AddSystem<...>()` is no
    // longer necessary just to see tiles.
    return 0;
}

extern "C" INDEX_PACKAGE_API void IndexPackage_OnUnload() {
    using namespace Index;
    if (Tilemap2D::s_RendererContributorToken != 0) {
        Renderer2D::UnregisterInstanceContributor(Tilemap2D::s_RendererContributorToken);
        Tilemap2D::s_RendererContributorToken = 0;
    }
}
