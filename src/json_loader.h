#pragma once

#include <filesystem>

#include "model.h"
#include "frontend_info.h"

namespace json_loader {

	model::Game LoadGame(const std::filesystem::path& json_path);
	rawinfo::FrontendInfo LoadRawInfo(const std::filesystem::path& json_path);
	void ConfigureGameDefaults(model::Game& game, const boost::json::value& json_value);
	void ConfigureLootGenerator(model::Game& game, const boost::json::value& json_value);
	void LoadMaps(model::Game& game, const boost::json::value& json_value);
	void ConfigureLootTypes(model::Map& map, const boost::json::object& map_obj);
	void ConfigureMapDefaults(model::Map& map, const boost::json::object& map_obj, const model::Game& game);
	void LoadRoads(model::Map& map, const boost::json::object& map_obj);
	void LoadBuildings(model::Map& map, const boost::json::object& map_obj);
	void LoadOffices(model::Map& map, const boost::json::object& map_obj);
	rawinfo::FrontendInfo LoadRawInfo(const std::filesystem::path& json_path);


}  // namespace json_loader
