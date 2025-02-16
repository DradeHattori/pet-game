#pragma once

#include "http_server.h"
#include "model.h"
#include "logger.h"
#include "frontend_info.h"

#include <boost/json.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <sstream>
#include <iostream>
#include <variant>
#include <random>

namespace http_handler {

    std::string GenerateAuthToken();

    namespace beast = boost::beast;
    namespace fs = std::filesystem;
    namespace net = boost::asio;
    using tcp = net::ip::tcp;
    namespace http = beast::http;

    // Определяем variant для обработки обоих типов ответа
    using Response = std::variant<http::response<http::string_body>, http::response<http::file_body>>;

    template <class SomeRequestHandler>
    class LoggingRequestHandler {
    public:
        explicit LoggingRequestHandler(SomeRequestHandler& decorated) : decorated_(decorated) {}

        template <typename Send>
        void operator()(http::request<http::string_body> req, Send&& send, const tcp::socket& socket) {
            std::string ip = socket.remote_endpoint().address().to_string();
            LogRequestReceived(std::string(req.target()), std::string(req.method_string()), ip);

            // Вызываем декорированного обработчика через strand
            decorated_(std::move(req), [this, ip, send = std::forward<Send>(send)](Response response) mutable {
                std::chrono::system_clock::time_point start_time_stamp = std::chrono::system_clock::now();
                std::chrono::system_clock::time_point end_time_stamp = std::chrono::system_clock::now();
                int response_time_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(end_time_stamp - start_time_stamp).count();

                if (std::holds_alternative<http::response<http::string_body>>(response)) {
                    http::response<http::string_body>& res_string = std::get<http::response<http::string_body>>(response);
                    LogRequestSent(ip, response_time_ms, res_string.result_int(), std::string{ res_string[http::field::content_type] });
                    send(std::forward<http::response<http::string_body>>(res_string));
                }
                else if (std::holds_alternative<http::response<http::file_body>>(response)) {
                    http::response<http::file_body>& res_file = std::get<http::response<http::file_body>>(response);
                    LogRequestSent(ip, response_time_ms, res_file.result_int(), std::string{ res_file[http::field::content_type] });
                    send(std::forward<http::response<http::file_body>>(res_file));
                }
                });
        }

    private:
        SomeRequestHandler& decorated_;
    };

    class RequestHandler {
    public:
        explicit RequestHandler(model::Game& game_, rawinfo::FrontendInfo& frontend_information_, const std::string& root_dir, net::io_context& ioc)
            : game(game_), root_dir_{ root_dir }, frontend_information(frontend_information_), strand_(net::make_strand(ioc)) {}

        RequestHandler(const RequestHandler&) = delete;
        RequestHandler& operator=(const RequestHandler&) = delete;

        template <typename Body, typename Allocator>
        void operator()(http::request<Body, http::basic_fields<Allocator>>&& req, std::function<void(Response)> callback) {
            // Все операции, которые могут привести к состоянию гонки, выполняем через strand
            net::post(strand_, [this, req = std::move(req), callback = std::move(callback)]() mutable {
                Response response = this->HandleRequest(std::move(req));
                if (callback) {
                    callback(std::move(response));
                }
                });
        }

    private:

        std::unordered_map<std::string, std::string> ParseQueryParams(const std::string& target) {
            std::unordered_map<std::string, std::string> params;
            auto pos = target.find('?');
            if (pos == std::string::npos) return params; 

            std::string query_string = target.substr(pos + 1);
            std::istringstream query_stream(query_string);
            std::string param;

            while (std::getline(query_stream, param, '&')) {
                auto eq_pos = param.find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = param.substr(0, eq_pos);
                    std::string value = param.substr(eq_pos + 1);
                    params[key] = value;
                }
            }
            return params;
        }

        template <typename Body, typename Allocator>
        Response HandleRequest(http::request<Body, http::basic_fields<Allocator>>&& req) {
            if (req.target().starts_with("/api")) {
                if (req.target().starts_with("/api/v1/game/join")) {
                    return HandleJoinGame(req);
                }
                if (req.target().starts_with("/api/v1/game/players")) {
                    return HandleGetPlayers(req);
                }
                if (req.target().starts_with("/api/v1/game/state")) {
                    return HandleGetGameState(req);
                }
                if (req.target().starts_with("/api/v1/game/player/action")) {
                    return HandleAction(req);
                }
                if (req.target().starts_with("/api/v1/game/tick")) {
                    return HandleTick(req);
                }
                if (req.target().starts_with("/api/v1/maps")) {
                    if (req.target() == "/api/v1/maps") {
                        return HandleGetMaps();
                    }
                    else if (req.target().starts_with("/api/v1/maps/")) {
                        return HandleGetMap(req);
                    }
                }
                if (req.target().starts_with("/api/v1/game/record")) {
                    return HandleRecord(req);
                }
                else {
                    return ErrorResponseApi(http::status::bad_request, "Bad request");
                }
            }
            else {
                return HandleStaticFileRequest(std::forward<http::request<Body, http::basic_fields<Allocator>>>(req));
            }

            return ErrorResponseApi(http::status::bad_request, "Bad request");
        }



        Response HandleJoinGame(const http::request<http::string_body>& req) {
            try {
                if (req.method() != http::verb::post) {
                    return NotAllowedExceptPOST(http::status::method_not_allowed, "Only POST method is expected");
                }

                if (req[http::field::content_type] != "application/json") {
                    return ErrorResponseApi(http::status::bad_request, "Invalid Content-Type");
                }

                boost::system::error_code ec;
                auto json_body = boost::json::parse(req.body(), ec);
                if (ec && !json_body.is_object()) {
                    return ErrorResponseJsonInvalidArgument(http::status::bad_request, "Join game request parse error");
                }

                auto obj = json_body.as_object();
                if (!obj.contains("mapId")) {
                    return ErrorResponseJsonInvalidArgument(http::status::bad_request, "Invalid map");
                }
                if (!obj.contains("userName")) {
                    return ErrorResponseJsonInvalidArgument(http::status::bad_request, "Invalid name");
                }

                const auto& username = obj["userName"].as_string();
                const auto& map_id = obj["mapId"].as_string();

                // Проверка на пустое имя игрока
                if (username.empty()) {
                    return ErrorResponseJsonInvalidArgument(http::status::bad_request, "Player username cannot be empty");
                }

                // Проверка на пустой или несуществующий map_id
                std::string map_id_str = std::string(map_id);
                const model::MapSharedPtr map = game.FindMap(model::Map::Id(map_id_str));
                if (map_id_str.empty() || !map) {
                    return ErrorResponseApi(http::status::not_found, "Map not found");
                }

                int player_id = map->UpdatePlayerIdCounter(); // Обновляет счетчик игроков на карте и возвращает его
                std::string auth_token = GenerateAuthToken();  // Генерация уникального токена
               
                model::Player new_player(player_id, std::string(username), auth_token);

                // Добавляем игрока на карту, если не существует сессии, создаем её
                game.AddPlayer(new_player, map->GetId());

                // Создаём JSON-ответ с данными игрока
                boost::json::object response_body{
                    {"authToken", auth_token},
                    {"playerId", player_id}
                };

                http::response<http::string_body> res{ http::status::ok, 11 };
                res.set(http::field::content_type, "application/json");
                res.set(http::field::cache_control, "no-cache");
                res.body() = boost::json::serialize(response_body);
                res.prepare_payload();
                return Response{ std::move(res) };
            }
            catch (const std::exception& e) {
                return ErrorResponseApi(http::status::internal_server_error, std::string("Error: ") + e.what());
            }

        }

        Response HandleGetPlayers(const http::request<http::string_body>& req) {

            if (req.method() != http::verb::get && req.method() != http::verb::head) {
                return NotAllowedExceptGET_HEAD(http::status::method_not_allowed, "Only GET and HEAD method is expected");
            }

            // Проверяем авторизационный токен
            const auto& auth_header = req[http::field::authorization];
            if (auth_header.empty() || !auth_header.starts_with("Bearer ")) {
                return InvalidToken(http::status::unauthorized, "Authorization header is missing");
            }

            std::string token = std::string(auth_header.substr(7));
            const auto* player = game.FindPlayerByToken(token);
            if (!player) {
                return UnknownToken(http::status::unauthorized, "Player token not found");
            }

            // Получаем сессию, где находится игрок
            auto player_current_session = game.FindGameSession(player->GetSessionId());
            auto dogs_on_map = player_current_session->GetDogs();

            json::array players_array;

            for (const auto dog : dogs_on_map) {
                players_array.push_back(boost::json::object{
                    {"name", dog->GetName()}
                    });
            }

            // Формируем JSON-ответ
            boost::json::object response_body;
            response_body["players"] = std::move(players_array);

            http::response<http::string_body> res{ http::status::ok, req.version() };
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");
            res.body() = boost::json::serialize(response_body);
            res.prepare_payload();

            return Response{ std::move(res) };
        }

        Response HandleGetGameState(const http::request<http::string_body>& req) {

            if (req.method() != http::verb::get && req.method() != http::verb::head) {
                return NotAllowedExceptGET_HEAD(http::status::method_not_allowed, "Only GET and HEAD method is expected");
            }
            // Проверяем авторизационный токен
            const auto& auth_header = req[http::field::authorization];
            if (auth_header.empty() || !auth_header.starts_with("Bearer ")) {
                return InvalidToken(http::status::unauthorized, "Authorization header is missing");
            }
            std::string token = std::string(auth_header.substr(7));
            if (token.size() != 32) {
                return InvalidToken(http::status::unauthorized, "Invalid token");
            }
            const auto* player = game.FindPlayerByToken(token);
            if (!player) {
                return UnknownToken(http::status::unauthorized, "Player token not found");
            }

            // Получаем сессию, где находится игрок
            auto player_current_session = game.FindGameSession(player->GetSessionId());

            // Получаем инфо о собаках
            auto dogs_on_map = player_current_session->GetDogs();

            boost::json::object players_obj;
            for (const auto& dog : dogs_on_map) {
                boost::json::object dog_obj;
                dog_obj["pos"] = boost::json::array{
                    dog->GetPosition().x,
                    dog->GetPosition().y
                };
                dog_obj["speed"] = boost::json::array{
                    dog->GetSpeed().dx,
                    dog->GetSpeed().dy
                };
                dog_obj["dir"] = dog->GetDirectionString();

                boost::json::array dog_lootbag;
                for (const auto& dog_loot : dog->GetLootBag()) {
                    boost::json::object loot_obj;
                    loot_obj[std::to_string(dog_loot->GetId())] = dog_loot->GetType();
                    dog_lootbag.push_back(loot_obj);
                }
                dog_obj["bag"] = dog_lootbag;

                dog_obj["score"] = dog->GetScore();

                players_obj[std::to_string(dog->GetId())] = std::move(dog_obj);

            }

            // Получаем инфо о луте
            auto loot_on_map = player_current_session->GetLoots();

            boost::json::object loots_obj;
            for (const auto& loot : loot_on_map) {
                boost::json::object one_loot_obj;
                one_loot_obj["type"] = boost::json::value(loot->GetType());
                one_loot_obj["pos"] = boost::json::array{
                    loot->GetPos().x,
                    loot->GetPos().y
                };
                loots_obj[std::to_string(loot->GetId())] = std::move(one_loot_obj);
            }

            // Формируем JSON-ответ
            boost::json::object response_body;
            response_body["players"] = std::move(players_obj);
            response_body["lostObjects"] = std::move(loots_obj);

            http::response<http::string_body> res{ http::status::ok, req.version() };
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");
            res.body() = boost::json::serialize(response_body);
            res.prepare_payload();

            return Response{ std::move(res) };
        }

        Response HandleAction(const http::request<http::string_body>& req) {
            if (req.method() != http::verb::post) {
                return NotAllowedExceptPOST(http::status::method_not_allowed, "Only POST method is expected");
            }
            if (req[http::field::content_type] != "application/json") {
                return ErrorResponseApi(http::status::bad_request, "Invalid Content-Type");
            }
            const auto& auth_header = req[http::field::authorization];
            if (auth_header.empty() || !auth_header.starts_with("Bearer ")) {
                return InvalidToken(http::status::unauthorized, "Authorization header is missing");
            }
            std::string token = std::string(auth_header.substr(7));
            if (token.size() != 32) {
                return InvalidToken(http::status::unauthorized, "Invalid token");
            }
            const auto* player = game.FindPlayerByToken(token);
            if (!player) {
                return UnknownToken(http::status::unauthorized, "Player token not found");
            }

            boost::system::error_code ec;
            boost::json::value json_body = boost::json::parse(req.body(), ec);
            if (ec) {
                return ErrorResponseJsonInvalidArgument(http::status::bad_request, "Invalid JSON in request body");
            }

            // Извлекаем поле "move" и получаем направление
            if (!json_body.is_object() || !json_body.as_object().contains("move")) {
                return ErrorResponseJsonInvalidArgument(http::status::bad_request, "Field 'move' is missing");
            }
            std::string direction = boost::json::value_to<std::string>(json_body.as_object()["move"]);

            // Задаем собаке направление
            const auto& current_player_dog = player->GetDog();
            current_player_dog->SetDirection(direction);

            // Создаём JSON-ответ с данными игрока
            boost::json::object response_body{
                {}
            };

            http::response<http::string_body> res{ http::status::ok, 11 };
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");
            res.body() = boost::json::serialize(response_body);
            res.prepare_payload();
            return Response{ std::move(res) };
        }

        Response HandleTick(const http::request<http::string_body>& req) {
            try {
                if (!game.ManualTimeControlMode()) {
                    return ErrorResponseApi(http::status::bad_request, "Invalid endpoint");
                }
                // Проверка метода
                if (req.method() != http::verb::post) {
                    return NotAllowedExceptPOST(http::status::method_not_allowed, "Only POST method is allowed");
                }

                // Проверка заголовка Content-Type
                const auto content_type = req[http::field::content_type];
                if (content_type.find("application/json") != 0) {
                    return ErrorResponseApi(http::status::bad_request, "Invalid Content-Type. Expected 'application/json'");
                }

                // Парсинг JSON
                boost::system::error_code ec;
                boost::json::value json_body = boost::json::parse(req.body(), ec);
                if (ec) {
                    return ErrorResponseJsonInvalidArgument(http::status::bad_request, "Failed to parse JSON: " + ec.message());
                }

                // Проверка структуры JSON
                if (!json_body.is_object() || !json_body.as_object().contains("timeDelta")) {
                    return ErrorResponseJsonInvalidArgument(http::status::bad_request, "Missing field 'timeDelta' in JSON");
                }

                const auto& time_delta = json_body.as_object()["timeDelta"];
                if (!time_delta.is_int64() || time_delta.as_int64() <= 0) {
                    return ErrorResponseJsonInvalidArgument(http::status::bad_request, "'timeDelta' must be a positive integer");
                }

                // Извлечение параметра и выполнение действий
                int64_t tick_time = time_delta.as_int64();
                game.Update(tick_time);

                // Формирование успешного ответа
                boost::json::object response_body{};
                http::response<http::string_body> res{ http::status::ok, req.version() };
                res.set(http::field::cache_control, "no-cache");
                res.set(http::field::content_type, "application/json");
                res.body() = boost::json::serialize(response_body);
                res.prepare_payload();

                return Response{ std::move(res) };
            }
            catch (const std::exception& ex) {
                // Обработка исключений
                return ErrorResponseApi(http::status::internal_server_error, "Server error: " + std::string(ex.what()));
            }
        }


        Response HandleGetMap(const http::request<http::string_body>& req) {
            if (req.method() != http::verb::get && req.method() != http::verb::head) {
                return NotAllowedExceptGET_HEAD(http::status::method_not_allowed, "Only GET and HEAD method is expected");
            }

            const std::string& map_id = std::string(req.target().substr(13));
            if (const model::MapSharedPtr map = game.FindMap(model::Map::Id(map_id))) {
                const auto& mapinfo = SerializeMap(map, frontend_information.GetLootInfo(map_id));

                res_string_body.version(11);
                res_string_body.result(http::status::ok);
                res_string_body.set(http::field::content_type, "application/json");
                res_string_body.set(http::field::cache_control, "no-cache");
                res_string_body.body() = mapinfo;
                res_string_body.prepare_payload();
                return Response{ std::move(res_string_body) };
            }
            return ErrorResponseApi(http::status::not_found, "Map not found");
        }

        Response HandleGetMaps() {
            boost::json::array maps_array;
            for (auto& map : game.GetMaps()) {
                boost::json::object map_obj;
                map_obj["id"] = *map->GetId();
                map_obj["name"] = map->GetName();
                maps_array.push_back(map_obj);
            }

            res_string_body.version(11);
            res_string_body.result(http::status::ok);
            res_string_body.set(http::field::content_type, "application/json");
            res_string_body.set(http::field::cache_control, "no-cache");
            res_string_body.body() = boost::json::serialize(maps_array);
            res_string_body.prepare_payload();
            return Response{ std::move(res_string_body) };
        }

        std::string DecodeURL(const std::string& url) {
            std::ostringstream decoded;
            for (size_t i = 0; i < url.length(); ++i) {
                if (url[i] == '%' && i + 2 < url.length()) {
                    int hex_value;
                    std::istringstream(url.substr(i + 1, 2)) >> std::hex >> hex_value;
                    decoded << static_cast<char>(hex_value);
                    i += 2;
                }
                else {
                    decoded << url[i];
                }
            }
            return decoded.str();
        }

        std::string GetMimeType(const std::string& path) {
            static const std::unordered_map<std::string, std::string> mime_types = {
                {".htm", "text/html"}, {".html", "text/html"}, {".css", "text/css"},
                {".txt", "text/plain"}, {".js", "text/javascript"}, {".json", "application/json"},
                {".xml", "application/xml"}, {".png", "image/png"}, {".jpg", "image/jpeg"},
                {".jpeg", "image/jpeg"}, {".jpe", "image/jpeg"}, {".gif", "image/gif"},
                {".bmp", "image/bmp"}, {".ico", "image/vnd.microsoft.icon"}, {".tiff", "image/tiff"},
                {".tif", "image/tiff"}, {".svg", "image/svg+xml"}, {".svgz", "image/svg+xml"},
                {".mp3", "audio/mpeg"}
            };

            std::string ext = fs::path(path).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            auto it = mime_types.find(ext);
            if (it != mime_types.end()) {
                return it->second;
            }

            return "application/octet-stream";
        }

        bool IsSubPath(fs::path path, fs::path base) {
            // Приводим оба пути к каноничному виду (без . и ..)
            path = fs::weakly_canonical(path);
            base = fs::weakly_canonical(base);

            // Проверяем, что все компоненты base содержатся внутри path
            for (auto b = base.begin(), p = path.begin(); b != base.end(); ++b, ++p) {
                if (p == path.end() || *p != *b) {
                    return false;
                }
            }
            return true;
        }

        template <typename Body, typename Allocator>
        Response HandleStaticFileRequest(http::request<Body, http::basic_fields<Allocator>>&& req) {
            std::string path = DecodeURL(std::string(req.target()));
            if (path == "/") {
                path = "/index.html";
            }

            path = root_dir_ + path;
            fs::path full_path(path);
            if (!IsSubPath(full_path, path)) {
                return ErrorResponseStatic(http::status::bad_request, "За пределы");
            }

            if (!fs::exists(full_path) || !fs::is_regular_file(full_path)) {
                return ErrorResponseStatic(http::status::not_found, "File Not Found");
            }

            http::file_body::value_type file;
            boost::system::error_code ec;


            file.open(full_path.string().c_str(), boost::beast::file_mode::read, ec);


            if (ec) {
                return ErrorResponseStatic(http::status::internal_server_error, "Failed to open file");
            }

            res_file.version(11);
            res_file.result(http::status::ok);
            res_file.set(http::field::content_type, GetMimeType(full_path.string()));
            res_file.body() = std::move(file);
            res_file.prepare_payload();
            return Response{ std::move(res_file) };
        }


        http::response<http::string_body> ErrorResponseApi(http::status status, const std::string& message);
        http::response<http::string_body> ErrorResponseStatic(http::status status, const std::string& message);
        http::response<http::string_body> ErrorResponseJsonInvalidArgument(http::status status, const std::string& message);
        http::response<http::string_body> ErrorResponseMethodNotAllowed(http::status status, const std::string& message);
        http::response<http::string_body> NotAllowedExceptPOST(http::status status, const std::string& message);
        http::response<http::string_body> NotAllowedExceptGET_HEAD(http::status status, const std::string& message);
        http::response<http::string_body> InvalidToken(http::status status, const std::string& message);
        http::response<http::string_body> UnknownToken(http::status status, const std::string& message);

        std::string SerializeMap(const model::MapSharedPtr map, boost::json::array mapsinfo);

        boost::json::object CreateMapObject(const model::MapSharedPtr map, const boost::json::array& mapsinfo);
        boost::json::array CreateRoadsArray(const model::MapSharedPtr map);
        boost::json::array CreateBuildingsArray(const model::MapSharedPtr map);
        boost::json::array CreateOfficesArray(const model::MapSharedPtr map);


        net::strand<net::io_context::executor_type> strand_;
        http::response<http::file_body> res_file;
        http::response<http::string_body> res_string_body;
        model::Game& game;
        rawinfo::FrontendInfo& frontend_information;
        std::string root_dir_;
    };
}
