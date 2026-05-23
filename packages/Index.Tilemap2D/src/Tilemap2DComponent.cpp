#include "Tilemap2DComponent.hpp"

#include <bit>
#include <limits>

namespace Index::Tilemap2D {

	namespace {
		// Chunk lookup that creates the chunk on miss. Used by SetTile /
		// SetTiles. Returns a non-owning pointer that's valid until the next
		// chunk insert (we hold unique_ptr<Chunk> so the Chunk pointee
		// itself is stable across map rehashes — only the iterator/value
		// references would be invalidated, which we don't keep around).
		Chunk* EnsureChunk(Tilemap2DComponent& tm, glm::ivec2 chunkCoord) {
			auto it = tm.Chunks.find(chunkCoord);
			if (it != tm.Chunks.end()) {
				return it->second.get();
			}
			auto inserted = tm.Chunks.emplace(chunkCoord, std::make_unique<Chunk>());
			return inserted.first->second.get();
		}
	}

	void Tilemap2DComponent::SetTile(glm::ivec2 cell, Tile tile) {
		Chunk* chunk = EnsureChunk(*this, CellToChunk(cell));
		const int local = CellToLocal(cell);
		chunk->Tiles[local] = tile;
		chunk->MarkCellOccupied(local);
	}

	void Tilemap2DComponent::SetTiles(std::span<const glm::ivec2> cells, std::span<const Tile> tiles) {
		if (cells.size() != tiles.size()) {
			return;
		}

		// Batch by chunk: walk the run, hold the current chunk pointer until
		// the chunk-coord changes, then re-resolve. For dense rectangular
		// brush strokes this collapses N hashmap probes into ~N/256.
		Chunk* currentChunk = nullptr;
		glm::ivec2 currentChunkCoord{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };

		const std::size_t count = cells.size();
		for (std::size_t i = 0; i < count; ++i) {
			const glm::ivec2 cell = cells[i];
			const glm::ivec2 chunkCoord = CellToChunk(cell);
			if (chunkCoord != currentChunkCoord) {
				currentChunk = EnsureChunk(*this, chunkCoord);
				currentChunkCoord = chunkCoord;
			}
			const int local = CellToLocal(cell);
			currentChunk->Tiles[local] = tiles[i];
			currentChunk->MarkCellOccupied(local);
		}
	}

	Tile Tilemap2DComponent::GetTile(glm::ivec2 cell) const {
		auto it = Chunks.find(CellToChunk(cell));
		if (it == Chunks.end()) {
			return Tile{};
		}
		const int local = CellToLocal(cell);
		if (!it->second->IsCellOccupied(local)) {
			return Tile{};
		}
		return it->second->Tiles[local];
	}

	void Tilemap2DComponent::ClearTile(glm::ivec2 cell) {
		auto it = Chunks.find(CellToChunk(cell));
		if (it == Chunks.end()) return;
		const int local = CellToLocal(cell);
		it->second->ClearCellOccupied(local);
		// Leave the slot's Tile data in place — the occupancy bit is the
		// source of truth. Avoids a 24-byte write that GetTile / iteration
		// would never observe through the bitmap gate.
	}

	void Tilemap2DComponent::SetTiles(std::span<const glm::ivec2> cells, Tile tile) {
		Chunk* currentChunk = nullptr;
		glm::ivec2 currentChunkCoord{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };
		for (const glm::ivec2 cell : cells) {
			const glm::ivec2 chunkCoord = CellToChunk(cell);
			if (chunkCoord != currentChunkCoord) {
				currentChunk = EnsureChunk(*this, chunkCoord);
				currentChunkCoord = chunkCoord;
			}
			const int local = CellToLocal(cell);
			currentChunk->Tiles[local] = tile;
			currentChunk->MarkCellOccupied(local);
		}
	}

	void Tilemap2DComponent::ClearTiles(std::span<const glm::ivec2> cells) {
		// Symmetric chunk batching: hold the current chunk pointer until the
		// chunk-coord changes. Skips entirely when the chunk doesn't exist —
		// a clear on a never-touched cell is a no-op (matches ClearTile).
		Chunk* currentChunk = nullptr;
		glm::ivec2 currentChunkCoord{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };
		for (const glm::ivec2 cell : cells) {
			const glm::ivec2 chunkCoord = CellToChunk(cell);
			if (chunkCoord != currentChunkCoord) {
				auto it = Chunks.find(chunkCoord);
				currentChunk = (it != Chunks.end()) ? it->second.get() : nullptr;
				currentChunkCoord = chunkCoord;
			}
			if (!currentChunk) continue;
			const int local = CellToLocal(cell);
			currentChunk->ClearCellOccupied(local);
		}
	}

	void Tilemap2DComponent::Clear() {
		// Drop every chunk. The unique_ptrs free the underlying Chunk
		// allocations; the unordered_map keeps its bucket capacity so
		// rebuilding the same shape afterwards doesn't pay the rehash again.
		Chunks.clear();
	}

	std::size_t Tilemap2DComponent::UsedCellCount() const {
		std::size_t total = 0;
		for (const auto& [_, chunk] : Chunks) {
			for (std::uint64_t word : chunk->Occupancy) {
				total += static_cast<std::size_t>(std::popcount(word));
			}
		}
		return total;
	}

	bool Tilemap2DComponent::ComputeUsedBounds(glm::ivec2& outMin, glm::ivec2& outMax) const {
		bool any = false;
		glm::ivec2 mn{ std::numeric_limits<int>::max(),  std::numeric_limits<int>::max() };
		glm::ivec2 mx{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };

		for (const auto& [chunkCoord, chunk] : Chunks) {
			for (int wi = 0; wi < 4; ++wi) {
				std::uint64_t word = chunk->Occupancy[wi];
				while (word != 0) {
					const int bit = std::countr_zero(word);
					word &= word - 1;
					const int local = wi * 64 + bit;
					const int lx = local & (kChunkSize - 1);
					const int ly = local / kChunkSize;
					const glm::ivec2 cell{
						chunkCoord.x * kChunkSize + lx,
						chunkCoord.y * kChunkSize + ly,
					};
					mn.x = std::min(mn.x, cell.x);
					mn.y = std::min(mn.y, cell.y);
					mx.x = std::max(mx.x, cell.x);
					mx.y = std::max(mx.y, cell.y);
					any = true;
				}
			}
		}

		if (any) {
			outMin = mn;
			outMax = mx;
		}
		return any;
	}

} // namespace Index::Tilemap2D
