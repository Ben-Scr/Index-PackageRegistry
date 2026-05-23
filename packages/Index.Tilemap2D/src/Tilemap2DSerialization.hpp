#pragma once

// Explicit JSON serialization for Tilemap2DComponent.
//
// Why this lives in the package, not in SceneSerializer
// -----------------------------------------------------
// Index's `SceneSerializer` currently hardcodes every component type it
// knows how to round-trip — there's no extension point for a package to
// register a per-component serializer keyed off `ComponentInfo::serializedName`.
// Until the engine grows that hook, scene save/load WILL NOT carry a
// Tilemap2DComponent. The component stays alive in the registry but its
// data is dropped on save and absent on load.
//
// This header provides the standalone JSON round-trip so:
//   • The C# side can drive `SaveToFile` / `LoadFromFile` independently.
//   • An engine-side scene serializer extension can call these directly
//     once the dispatch hook lands — the call site will be one line per
//     direction.
//
// Layout is documented in `Tilemap2DComponent.hpp`. Hand-written; never
// reflection-based.

#include "IndexTilemap2DApi.hpp"

#include "Serialization/Json.hpp"

namespace Index::Tilemap2D {

	struct Tilemap2DComponent;

	// Serialize a tilemap component to a JSON value matching the layout
	// documented in Tilemap2DComponent.hpp. Returns the resulting object
	// (caller decides where to embed it).
	INDEX_PACKAGE_API Json::Value SerializeTilemap2D(const Tilemap2DComponent& tilemap);

	// Deserialize a JSON value (matching the layout above) into the
	// component, REPLACING any existing chunks. Returns true on success.
	// Tolerant of older versions: missing `tilesetTexture`, missing/short
	// `occupancy` strings, and missing tile fields all default to zero.
	INDEX_PACKAGE_API bool DeserializeTilemap2D(const Json::Value& value, Tilemap2DComponent& outTilemap);

	// Convenience round-trips against a file path. Use these when driving
	// save/load from the C# side; the editor inspector also uses them for
	// the planned import/export buttons.
	INDEX_PACKAGE_API bool SaveTilemap2DToFile(const Tilemap2DComponent& tilemap, const std::string& path);
	INDEX_PACKAGE_API bool LoadTilemap2DFromFile(const std::string& path, Tilemap2DComponent& outTilemap);

} // namespace Index::Tilemap2D
