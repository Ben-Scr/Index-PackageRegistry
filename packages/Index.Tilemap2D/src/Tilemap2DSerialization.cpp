#include "Tilemap2DSerialization.hpp"
#include "Tilemap2DComponent.hpp"

#include "Serialization/File.hpp"

#include <array>
#include <cstdio>
#include <cstring>

namespace Index::Tilemap2D {

	namespace {
		// Hex helpers — used to round-trip the 256-bit chunk occupancy bitmap
		// as a fixed-width 64-character lower-case hex string.
		void OccupancyToHex(const std::array<std::uint64_t, 4>& occ, char (&out)[65]) {
			std::snprintf(out, sizeof(out), "%016llx%016llx%016llx%016llx",
				static_cast<unsigned long long>(occ[0]),
				static_cast<unsigned long long>(occ[1]),
				static_cast<unsigned long long>(occ[2]),
				static_cast<unsigned long long>(occ[3]));
		}

		bool HexToOccupancy(std::string_view hex, std::array<std::uint64_t, 4>& out) {
			out = {};
			if (hex.size() != 64) return false;
			char buf[17] = {};
			for (int word = 0; word < 4; ++word) {
				std::memcpy(buf, hex.data() + word * 16, 16);
				buf[16] = '\0';
				char* end = nullptr;
				const unsigned long long v = std::strtoull(buf, &end, 16);
				if (end != buf + 16) return false;
				out[word] = static_cast<std::uint64_t>(v);
			}
			return true;
		}

		// GUIDs round-trip as decimal strings to match the rest of
		// SceneSerializer (every other GUID field uses to_string of
		// uint64_t).
		std::string GuidToString(std::uint64_t guid) {
			return std::to_string(guid);
		}

		std::uint64_t GuidFromString(std::string_view s) {
			if (s.empty()) return 0;
			char* end = nullptr;
			const auto buf = std::string(s);
			const unsigned long long v = std::strtoull(buf.c_str(), &end, 10);
			return static_cast<std::uint64_t>(v);
		}
	}

	Json::Value SerializeTilemap2D(const Tilemap2DComponent& tilemap) {
		Json::Value root = Json::Value::MakeObject();

		if (static_cast<uint64_t>(tilemap.TilesetTexture) != 0) {
			root.AddMember("tilesetTexture", Json::Value(GuidToString(static_cast<uint64_t>(tilemap.TilesetTexture))));
		}

		// Tilemap-wide color tint. Always serialised so a saved-and-reloaded
		// scene preserves any non-default tint set by the user; cheap (4
		// floats) and the default round-trips harmlessly.
		root.AddMember("r", Json::Value(tilemap.Color.r));
		root.AddMember("g", Json::Value(tilemap.Color.g));
		root.AddMember("b", Json::Value(tilemap.Color.b));
		root.AddMember("a", Json::Value(tilemap.Color.a));

		// Geometry knobs. Always serialised — total payload is six floats.
		// Old scenes without these fields fall back to the defaults during
		// deserialise (1×1 cell, no gap, anchor at 0) so existing tilemaps
		// load identically.
		root.AddMember("anchorX", Json::Value(tilemap.TileAnchor.x));
		root.AddMember("anchorY", Json::Value(tilemap.TileAnchor.y));
		root.AddMember("cellW", Json::Value(tilemap.CellSize.x));
		root.AddMember("cellH", Json::Value(tilemap.CellSize.y));
		root.AddMember("gapX", Json::Value(tilemap.CellSpace.x));
		root.AddMember("gapY", Json::Value(tilemap.CellSpace.y));

		Json::Value chunks = Json::Value::MakeArray();
		for (const auto& [chunkCoord, chunk] : tilemap.Chunks) {
			Json::Value chunkVal = Json::Value::MakeObject();
			chunkVal.AddMember("cx", Json::Value(chunkCoord.x));
			chunkVal.AddMember("cy", Json::Value(chunkCoord.y));

			char occHex[65];
			OccupancyToHex(chunk->Occupancy, occHex);
			chunkVal.AddMember("occupancy", Json::Value(std::string(occHex, 64)));

			// Tiles array: dense, always kTilesPerChunk entries. Even unset
			// cells get a record (texture=0, color=0,0,0,0); occupancy is
			// the source of truth so this just keeps the JSON shape stable.
			Json::Value tilesArr = Json::Value::MakeArray();
			for (const Tile& tile : chunk->Tiles) {
				Json::Value tileVal = Json::Value::MakeObject();
				tileVal.AddMember("tex", Json::Value(GuidToString(tile.Texture)));
				tileVal.AddMember("r", Json::Value(tile.TileColor.r));
				tileVal.AddMember("g", Json::Value(tile.TileColor.g));
				tileVal.AddMember("b", Json::Value(tile.TileColor.b));
				tileVal.AddMember("a", Json::Value(tile.TileColor.a));
				tilesArr.Append(std::move(tileVal));
			}
			chunkVal.AddMember("tiles", std::move(tilesArr));

			chunks.Append(std::move(chunkVal));
		}
		root.AddMember("chunks", std::move(chunks));

		return root;
	}

	bool DeserializeTilemap2D(const Json::Value& value, Tilemap2DComponent& outTilemap) {
		if (!value.IsObject()) return false;

		outTilemap.Chunks.clear();
		outTilemap.TilesetTexture = AssetGUID(0);
		// Color defaults to Index::Color::White() if no JSON members present
		// — preserves the "missing color in JSON = no tint" contract for
		// scenes saved before this field was added.
		outTilemap.Color = Index::Color::White();
		// Geometry defaults match the historic 1-unit-per-tile behaviour so
		// scenes saved before these fields existed still render identically.
		outTilemap.TileAnchor = glm::vec2(0.0f);
		outTilemap.CellSize   = glm::vec2(1.0f);
		outTilemap.CellSpace  = glm::vec2(0.0f);

		if (const Json::Value* tileset = value.FindMember("tilesetTexture")) {
			outTilemap.TilesetTexture = AssetGUID(GuidFromString(tileset->AsStringOr()));
		}

		if (const Json::Value* m = value.FindMember("r")) outTilemap.Color.r = static_cast<float>(m->AsDoubleOr(1.0));
		if (const Json::Value* m = value.FindMember("g")) outTilemap.Color.g = static_cast<float>(m->AsDoubleOr(1.0));
		if (const Json::Value* m = value.FindMember("b")) outTilemap.Color.b = static_cast<float>(m->AsDoubleOr(1.0));
		if (const Json::Value* m = value.FindMember("a")) outTilemap.Color.a = static_cast<float>(m->AsDoubleOr(1.0));

		if (const Json::Value* m = value.FindMember("anchorX")) outTilemap.TileAnchor.x = static_cast<float>(m->AsDoubleOr(0.0));
		if (const Json::Value* m = value.FindMember("anchorY")) outTilemap.TileAnchor.y = static_cast<float>(m->AsDoubleOr(0.0));
		if (const Json::Value* m = value.FindMember("cellW"))   outTilemap.CellSize.x   = static_cast<float>(m->AsDoubleOr(1.0));
		if (const Json::Value* m = value.FindMember("cellH"))   outTilemap.CellSize.y   = static_cast<float>(m->AsDoubleOr(1.0));
		if (const Json::Value* m = value.FindMember("gapX"))    outTilemap.CellSpace.x  = static_cast<float>(m->AsDoubleOr(0.0));
		if (const Json::Value* m = value.FindMember("gapY"))    outTilemap.CellSpace.y  = static_cast<float>(m->AsDoubleOr(0.0));

		const Json::Value* chunks = value.FindMember("chunks");
		if (!chunks || !chunks->IsArray()) {
			return true; // empty tilemap is valid
		}

		for (const Json::Value& chunkVal : chunks->GetArray()) {
			if (!chunkVal.IsObject()) continue;

			const int cx = chunkVal.FindMember("cx") ? chunkVal.FindMember("cx")->AsIntOr(0) : 0;
			const int cy = chunkVal.FindMember("cy") ? chunkVal.FindMember("cy")->AsIntOr(0) : 0;

			auto chunk = std::make_unique<Chunk>();

			if (const Json::Value* occ = chunkVal.FindMember("occupancy")) {
				const std::string occStr = occ->AsStringOr();
				HexToOccupancy(occStr, chunk->Occupancy);
			}

			if (const Json::Value* tiles = chunkVal.FindMember("tiles"); tiles && tiles->IsArray()) {
				const auto& arr = tiles->GetArray();
				const std::size_t copyCount = std::min<std::size_t>(arr.size(), kTilesPerChunk);
				for (std::size_t i = 0; i < copyCount; ++i) {
					const Json::Value& t = arr[i];
					if (!t.IsObject()) continue;
					Tile& dst = chunk->Tiles[i];
					if (const auto* m = t.FindMember("tex")) dst.Texture = GuidFromString(m->AsStringOr());
					if (const auto* m = t.FindMember("r"))   dst.TileColor.r = static_cast<float>(m->AsDoubleOr(0.0));
					if (const auto* m = t.FindMember("g"))   dst.TileColor.g = static_cast<float>(m->AsDoubleOr(0.0));
					if (const auto* m = t.FindMember("b"))   dst.TileColor.b = static_cast<float>(m->AsDoubleOr(0.0));
					if (const auto* m = t.FindMember("a"))   dst.TileColor.a = static_cast<float>(m->AsDoubleOr(0.0));
				}
			}

			outTilemap.Chunks.emplace(glm::ivec2{ cx, cy }, std::move(chunk));
		}

		return true;
	}

	bool SaveTilemap2DToFile(const Tilemap2DComponent& tilemap, const std::string& path) {
		// File::WriteAllText is void-returning today (throws on failure
		// inside its impl). We just call it through; if a future error path
		// is added the bool plumbs through with no other change.
		const Json::Value root = SerializeTilemap2D(tilemap);
		File::WriteAllText(path, Json::Stringify(root, true));
		return true;
	}

	bool LoadTilemap2DFromFile(const std::string& path, Tilemap2DComponent& outTilemap) {
		const std::string text = File::ReadAllText(path);
		if (text.empty()) return false;
		Json::Value root;
		std::string err;
		if (!Json::TryParse(text, root, &err)) return false;
		return DeserializeTilemap2D(root, outTilemap);
	}

} // namespace Index::Tilemap2D
