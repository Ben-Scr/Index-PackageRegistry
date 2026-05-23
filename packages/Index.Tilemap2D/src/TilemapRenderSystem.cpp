#include "TilemapRenderSystem.hpp"
#include "Tilemap2DComponent.hpp"

#include <atomic>

namespace Index::Tilemap2D {

	// Stats published by the Renderer2D contributor in PackageEntry.cpp.
	// Declared extern here so this TU doesn't have to reach into the
	// contributor file's internals — the contributor writes, the system
	// reads. relaxed atomics: diagnostic counters, no synchronization
	// semantics needed.
	extern std::atomic<std::size_t> g_LastTilesEmitted;
	extern std::atomic<std::size_t> g_LastChunksWalked;

	// OnPreRender used to do its own full walk of every tilemap to count
	// stats — that walk is gone. Rendering and stat collection both happen
	// in the Renderer2D instance contributor (one walk total per scene per
	// frame, not two). Keeping the system class around so existing user code
	// that called `def.AddSystem<TilemapRenderSystem>()` per the README still
	// compiles; it's just a passive accessor for the contributor's stats now.
	//
	// We sync the cached members at the top of every frame so script callers
	// holding a reference to the system see the latest values without having
	// to know about the underlying atomics.
	void TilemapRenderSystem::OnPreRender(Index::Scene&) {
		m_LastTilesEmitted = g_LastTilesEmitted.load(std::memory_order_relaxed);
		m_LastChunksWalked = g_LastChunksWalked.load(std::memory_order_relaxed);
	}

} // namespace Index::Tilemap2D
