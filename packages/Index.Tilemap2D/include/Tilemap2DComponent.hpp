#pragma once

// ── Index.Tilemap2D — chunked sparse 2D tilemap ──────────────────────
//
// Storage strategy
// ----------------
// Tiles are grouped into 16×16 chunks (`kChunkSize²` cells per chunk). Each
// chunk stores its 256 tiles as a flat std::array plus a 256-bit occupancy
// bitmap (4 × uint64). Chunks are owned by the component in an
// std::unordered_map keyed by chunk-coordinate.
//
// Why chunked sparse vs. a dense 2D array:
//   • Tilemaps in real games are usually large but sparsely populated
//     (an interior level might fill 5–10 % of its bounding box).
//   • A dense array sized to the bounds would waste memory and force the
//     user to declare bounds up front. Chunks let the world grow on demand
//     in any direction — including negative coordinates, since the chunk
//     coord is signed.
//   • Per-tile read/write is still O(1): one hash-map lookup + one array
//     index. Once a chunk exists, SetTile is allocation-free; the bitmap
//     write touches a single uint64.
//   • For the bounded case (small fixed-size levels) a dense array would
//     be slightly faster on cold cache lines, but the API is the same — a
//     future variant component could swap the storage if it ever matters.
//
// Tile is a POD struct: an `AssetGUID` (asset-system reference, NOT a raw
// `TextureHandle` index) and a `Color`. SetTile / GetTile do not allocate
// per call once the underlying chunk exists; SetTiles batches the chunk
// lookups so a run of writes within the same chunk is a single map probe.
//
// Serialization layout (explicit, no reflection)
// ----------------------------------------------
// JSON shape, written by SerializeTilemap2D / read by DeserializeTilemap2D
// (both in `Tilemap2DSerialization.hpp`):
//
//   {
//     "tilesetTexture": "<asset-guid-as-decimal-string>",  // optional
//     "chunks": [
//       {
//         "cx": <int>, "cy": <int>,
//         "tiles": [                       // dense — kChunkSize² entries
//           { "tex": "<guid>", "r":..., "g":..., "b":..., "a":... },
//           …
//         ],
//         "occupancy": "<hex-32>"          // 256-bit bitmap, 32 hex chars
//       },
//       …
//     ]
//   }
//
// `tilesetTexture` is an editor convenience for the inspector's
// "active tileset" affordance and is NOT used at runtime — every Tile
// already carries its own texture GUID independently. Renaming or moving
// the texture asset doesn't break the tilemap because we resolve by GUID
// every frame, not by path.

#include "IndexTilemap2DApi.hpp"

#include "Collections/Color.hpp"
#include "Collections/Ids.hpp"
#include "Core/Export.hpp"

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

namespace Index::Tilemap2D {

	// Compile-time constant: tiles per chunk side. Power-of-two so the
	// (cell.x % kChunkSize) reduces to a bit-and. Don't change without
	// updating the bitmap word count and the serializer.
	inline constexpr int kChunkSize = 16;
	inline constexpr int kTilesPerChunk = kChunkSize * kChunkSize;

	// Tile — POD. Stores the texture asset reference as a raw uint64 GUID
	// rather than the AssetGUID class because AssetGUID (= Index::UUID) has
	// a user-defined copy ctor and isn't trivially-copyable; that breaks
	// span<const Tile> batch IO and the matching C# `[StructLayout]` ABI.
	// Convert at the boundary with `AssetGUID(tile.Texture)` when handing
	// off to AssetRegistry / TextureManager. Default-constructed Tile is
	// the "unset" sentinel — Texture == 0 is the universally-invalid asset
	// reference in Index.
	struct Tile {
		std::uint64_t Texture = 0;
		Color         TileColor{};
	};
	static_assert(std::is_trivially_copyable_v<Tile>, "Tile must remain POD for span IO + C# struct ABI.");

	// Hash for glm::ivec2 chunk coords. Pulled in via the unordered_map below.
	struct IVec2Hash {
		std::size_t operator()(const glm::ivec2& v) const noexcept {
			// Hash mix borrowed from boost; cheap and good enough for chunk
			// coordinates which are small integers near origin in practice.
			std::size_t h = static_cast<std::size_t>(static_cast<uint32_t>(v.x));
			h ^= static_cast<std::size_t>(static_cast<uint32_t>(v.y)) + 0x9e3779b9 + (h << 6) + (h >> 2);
			return h;
		}
	};

	// One 16×16 chunk. Storage: tiles[256] + occupancy[256-bit].
	// Owned uniquely by the component (unique_ptr so the chunk-map can rehash
	// without invalidating Tile& references — though we don't currently hand
	// out references across map mutations, this keeps it cheap to do later).
	struct Chunk {
		std::array<Tile, kTilesPerChunk> Tiles{};
		// 4 × uint64 = 256 bits, one per cell. bit i = cell (i % kChunkSize, i / kChunkSize).
		std::array<std::uint64_t, 4>     Occupancy{};

		bool IsCellOccupied(int local) const noexcept {
			return (Occupancy[local >> 6] & (std::uint64_t(1) << (local & 63))) != 0;
		}
		void MarkCellOccupied(int local) noexcept {
			Occupancy[local >> 6] |= (std::uint64_t(1) << (local & 63));
		}
		void ClearCellOccupied(int local) noexcept {
			Occupancy[local >> 6] &= ~(std::uint64_t(1) << (local & 63));
		}
	};

	// Tilemap2DComponent — POD-style (no virtuals, no inheritance, just
	// data). The component itself contains the chunk map and a tileset
	// reference (editor metadata; runtime ignores it). Methods are all
	// non-virtual and small; the engine's component registry stores this
	// by-value in the EnTT registry like every other component.
	//
	// Copy semantics: the engine's ComponentRegistry generates a `copyTo`
	// callback that invokes T's copy-assignment operator (used by Entity
	// duplicate, prefab apply, etc.). std::unique_ptr<Chunk> is move-only,
	// so we provide explicit copy ctor / copy-assignment that deep-clone
	// every chunk. Move ops stay defaulted.
	struct INDEX_PACKAGE_API Tilemap2DComponent {
		// Active "tileset" texture — used by tooling (scripts, future paint
		// editor) as the brush's source. Not surfaced in the inspector; the
		// component is a data store. The renderer uses Tile::Texture per cell
		// and only falls back to TilesetTexture when a cell has no explicit
		// texture.
		AssetGUID TilesetTexture = AssetGUID(0);

		// Tilemap-wide color tint. Multiplied with each tile's per-cell
		// TileColor at render time, so this acts as a global modulation
		// (alpha-fade the whole tilemap, color-shift it, etc.) without
		// touching per-tile data. Default = Color::White() = no-op tint.
		// This is the ONLY field exposed by the inspector — everything else
		// is data driven via scripts or future editor paint tooling.
		Index::Color Color = Index::Color::White();

		// Tilemap geometry knobs. The renderer composes them as follows:
		//   cellPitch = (CellSize + CellSpace) * Transform.Scale
		//   localPos  = (cell * cellPitch) - TileAnchor
		//   worldPos  = Transform.Position + Rotate(localPos, Transform.Rotation)
		//   tileSize  = CellSize * Transform.Scale
		// Defaults preserve the historic behaviour: 1×1 cells with no gap
		// and the anchor at the entity origin, so existing tilemaps render
		// identically without touching these fields.
		glm::vec2 TileAnchor{ 0.0f, 0.0f };
		glm::vec2 CellSize{ 1.0f, 1.0f };
		glm::vec2 CellSpace{ 0.0f, 0.0f };

		// Chunk-coordinate → chunk owning ptr. unique_ptr keeps each chunk
		// stable in memory across map rehashes.
		std::unordered_map<glm::ivec2, std::unique_ptr<Chunk>, IVec2Hash> Chunks;

		Tilemap2DComponent() = default;
		~Tilemap2DComponent() = default;
		Tilemap2DComponent(Tilemap2DComponent&&) noexcept = default;
		Tilemap2DComponent& operator=(Tilemap2DComponent&&) noexcept = default;

		Tilemap2DComponent(const Tilemap2DComponent& other)
			: TilesetTexture(other.TilesetTexture)
			, Color(other.Color)
			, TileAnchor(other.TileAnchor)
			, CellSize(other.CellSize)
			, CellSpace(other.CellSpace) {
			Chunks.reserve(other.Chunks.size());
			for (const auto& [coord, chunk] : other.Chunks) {
				Chunks.emplace(coord, std::make_unique<Chunk>(*chunk));
			}
		}

		Tilemap2DComponent& operator=(const Tilemap2DComponent& other) {
			if (&other == this) return *this;
			TilesetTexture = other.TilesetTexture;
			Color = other.Color;
			TileAnchor = other.TileAnchor;
			CellSize = other.CellSize;
			CellSpace = other.CellSpace;
			Chunks.clear();
			Chunks.reserve(other.Chunks.size());
			for (const auto& [coord, chunk] : other.Chunks) {
				Chunks.emplace(coord, std::make_unique<Chunk>(*chunk));
			}
			return *this;
		}

		// ── Hot path ────────────────────────────────────────────────

		// SetTile — O(1) once the underlying chunk exists. Allocates exactly
		// once on first write per chunk (the unique_ptr<Chunk> creation),
		// never again on subsequent writes within that chunk.
		void SetTile(glm::ivec2 cell, Tile tile);

		// SetTiles — span-based batch. Groups writes by chunk so a run of N
		// tiles within a single chunk is one map probe + N flat writes,
		// rather than the N probes a SetTile-loop would do. Spans must have
		// the same length; cells.size() != tiles.size() returns silently.
		void SetTiles(std::span<const glm::ivec2> cells, std::span<const Tile> tiles);

		// SetTiles — uniform-tile overload. Writes the same `tile` to every
		// position in `cells`. Same chunk-batching as the span overload.
		// Useful for "fill brush" / "paint rectangle" tools that draw the
		// same tile across many cells.
		void SetTiles(std::span<const glm::ivec2> cells, Tile tile);

		// GetTile — returns the stored value when the cell is occupied, or
		// the sentinel default-constructed Tile{} (Texture == 0, Color all
		// zero) when unset. Splitting this into TryGetTile would make sense
		// if callers needed to distinguish "unset" from "explicitly cleared
		// to default" — currently they don't.
		Tile GetTile(glm::ivec2 cell) const;

		// Convenience: clear a tile. Equivalent to "occupancy bit off"; the
		// stored Tile data is left in place (not zeroed) so a future
		// re-occupy can reuse the slot without writing the whole struct.
		void ClearTile(glm::ivec2 cell);

		// Clear a batch of tiles by their cell coordinates. Same chunk
		// batching as SetTiles — N cells in one chunk become one hashmap
		// probe + N bit clears. Empty span returns silently.
		void ClearTiles(std::span<const glm::ivec2> cells);

		// Remove every tile from the tilemap. Drops all chunks (frees their
		// allocations) so the post-Clear() footprint is the same as a
		// freshly-default-constructed component.
		void Clear();

		// Total occupied tiles across all chunks. O(chunks × 4) bitcount.
		std::size_t UsedCellCount() const;

		// Inclusive bounds in cell-space across all occupied tiles.
		// outMin/outMax are unchanged when the tilemap is empty; check the
		// return value first.
		bool ComputeUsedBounds(glm::ivec2& outMin, glm::ivec2& outMax) const;

		// ── Lookup helpers (used by SetTiles + the renderer) ────────

		// Cell → (chunk coord, local index in 0..kTilesPerChunk-1). Branchless
		// signed-floor-div replacement; floor(cell / chunkSize) for negatives.
		static glm::ivec2 CellToChunk(glm::ivec2 cell) noexcept {
			// Arithmetic-shift floor-divide for power-of-two divisor.
			return { cell.x >> 4, cell.y >> 4 };
		}
		static int CellToLocal(glm::ivec2 cell) noexcept {
			const int lx = cell.x & (kChunkSize - 1);
			const int ly = cell.y & (kChunkSize - 1);
			return ly * kChunkSize + lx;
		}
	};

} // namespace Index::Tilemap2D
