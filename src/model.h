#pragma once

#include <boost/geometry.hpp>
#include <memory>
#include <random>
#include <unordered_map>
#include <utility>
#include <filesystem>
#include <mutex>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <map>

#include "tagged.h"
#include "loot_generator.h"
#include "collision_detector.h"
#include "postgres.h"

namespace FS = std::filesystem;

namespace model {

    class GameSession;
    using GameSessionSharedPtr = std::shared_ptr<GameSession>;
    class Dog;
    using DogSharedPtr = std::shared_ptr<Dog>;
    using Dogs = std::vector<DogSharedPtr>;
    class Loot;
    using LootSharedPtr = std::shared_ptr<Loot>;
    using Loots = std::vector<LootSharedPtr>;
    class Map;
    using MapSharedPtr = std::shared_ptr<Map>;

    constexpr double ROAD_RADIUS = 0.4;
    constexpr double LOOT_RADIUS = 0.0;
    constexpr double BASE_RADIUS = 0.5;
    

using Dimension = int;
using Coord = Dimension;

struct Point {
    Coord x, y;
};

struct Size {
    Dimension width, height;
};

struct Rectangle {
    Point position;
    Size size;
};

struct Offset {
    Dimension dx, dy;
};

enum DIRECTION {
    NORTH,
    SOUTH,
    WEST,
    EAST,
    NONE
};

struct MapPoint {
    double x, y;
    MapPoint() = default;
    MapPoint(double x_, double y_) :x(x_), y(y_) {};
    MapPoint(Point p) :x(p.x), y(p.y) {};
    bool operator==(const MapPoint& rhs) const { return ((x == rhs.x) && (y == rhs.y)); };
};

struct MapLine {
    MapPoint first, second;
    MapLine() = default;
    MapLine(MapPoint first_, MapPoint second_) :first(first_), second(second_) {};
    MapLine(double x1, double y1, double x2, double y2) : first(MapPoint{ x1,y1 }), second(MapPoint{ x2,y2 }) {};
};

struct MapSpeed {
    double dx, dy;
    bool operator==(const MapSpeed& rhs) const { return ((dx == rhs.dx) && (dy == rhs.dy)); };
    bool operator!=(const MapSpeed& rhs) const { return ((dx != rhs.dx) || (dy != rhs.dy)); };
};

class Road {
    struct HorizontalTag {
        explicit HorizontalTag() = default;
    };

    struct VerticalTag {
        explicit VerticalTag() = default;
    };

public:
    constexpr static HorizontalTag HORIZONTAL{};
    constexpr static VerticalTag VERTICAL{};

    Road(HorizontalTag, Point start, Coord end_x) noexcept
        : start_{start}
        , end_{end_x, start.y} {
    }

    Road(VerticalTag, Point start, Coord end_y) noexcept
        : start_{start}
        , end_{start.x, end_y} {
    }

    bool IsHorizontal() const noexcept {
        return start_.y == end_.y;
    }

    bool IsVertical() const noexcept {
        return start_.x == end_.x;
    }

    Point GetStart() const noexcept {
        return start_;
    }

    Point GetEnd() const noexcept {
        return end_;
    }

private:
    Point start_;
    Point end_;
};

class Building {
public:
    explicit Building(Rectangle bounds) noexcept
        : bounds_{bounds} {
    }

    const Rectangle& GetBounds() const noexcept {
        return bounds_;
    }

private:
    Rectangle bounds_;
};

class Office {
public:
    using Id = util::Tagged<std::string, Office>;

    Office(Id id, Point position, Offset offset) noexcept
        : id_{std::move(id)}
        , position_{position}
        , offset_{offset} {
    }

    const Id& GetId() const noexcept {
        return id_;
    }

    Point GetPosition() const noexcept {
        return position_;
    }

    Offset GetOffset() const noexcept {
        return offset_;
    }

    double GetWidht() const {
        return width_;
    }

private:
    Id id_;
    Point position_;
    Offset offset_;
    double width_ = 0.5;
};

class Map {
public:
    using Id = util::Tagged<std::string, Map>;
    using Roads = std::vector<Road>;
    using Buildings = std::vector<Building>;
    using Offices = std::vector<Office>;

    Map(Id id, std::string name) noexcept
        : id_(std::move(id))
        , name_(std::move(name)) {
    }

    const Id& GetId() const noexcept { return id_; }

    const std::string& GetName() const noexcept { return name_; }

    const Buildings& GetBuildings() const noexcept { return buildings_; }

    const Roads& GetRoads() const noexcept { return roads_; }

    const Offices& GetOffices() const noexcept { return offices_; }

    const double GetDefaultDogSpeed() const noexcept { return map_default_dogs_speed; }

    const uint64_t GetDefaultBagCapacity() const noexcept { return map_default_bag_capacity; }

    const uint64_t GetLootValueByTypeID(uint64_t id) const noexcept { return loot_id_to_value_.at(id); }

    const int GetLootTypesCount() const { return loot_types_count_; }

    void AddRoad(const Road& road) { roads_.emplace_back(road); }

    void AddBuilding(const Building& building) { buildings_.emplace_back(building); }

    void SetDefaultDogsSpeed(double speed) { map_default_dogs_speed = speed; }

    void AddLootType(uint64_t loot_id, uint64_t value) { loot_id_to_value_[loot_id] = value; }

    void AddOffice(Office office);
    
    void SetLootTypesCount(int loot_types_count) { loot_types_count_ = loot_types_count; }

    void SetDefaultBagCapacity(uint64_t capacity) { map_default_bag_capacity = capacity; }

    size_t GetPlayerIdCounter() const {
        return player_id_counter_;
    }
    void SetPlayerIdCounter(size_t counter) {
        player_id_counter_ = counter;
    }
    size_t UpdatePlayerIdCounter() {
        size_t res = player_id_counter_;
        ++player_id_counter_;
        return res;
    }


private:
    using OfficeIdToIndex = std::unordered_map<Office::Id, size_t, util::TaggedHasher<Office::Id>>;

    Id id_;
    std::string name_;
    Roads roads_;
    Buildings buildings_;
    double map_default_dogs_speed = 1.0;
    uint64_t map_default_bag_capacity = 3;
    OfficeIdToIndex warehouse_id_to_index_;
    Offices offices_;
    int loot_types_count_;
    std::unordered_map<uint64_t, uint64_t> loot_id_to_value_;
    size_t player_id_counter_ = 0;
};

class Player {
public:
    Player(int id, const std::string& name, const std::string& token)
        : playerId(id), userName(name), authToken(token) {}

    int GetId() const { return playerId; }
    const std::string& GetName() const { return userName; }
    const std::string& GetAuthToken() const { return authToken; }
    void ChangeSession(uint64_t id) { current_session_id_ = id; }
    uint64_t GetSessionId() const { return current_session_id_; }
    void SetDog(std::shared_ptr<Dog> dog) { player_dog_ = std::move(dog); }
    std::shared_ptr<Dog> GetDog() const { return player_dog_; }


private:
    int playerId;
    std::string userName;
    std::string authToken;
    uint64_t current_session_id_ = 0;
    std::shared_ptr<Dog> player_dog_;
};

class Loot {
public:
    Loot() = default;
    Loot(int loot_type, int value, MapPoint position) : loot_type_(loot_type), position_(position), id_(loot_counter++), value_(value) {}
    Loot(int loot_type, int value) : loot_type_(loot_type), value_(value) {}
    std::uint64_t GetId() const noexcept { return id_; }
    void SetId(uint64_t id) { id_ = id; }
    std::string GetIdStr() const noexcept { return std::to_string(id_); }
    int GetType() const noexcept { return loot_type_; }
    MapPoint GetPos() const noexcept { return position_; }
    int GetValue() const noexcept { return value_; }
    void SetValue(int value) { value_ = value; }
    void UpdateLootCounter() { if (id_ > loot_counter) { loot_counter = id_; } }
    void SetType(int type) { loot_type_ = type; }
    void SetPos(MapPoint position) { position_ = position; }

private:
    int loot_type_;
    MapPoint position_;
    std::uint64_t id_;
    int value_;
    static uint64_t loot_counter;
};

class Dog {
public:
    Dog() {
        id_ = dog_counter++;
        name_ = "Dog_";
        name_.append(std::to_string(id_));
        pos_.x = 0;
        pos_.y = 0;
        speed_.dx = 0;
        speed_.dy = 0;
        dir_ = NORTH;
    }
    Dog(std::string name) {
        id_ = dog_counter++;
        name_ = name;
        name_.append("_" + std::to_string(id_));
        pos_.x = 0;
        pos_.y = 0;
        speed_.dx = 0;
        speed_.dy = 0;
        dir_ = NORTH;
    }
    std::string GetName() const noexcept {
        return name_;
    }

    std::uint64_t GetId() const {
        return id_;
    }

    const MapPoint& GetPosition() const {
        return pos_;
    }
    const MapPoint& GetPreviousPosition() const {
        return previous_pos_;
    }

    const MapSpeed& GetSpeed() const {
        return speed_;
    }

    std::string GetDirectionString() const {
        if (dir_ == DIRECTION::WEST) {
            return "L";
        }
        if (dir_ == DIRECTION::EAST) {
            return "R";
        }
        if (dir_ == DIRECTION::NORTH) {
            return "U";
        }
        if (dir_ == DIRECTION::SOUTH) {
            return "D";
        }
        return "";
    }

    void SetName(const std::string& name) {
        name_ = name;
    }
    
    void SetId(const std::uint64_t& id) {
        id_ = id;
    }
    
    void SetPos(MapPoint point) {
        pos_.x = std::round(point.x * 100.0) / 100.0;
        pos_.y = std::round(point.y * 100.0) / 100.0;
    }
    
    void SetPreviousPos(MapPoint point) {
        previous_pos_.x = std::round(point.x * 100.0) / 100.0;
        previous_pos_.y = std::round(point.y * 100.0) / 100.0;
    }

    void SetDirection(const std::string& direction) {
        if (direction == "L") {
            dir_ = DIRECTION::WEST;
            speed_ = { -movement_speed_, 0 };
        }
        else if (direction == "R") {
            dir_ = DIRECTION::EAST;
            speed_ = { movement_speed_, 0 };
        }
        else if (direction == "U") {
            dir_ = DIRECTION::NORTH;
            speed_ = { 0, -movement_speed_ };
        }
        else if (direction == "D") {
            dir_ = DIRECTION::SOUTH;
            speed_ = { 0, movement_speed_ };
        }
        else {
            speed_ = { 0, 0 };
        }
    }

    void SetMovementSpeed(double x) { movement_speed_ = x; }
    
    void SetBagCapacity(uint64_t x) { lootbag_capacity_ = x; }

    void SetWidth(double x) { width_ = x; }

    void UpdateDogCounter() {
        if (id_ > dog_counter) {
            dog_counter = id_;
        }
    }

    void Move(double time, const Map::Roads& roads) {

        previous_pos_ = pos_;

        double already_traveled = 0.0;

        bool at_bound = false;

        for (const auto& road : roads) {
            if (road.IsHorizontal()) {
                double road_start_x = std::min(road.GetStart().x, road.GetEnd().x);
                double road_end_x = std::max(road.GetStart().x, road.GetEnd().x);
                double road_y = road.GetStart().y;

                if (std::abs(pos_.y - road_y) <= ROAD_RADIUS && pos_.x >= road_start_x - ROAD_RADIUS && pos_.x <= road_end_x + ROAD_RADIUS) {

                    if (speed_.dx > 0) {
                        double distance = std::min(pos_.x - already_traveled + speed_.dx * time, road_end_x + ROAD_RADIUS);
                        pos_.x = distance;
                        
                        if (distance >= road_end_x + ROAD_RADIUS) {
                            at_bound = true; 
                            speed_.dx = 0;
                        }
                        else { at_bound = false; }
                        break;
                    }
                    else if (speed_.dx < 0) {
                        double distance = std::max(pos_.x - already_traveled + speed_.dx * time, road_start_x - ROAD_RADIUS);
                        pos_.x = distance;

                        if (distance <= road_start_x - ROAD_RADIUS) {
                            at_bound = true; 
                            speed_.dx = 0;
                        }
                        else { at_bound = false; }
                        break;
                    }

                    else if (speed_.dy != 0) {
                        double distance = pos_.y + speed_.dy * time;
                        double start_pos = pos_.y;
                        if (std::abs(distance) <= road_y + ROAD_RADIUS) {
                            pos_.y = distance;
                            at_bound = false; 
                        }
                        else {
                            speed_.dy < 0 ? pos_.y = road_y -ROAD_RADIUS : pos_.y = road_y + ROAD_RADIUS;
                            at_bound = true;
                        }
                        already_traveled = pos_.y - start_pos;
                    }
                }
            }
            else if (road.IsVertical()) {
                double road_start_y = std::min(road.GetStart().y, road.GetEnd().y);
                double road_end_y = std::max(road.GetStart().y, road.GetEnd().y);
                double road_x = road.GetStart().x;

                // Проверка находится ли собака в пределах дороги
                if (std::abs(pos_.x - road_x) <= ROAD_RADIUS && pos_.y >= road_start_y - ROAD_RADIUS && pos_.y <= road_end_y + ROAD_RADIUS) {

                    if (speed_.dy > 0) {
                        double distance = std::min(pos_.y - already_traveled + speed_.dy * time, road_end_y + ROAD_RADIUS);
                        pos_.y = distance;

                        if (distance >= road_end_y + ROAD_RADIUS) {
                            at_bound = true; 
                            speed_.dy = 0;
                        }
                        else { at_bound = false; }
                        break;
                    }
                    else if (speed_.dy < 0) {
                        double distance = std::max(pos_.y - already_traveled + speed_.dy * time, road_start_y - ROAD_RADIUS);
                        pos_.y = distance;
                        if (distance <= road_start_y - ROAD_RADIUS) {
                            at_bound = true;
                            speed_.dy = 0;
                        }
                        else { at_bound = false; }
                        break;
                    }
                   
                    else if (speed_.dx != 0) {
                        double distance = pos_.x + speed_.dx * time;
                        double start_pos = pos_.x;
                        if (std::abs(distance) <= road_x + ROAD_RADIUS) {
                            pos_.x = distance;
                            at_bound = false; 
                        }
                        else {
                            speed_.dx < 0 ? pos_.x = road_x - ROAD_RADIUS : pos_.x = road_x + ROAD_RADIUS;
                            at_bound = true;
                        }
                        already_traveled = pos_.x - start_pos;
                    }
                }
            }
        }
        
        if (at_bound) {
            speed_ = { 0,0 };
        }
    }

    double GetWidth() const { return width_; }
    
    bool AddLoot(LootSharedPtr loot) {
        if (lootbag_capacity_ > lootbag_.size()) {
            lootbag_.push_back(loot);
            return true;
            score_ += loot->GetValue();
        }
        return false;
    }

    void ClearBag() { lootbag_.clear(); }

    void AddScore(int score_points) {
        score_ += score_points;
    }
    
    size_t GetLootCount() { return lootbag_.size(); }
    
    Loots GetLootBag() const { return lootbag_; }
    
    int GetScore() const { return score_; }

    size_t GetLootBagCapacity() const { return lootbag_capacity_; }

    DIRECTION GetDir() const { return dir_; }

    double GetMovementSpeed() const { return movement_speed_; }

    void ResetAFKTime() { AFK_time_ = 0.0; }
    void UpdateAFKTime(double time) { AFK_time_ += time; }
    double GetAFKTime() const { return AFK_time_; }

    void ResetPlaytime() { play_time_ = 0.0; }
    void UpdatePlaytime(double time) { play_time_ += time; }
    double GetPlaytime() const { return play_time_; }

    bool IsMoving() const { return speed_.dx != 0 || speed_.dy != 0; }

private:

    static uint64_t dog_counter;
    std::uint64_t id_;
    std::string name_;
    MapPoint pos_;
    MapSpeed speed_;
    DIRECTION dir_;
    double movement_speed_ = 0;
    double width_ = 0.6;
    Loots lootbag_;
    size_t lootbag_capacity_ = 3;
    int score_ = 0;

    MapPoint previous_pos_; // переменная для хранения предыдущей позиции собаки перед перемещением. Используется для сбора лута в FindGatherEvents 
    double AFK_time_ = 0.0;
    double play_time_ = 0.0;
    
};

class GameSession {
public:
    GameSession(MapSharedPtr map) : map_(map) {}
    void AddDog(DogSharedPtr dog) { dogs_.push_back(dog); }
    void AddLoot(LootSharedPtr loot) { loot_.push_back(loot); }
    std::uint64_t GetId() const noexcept { return id_; }
    void SetId(std::uint64_t id) { id_ = id; }
    MapSharedPtr GetMap() const noexcept { return map_; }
    Loots GetLoots() const noexcept { return loot_; }
    Dogs GetDogs() const noexcept { return dogs_; }
    DogSharedPtr GetDog(std::uint64_t dog_id) const {
        for (auto& d : dogs_) {
            if (d->GetId() == dog_id) {
                return d;
            }
        }
        return nullptr;
    }
    /*
    std::map <uint64_t, double> GetPlaytimeSheet() const { return dog_id_to_playtime_; }
    double GetDogPlaytime(uint64_t dog_id) const { return dog_id_to_playtime_.at(dog_id); }
    void UpdateDogPlayTime(const uint64_t dog_id, double playtime) {
        dog_id_to_playtime_[dog_id] += playtime;
    }
    void LoadPlaytimeSheet(const std::map <uint64_t, double>& load_data) { dog_id_to_playtime_ = load_data; }
     void RemoveDog(std::uint64_t dog_id) {
        dogs_.erase(find(dogs_.begin(), dogs_.end(), GetDog(dog_id)));
        dog_id_to_playtime_.erase(dog_id);
    }
    */
    void UpdateGameSessionCounter() { if (id_ > session_counter) { session_counter = id_; } }
    void RemoveDog(std::uint64_t dog_id) {
        dogs_.erase(find(dogs_.begin(), dogs_.end(), GetDog(dog_id)));
    }
    void RemoveLoot(LootSharedPtr loot) { loot_.erase(std::remove(loot_.begin(), loot_.end(), loot), loot_.end()); }
    void UpdateSessionPlayersIdCounter() { map_->SetPlayerIdCounter(dogs_.size()); }


private:
    Dogs dogs_;
    Loots loot_;
    MapSharedPtr map_;
    uint64_t session_counter = 0;
    std::uint64_t id_ = 0;

    // std::map <uint64_t, double> dog_id_to_playtime_;
};


struct LootConfig {
    double period_;
    double probability_;
};

class GameItemGathererProvider : public collision_detector::ItemGathererProvider {
public:
    GameItemGathererProvider(const Dogs& dogs, const Loots& loots)
        : dogs_(dogs), loots_(loots) {}

    size_t ItemsCount() const override {
        return loots_.size();
    }

    collision_detector::Item GetItem(size_t idx) const override {
        const auto& loot = loots_.at(idx);
        return { {loot->GetPos().x, loot->GetPos().y}, LOOT_RADIUS }; 
    }

    size_t GatherersCount() const override {
        return dogs_.size();
    }

    collision_detector::Gatherer GetGatherer(size_t idx) const override {
        const auto& dog = dogs_.at(idx);
        MapPoint start_pos = dog->GetPreviousPosition();
        MapPoint end_pos = dog->GetPosition();
        return { {start_pos.x, start_pos.y}, {end_pos.x, end_pos.y}, (dog->GetWidth()) }; 
    }

private:
    const Dogs& dogs_;
    const Loots& loots_;
};

class GameOfficePassProvider : public collision_detector::ItemGathererProvider {
public:
    GameOfficePassProvider(const Dogs& dogs, const Map::Offices& offices)
        : dogs_(dogs), offices_(offices) {}

    size_t ItemsCount() const override {
        return offices_.size();
    }

    collision_detector::Item GetItem(size_t idx) const override {
        const auto& office = offices_.at(idx);
        return { { static_cast<double> (office.GetPosition().x), static_cast<double> (office.GetPosition().y)}, BASE_RADIUS};
    }

    size_t GatherersCount() const override {
        return dogs_.size();
    }

    collision_detector::Gatherer GetGatherer(size_t idx) const override {
        const auto& dog = dogs_.at(idx);
        MapPoint start_pos = dog->GetPreviousPosition();
        MapPoint end_pos = dog->GetPosition();
        return { {start_pos.x, start_pos.y}, {end_pos.x, end_pos.y}, (dog->GetWidth()) };
    }

private:
    const Dogs& dogs_;
    const Map::Offices& offices_;
};

class Game {
    public:
        using Maps = std::vector<MapSharedPtr>;
        using GameSessions = std::vector<GameSessionSharedPtr>;

        void AddMap(Map map);

        void Update(std::uint64_t time_delta) { // для ручного управления
            MovePlayersAndUpdateLoot(time_delta);

            passed_time += time_delta;

            if (passed_time >= save_period && manual_time_control_ && !state_file_path_.empty()) {
                SaveGameState();
                passed_time = 0;
            }
        }

        void Update(std::chrono::milliseconds time_delta) { // для запуска с тикером
            MovePlayersAndUpdateLoot(time_delta.count());
        }

        void EnableManualTimeControl() {
            manual_time_control_ = true;
        }

        const bool ManualTimeControlMode() const {
            return manual_time_control_;
        }

        void EnableRandomSpawner() {
            dog_random_spawning_mode_ = true;
        }

        const Maps& GetMaps() const noexcept {
            return maps_;
        }

        MapSharedPtr FindMap(const Map::Id& id) const noexcept {
            if (auto it = map_id_to_index_.find(id); it != map_id_to_index_.end()) {
                return maps_.at(it->second);
            }
            return nullptr;
        };

        void AddPlayer(Player& player, const Map::Id& map_id) {
            // Создаем собаку с именем игрока
            auto dog = std::make_shared<Dog>(player.GetName());

            

            // Проверка флага рандомного спавна собаки (задается в консоли при запуске программы)
            if (dog_random_spawning_mode_) {
                dog->SetPos(GetRandomMapPointOnRoads(map_id));
            }
            else {
                dog->SetPos(GetBeginMapPointOnRoads(map_id));
            }

            auto map_ptr = FindMap(map_id);
            dog->SetMovementSpeed(map_ptr->GetDefaultDogSpeed());
            dog->SetBagCapacity(map_ptr->GetDefaultBagCapacity());            
            
            // dog_id_to_player_id_[dog->GetId()]

            // Назначить собаку игроку 
            player.SetDog(dog);

            // Если сессия найдена то добавляем в сессию, если нет - создаем
            auto it = sessions_.find(map_id);
            if (it == sessions_.end()) {
                auto new_session = std::make_shared<GameSession>(std::make_shared<Map>(*map_ptr));
                new_session->AddDog(dog);
    
                new_session->SetId(session_counter_);
                ++session_counter_;
                game_sessions_.emplace_back(new_session);
                sessions_[map_id] = new_session;

                player.ChangeSession(new_session->GetId());

            }
            else {
                auto& session = it->second;
                session->AddDog(dog);
                auto session_id = session->GetId();
                player.ChangeSession(session_id);
            }

            players_.emplace(player.GetAuthToken(), player);
        }

        const Player* FindPlayerByToken(const std::string& token) const noexcept {
            auto it = players_.find(token);
            return it != players_.end() ? &it->second : nullptr;
        }

     
        GameSessionSharedPtr FindGameSession(std::uint64_t session_id) const noexcept {
            for (const auto& session : game_sessions_) {
                if (session->GetId() == session_id) {
                    return session;
                }
            }
            return nullptr;
        }

        void SetLootConfig(const LootConfig& config) { loot_config_ = config; }
        
        const LootConfig& GetLootConfig() const noexcept { return loot_config_; }

        void MovePlayersAndUpdateLoot(uint64_t tick_time) {
            double travel_time = tick_time / 1000.0;
            for (const auto& game_session : game_sessions_) {
                const auto& current_map_roads = game_session->GetMap()->GetRoads();
                const auto& current_sessuion_dogs = game_session->GetDogs();
   
                for (const auto& dog : current_sessuion_dogs) {
                    if (dog->IsMoving()) {
                        dog->Move(travel_time, current_map_roads);
                        dog->UpdatePlaytime(travel_time);
                        dog->ResetAFKTime();
                    } else if (dog->GetAFKTime() + travel_time >= default_afk_time) {
                        dog->UpdatePlaytime(default_afk_time - dog->GetAFKTime());
                        dog->UpdateAFKTime(default_afk_time - dog->GetAFKTime());
                        RemovePlayerAndSaveStats(dog, game_session);
                    }
                    else {
                        dog->UpdateAFKTime(travel_time);
                        dog->UpdatePlaytime(travel_time);
                    }
                  
                }
                UpdateGatheredLoot(game_session); // Dog содержит в себе инфо о своей предыдущей локации, поэтому tick_time не используется
                UpdateLoot(game_session, tick_time);
            }
        }

 

        void UpdateGatheredLoot(GameSessionSharedPtr session) {
            const auto& session_dogs = session->GetDogs();
            const auto& session_loots = session->GetLoots();
            const auto& session_offices = session->GetMap()->GetOffices();

            GameItemGathererProvider provider_gatherloot(session_dogs, session_loots);
            const auto& gather_events = collision_detector::FindGatherEvents(provider_gatherloot);

            GameOfficePassProvider provider_passloot(session_dogs, session_offices);
            const auto& pass_events = collision_detector::FindGatherEvents(provider_passloot);

            for (const auto& event : gather_events) {
                const auto& dog = session_dogs.at(event.gatherer_id);
                const auto& loot = session_loots.at(event.item_id);

                if (dog->AddLoot(loot)) { // Добавление лута с проверкой вместимости рюкзака
                    session->RemoveLoot(loot);
                }
            }
            
            for (const auto& event : pass_events) {
                session_dogs.at(event.gatherer_id)->ClearBag();                
            }
        }

        void SetDefaultDogsSpeed(double speed) {
            default_dog_speed = speed;
        }
        
        void SetDefaultLootbagCapacity(uint64_t capacity) {
            default_lootbag_capacity = capacity;
        }

        void SetDefaultAFKtime(double afk_time) {
            default_afk_time = afk_time;
        }


        double GetDefaultAFKtime() const {
            return default_afk_time;
        }

        void UpdateLoot(GameSessionSharedPtr session, std::chrono::milliseconds time_delta) {
            loot_gen::LootGenerator gen{ std::chrono::milliseconds(static_cast<int>(loot_config_.period_)),
                                         loot_config_.probability_ };
            unsigned count_new_loot_to_add = gen.Generate(time_delta,
                static_cast<unsigned>(session->GetLoots().size()),
                static_cast<unsigned>(session->GetDogs().size()));
            std::random_device rd; 
            std::mt19937 randomiser(rd()); 
            const auto& current_map = session->GetMap();
            std::uniform_int_distribution<> dis(0, current_map->GetLootTypesCount() - 1);

            while (count_new_loot_to_add--) {
                const auto& map_id = session->GetMap()->GetId();
                MapPoint loot_pos = GetRandomMapPointOnRoads(map_id);
                int loot_type = dis(randomiser);
                uint64_t loot_value = current_map->GetLootValueByTypeID(loot_type);
                session->AddLoot(std::make_shared<Loot>(loot_type, loot_value, loot_pos));
            }
        }

        void UpdateLoot(GameSessionSharedPtr session, int64_t tick_time) {
            std::chrono::milliseconds time_delta {tick_time};
            UpdateLoot(session, time_delta);
        }

        double GetDefaultDogSpeed() const {
            return default_dog_speed;
        }

        uint64_t GetDefaultLootBagCapacity() const {
            return default_lootbag_capacity;
        }

        void SetDogToSessionId(uint64_t dog_id, uint64_t session_id) {
            dog_id_to_session_id_[dog_id] = session_id;
        }

        void SetSaveStateFilePath(std::string file_path) {
            state_file_path_ = file_path;
        }

        void SaveGameState();

        void LoadGameState();

        void SetSavePeriod(int64_t saveperiod) {
            save_period = saveperiod;
        }
       
        void SetDBConnectionPool(ConnectionPoolPtr pool) {
            pool_ = pool;
        }

        ConnectionPoolPtr GetDBConnectionPool() {
            return pool_;
        }
    private:
        MapPoint GetRandomMapPointOnRoads(const Map::Id& id) {
            std::random_device rd;
            size_t map_index = map_id_to_index_[id];
            size_t number_of_roads = maps_.at(map_index)->GetRoads().size() - 1;
            std::uniform_int_distribution<int> dist(0, number_of_roads);
            auto& random_road = maps_[map_index]->GetRoads()[dist(rd)];

            int x1 = random_road.GetStart().x;
            int y1 = random_road.GetStart().y;
            int x2 = random_road.GetEnd().x;
            int y2 = random_road.GetEnd().y;
            if (x1 > x2) {
                std::swap(x1, x2);
            }
            if (y1 > y2) {
                std::swap(y1, y2);
            }
            std::uniform_int_distribution<int> dist_x(x1, x2);
            std::uniform_int_distribution<int> dist_y(y1, y2);
            int x_random_at_map = dist_x(rd);
            int y_random_at_map = dist_y(rd);

            return MapPoint(x_random_at_map, y_random_at_map);
        }

        MapPoint GetBeginMapPointOnRoads(const Map::Id& id) {
            size_t map_index = map_id_to_index_[id];
            size_t number_of_roads = maps_.at(map_index)->GetRoads().size() - 1;
            auto& test_road = maps_[map_index]->GetRoads()[0];
            return MapPoint(test_road.GetStart().x, test_road.GetStart().y);
        }

        using MapIdHasher = util::TaggedHasher<Map::Id>;
        using MapIdToIndex = std::unordered_map<Map::Id, size_t, MapIdHasher>;
        using PlayerTokenToIndex = std::unordered_map<std::string, size_t>;

        Maps maps_;
        MapIdToIndex map_id_to_index_;

        std::unordered_map<std::string, Player> players_;

        GameSessions game_sessions_;
        std::unordered_map<Map::Id, GameSessionSharedPtr, MapIdHasher> sessions_;
        
        LootConfig loot_config_;

        std::unordered_map <uint64_t, uint64_t> dog_id_to_session_id_;

        std::uint64_t session_counter_ = 0;
        double default_dog_speed = 1.0;
        uint64_t default_lootbag_capacity = 3;
        double default_afk_time = 60;

        bool dog_random_spawning_mode_ = false;
        bool manual_time_control_ = false;

        // ДЛЯ СОХРАНЕНИЯ СОСТОЯНИЯ
        std::string state_file_path_;
        int64_t passed_time = 0;
        int64_t save_period = 0;
        // ДЛЯ СОХРАНЕНИЯ РЕЙТИНГА
        ConnectionPoolPtr pool_;

    };

}  // namespace model
