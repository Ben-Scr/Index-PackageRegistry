#pragma once

// TilemapRenderSystem — walks every Tilemap2DComponent in the scene and
// prepares per-tile draw data for the active 2D render path.
//
// Rendering integration (current state)
// -------------------------------------
// `Renderer2D` does not yet expose a "submit external instances" hook.
// `Renderer2D::RenderScene()` internally collects sprite instances by
// `view<Transform2DComponent, SpriteRendererComponent>` and immediately
// submits them — there is no extension point for a package to inject
// additional instances into the same draw batch.
//
// Until the planned render-graph lands, this system runs in `OnPreRender`
// and walks the scene's tilemaps, building the per-tile world transform +
// texture handle list. The actual GPU submission is a single, clearly-
// scoped block at the bottom of `Update()` marked `TODO(render-graph)`.
// When the engine grows either:
//   • a `Renderer2D::SubmitInstances(std::span<const Instance44>)` API,
//   • a render-graph "tilemap pass" entry point, or
//   • per-frame draw-list events,
// the migration is mechanical: replace the TODO with the engine call and
// delete nothing else. The collection / culling / per-tile transform code
// is the part that will dominate render-graph integration anyway.
//
// Statistics (`m_LastTilesEmitted`, `m_LastChunksWalked`) are exposed so
// the editor inspector can show what the system saw last frame even while
// the GL submission is stubbed.

#include "IndexTilemap2DApi.hpp"

#include "Scene/ISystem.hpp"

#include <cstddef>

namespace Index { class Scene; }

namespace Index::Tilemap2D {

	class INDEX_PACKAGE_API TilemapRenderSystem : public Index::ISystem {
	public:
		void OnPreRender(Index::Scene& scene) override;

		std::size_t GetTilesEmittedLastFrame() const noexcept { return m_LastTilesEmitted; }
		std::size_t GetChunksWalkedLastFrame() const noexcept { return m_LastChunksWalked; }

	private:
		std::size_t m_LastTilesEmitted = 0;
		std::size_t m_LastChunksWalked = 0;
	};

} // namespace Index::Tilemap2D
