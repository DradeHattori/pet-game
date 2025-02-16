#include "request_handler.h"

namespace http_handler {

    std::string GenerateAuthToken() {

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);

        std::string token;
        for (int i = 0; i < 32; ++i) {
            token += "0123456789abcdef"[dis(gen)];
        }

        // std::string token = "7a76d6b546f6feae2597ca085600e5af"; // для тестов
        return token;
    }



    std::string RequestHandler::SerializeMap(const model::MapSharedPtr map, boost::json::array mapsinfo) {
        auto map_obj = CreateMapObject(map, mapsinfo);
        return boost::json::serialize(map_obj);
    }

    boost::json::object RequestHandler::CreateMapObject(const model::MapSharedPtr map, const boost::json::array& mapsinfo) {
        boost::json::object map_obj;

        map_obj["id"] = *map->GetId();
        map_obj["name"] = map->GetName();
        map_obj["roads"] = CreateRoadsArray(map);
        map_obj["buildings"] = CreateBuildingsArray(map);
        map_obj["offices"] = CreateOfficesArray(map);
        map_obj["lootTypes"] = mapsinfo;

        return map_obj;
    }

    boost::json::array RequestHandler::CreateRoadsArray(const model::MapSharedPtr map) {
        boost::json::array roads_array;

        for (const auto& road : map->GetRoads()) {
            boost::json::object road_obj;

            road_obj["x0"] = road.GetStart().x;
            road_obj["y0"] = road.GetStart().y;

            if (road.IsHorizontal()) {
                road_obj["x1"] = road.GetEnd().x;
            }
            else {
                road_obj["y1"] = road.GetEnd().y;
            }

            roads_array.push_back(std::move(road_obj));
        }

        return roads_array;
    }

    boost::json::array RequestHandler::CreateBuildingsArray(const model::MapSharedPtr map) {
        boost::json::array buildings_array;

        for (const auto& building : map->GetBuildings()) {
            boost::json::object building_obj;
            building_obj["x"] = building.GetBounds().position.x;
            building_obj["y"] = building.GetBounds().position.y;
            building_obj["w"] = building.GetBounds().size.width;
            building_obj["h"] = building.GetBounds().size.height;
            buildings_array.push_back(std::move(building_obj));
        }

        return buildings_array;
    }

    boost::json::array RequestHandler::CreateOfficesArray(const model::MapSharedPtr map) {
        boost::json::array offices_array;

        for (const auto& office : map->GetOffices()) {
            boost::json::object office_obj;
            office_obj["id"] = *office.GetId();
            office_obj["x"] = office.GetPosition().x;
            office_obj["y"] = office.GetPosition().y;
            office_obj["offsetX"] = office.GetOffset().dx;
            office_obj["offsetY"] = office.GetOffset().dy;
            offices_array.push_back(std::move(office_obj));
        }

        return offices_array;
    }


    http::response<http::string_body> RequestHandler::ErrorResponseApi(http::status status, const std::string& message)
    {
        http::response<http::string_body> res{ status, 11 };
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        boost::json::object error;
        error["message"] = message;
        error["code"] = (status == http::status::not_found) ? "mapNotFound" : "badRequest";
        res.body() = boost::json::serialize(error);
        res.prepare_payload();
        return res;
    }

    http::response<http::string_body> RequestHandler::ErrorResponseStatic(http::status status, const std::string& message) {
        http::response<http::string_body> res{ status, 11 };
        res.set(http::field::content_type, "text/plain");
        res.set(http::field::cache_control, "no-cache");
        res.body() = message;
        res.prepare_payload();
        return res;
    }

    http::response<http::string_body> RequestHandler::ErrorResponseMethodNotAllowed(http::status status, const std::string& message)
    {
        http::response<http::string_body> res{ status, 11 };
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        boost::json::object error;
        error["message"] = message;
        error["code"] = "invalidMethod";
        res.set(http::field::allow, "GET, HEAD, POST");
        res.body() = boost::json::serialize(error);
        res.prepare_payload();
        return res;
    }

    http::response<http::string_body> RequestHandler::NotAllowedExceptPOST(http::status status, const std::string& message)
    {
        http::response<http::string_body> res{ status, 11 };
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        boost::json::object error;
        error["message"] = message;
        error["code"] = "invalidMethod";
        res.set(http::field::allow, "POST");
        res.body() = boost::json::serialize(error);
        res.prepare_payload();
        return res;
    }

    http::response<http::string_body> RequestHandler::NotAllowedExceptGET_HEAD(http::status status, const std::string& message)
    {
        http::response<http::string_body> res{ status, 11 };
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        boost::json::object error;
        error["message"] = message;
        error["code"] = "invalidMethod";
        res.set(http::field::allow, "GET, HEAD");
        res.body() = boost::json::serialize(error);
        res.prepare_payload();
        return res;
    }


    http::response<http::string_body> RequestHandler::InvalidToken(http::status status, const std::string& message) {
        http::response<http::string_body> res{ status, 11 };
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        boost::json::object error;
        error["message"] = message;
        error["code"] = "invalidToken";
        res.body() = boost::json::serialize(error);
        res.prepare_payload();
        return res;
    }

    http::response<http::string_body> RequestHandler::UnknownToken(http::status status, const std::string& message) {
        http::response<http::string_body> res{ status, 11 };
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        boost::json::object error;
        error["message"] = message;
        error["code"] = "unknownToken";
        res.body() = boost::json::serialize(error);
        res.prepare_payload();
        return res;
    }


    http::response<http::string_body> RequestHandler::ErrorResponseJsonInvalidArgument(http::status status, const std::string& message) {
        http::response<http::string_body> res{ status, 11 };
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        boost::json::object error;
        error["message"] = message;
        error["code"] = "invalidArgument";
        res.body() = boost::json::serialize(error);
        res.prepare_payload();
        return res;
    }

}