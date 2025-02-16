#include "json_loader.h"

#include <boost/json.hpp>
#include <fstream>

namespace json_loader
{
    model::Game LoadGame(const std::filesystem::path& json_path_) {
        std::ifstream file(json_path_);
        if (!file.is_open()) {
            throw std::runtime_error("Error opening file: " + json_path_.string());
        }

        boost::json::value json_value;
        std::ostringstream oss;
        oss << file.rdbuf();
        file.close();
        json_value = boost::json::parse(oss.str());

        model::Game game;
        ConfigureGameDefaults(game, json_value);
        ConfigureLootGenerator(game, json_value);
        LoadMaps(game, json_value);

        return game;
    }

    void ConfigureGameDefaults(model::Game& game, const boost::json::value& json_value) {
        double default_dogs_speed = 1.0;
        if (json_value.is_object() && json_value.as_object().contains("defaultDogSpeed")) {
            default_dogs_speed = json_value.as_object().at("defaultDogSpeed").as_double();
        }
        game.SetDefaultDogsSpeed(default_dogs_speed);

        uint64_t default_dogs_lootbag_capacity = 3;
        if (json_value.is_object() && json_value.as_object().contains("defaultBagCapacity")) {
            default_dogs_lootbag_capacity = json_value.as_object().at("defaultBagCapacity").as_uint64();
        }
        game.SetDefaultLootbagCapacity(default_dogs_lootbag_capacity);

        double default_afk_time = 0.0;
        if (json_value.is_object() && json_value.as_object().contains("dogRetirementTime")) {
            default_afk_time = json_value.as_object().at("dogRetirementTime").as_double();
        }
        game.SetDefaultAFKtime(default_afk_time);
    }

    void ConfigureLootGenerator(model::Game& game, const boost::json::value& json_value) {
        double loot_spawning_period = 0.0;
        double loot_spawning_probability = 0.0;
        if (json_value.is_object() && json_value.as_object().contains("lootGeneratorConfig")) {
            const auto& config = json_value.as_object().at("lootGeneratorConfig");
            try {
                loot_spawning_period = config.as_object().at("period").as_double();
                loot_spawning_probability = config.as_object().at("probability").as_double();
            }
            catch (const std::exception& ex) {
                throw std::runtime_error("Error setting loot parameters");
            }
        }
        game.SetLootConfig({ loot_spawning_period, loot_spawning_probability });
    }

    void LoadMaps(model::Game& game, const boost::json::value& json_value) {
        const auto& maps_array = json_value.at("maps").as_array();
        for (const auto& map_value : maps_array) {
            const auto& map_obj = map_value.as_object();
            model::Map map(
                model::Map::Id(map_obj.at("id").as_string().c_str()),
                map_obj.at("name").as_string().c_str()
            );

            ConfigureLootTypes(map, map_obj);
            ConfigureMapDefaults(map, map_obj, game);
            LoadRoads(map, map_obj);
            LoadBuildings(map, map_obj);
            LoadOffices(map, map_obj);

            game.AddMap(std::move(map));
        }
    }

    void ConfigureLootTypes(model::Map& map, const boost::json::object& map_obj) {
        int type_id_counter = 0;
        for (const auto& loot_value : map_obj.at("lootTypes").as_array()) {
            const auto& value_obj = loot_value.as_object();
            map.AddLootType(type_id_counter++, value_obj.at("value").as_int64());
        }
        map.SetLootTypesCount(type_id_counter);
    }

    void ConfigureMapDefaults(model::Map& map, const boost::json::object& map_obj, const model::Game& game) {
        if (map_obj.contains("dogSpeed")) {
            map.SetDefaultDogsSpeed(map_obj.at("dogSpeed").as_double());
        }
        else {
            map.SetDefaultDogsSpeed(game.GetDefaultDogSpeed());
        }

        if (map_obj.contains("bagCapacity")) {
            map.SetDefaultBagCapacity(map_obj.at("bagCapacity").as_uint64());
        }
        else {
            map.SetDefaultBagCapacity(game.GetDefaultLootBagCapacity());
        }
    }

    void LoadRoads(model::Map& map, const boost::json::object& map_obj) {
        for (const auto& road_value : map_obj.at("roads").as_array()) {
            const auto& road_obj = road_value.as_object();
            model::Point start{
                model::Coord(road_obj.at("x0").as_int64()),
                model::Coord(road_obj.at("y0").as_int64())
            };
            if (road_obj.contains("x1")) {
                map.AddRoad(model::Road(
                    model::Road::HORIZONTAL,
                    start,
                    model::Coord(road_obj.at("x1").as_int64())
                ));
            }
            else {
                map.AddRoad(model::Road(
                    model::Road::VERTICAL,
                    start,
                    model::Coord(road_obj.at("y1").as_int64())
                ));
            }
        }
    }

    void LoadBuildings(model::Map& map, const boost::json::object& map_obj) {
        for (const auto& building_value : map_obj.at("buildings").as_array()) {
            const auto& building_obj = building_value.as_object();
            model::Point position{
                model::Coord(building_obj.at("x").as_int64()),
                model::Coord(building_obj.at("y").as_int64())
            };
            model::Size size{
                model::Dimension(building_obj.at("w").as_int64()),
                model::Dimension(building_obj.at("h").as_int64())
            };
            map.AddBuilding(model::Building(model::Rectangle(position, size)));
        }
    }

    void LoadOffices(model::Map& map, const boost::json::object& map_obj) {
        for (const auto& office_value : map_obj.at("offices").as_array()) {
            const auto& office_obj = office_value.as_object();
            model::Point position{
                model::Coord(office_obj.at("x").as_int64()),
                model::Coord(office_obj.at("y").as_int64())
            };
            model::Offset offset{
                model::Dimension(office_obj.at("offsetX").as_int64()),
                model::Dimension(office_obj.at("offsetY").as_int64())
            };
            map.AddOffice(model::Office(
                model::Office::Id(office_obj.at("id").as_string().c_str()),
                position,
                offset
            ));
        }
    }

    rawinfo::FrontendInfo LoadRawInfo(const std::filesystem::path& json_path) {
        rawinfo::FrontendInfo raw_information;
        std::ifstream file(json_path);
        if (!file.is_open())
        {
            throw std::runtime_error("Ошибка открытия файла: " + json_path.string());
        }

        boost::json::value json_value;
        std::ostringstream oss;
        oss << file.rdbuf();
        file.close();
        json_value = boost::json::parse(oss.str());

        raw_information.SetRawInfo(json_value.at("maps").as_array());

        return raw_information;
    }

}