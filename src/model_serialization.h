#pragma once

#include <boost/serialization/vector.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/string.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include "model.h"

namespace model {

    // Сериализация для MapSpeed
    template <typename Archive>
    void serialize(Archive& ar, MapSpeed& vec, [[maybe_unused]] const unsigned version) {
        ar& vec.dx;
        ar& vec.dy;
    }

    // Сериализация для MapPoint
    template <typename Archive>
    void serialize(Archive& ar, MapPoint& point, [[maybe_unused]] const unsigned version) {
        ar& point.x;
        ar& point.y;
    }

    // Сериализация для LootSharedPtr
    template <typename Archive>
    void serialize(Archive& ar, LootSharedPtr& obj, [[maybe_unused]] const unsigned version) {
        ar& obj->id_;
        ar& obj->loot_type_;
        ar& obj->position_;
        ar& obj->value_;
    }

}  // namespace model

namespace serialization {

    using namespace model;

    // Сериализация для LootRepr
    class LootRepr {
    public:
        LootRepr() = default;

        explicit LootRepr(LootSharedPtr loot)
            : id_(loot->GetId())
            , loot_type_(loot->GetType())
            , position_(loot->GetPos())
            , value_(loot->GetValue())
        {}

        [[nodiscard]] LootSharedPtr Restore() const {
            auto result = std::make_shared<Loot>();
            result->SetId(id_);
            result->SetType(loot_type_);
            result->SetPos(position_);
            result->SetValue(value_);
            return result;
        }

        template <typename Archive>
        void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
            ar& id_;
            ar& loot_type_;
            ar& position_;
            ar& value_;
        }

    private:
        std::uint64_t id_;
        int loot_type_;
        MapPoint position_;
        int value_;
    };

    std::string RemovePreviousId(std::string& name_with_id, std::uint64_t& id) {
        std::string new_name;
        size_t chars_to_remove = std::to_string(id).length() + 1;
        if (chars_to_remove <= name_with_id.length()) {
            new_name = name_with_id.substr(0, name_with_id.length() - chars_to_remove);
        }
        return new_name;
    }

    // Сериализация для DogRepr
    class DogRepr {
    public:
        DogRepr() = default;

        explicit DogRepr(const DogSharedPtr dog)
            : id_(dog->GetId())
            , name_(dog->GetName())
            , pos_(dog->GetPosition())
            , previous_pos_(dog->GetPreviousPosition())
            , speed_(dog->GetSpeed())
            , dir_(dog->GetDir())
            , score_(dog->GetScore())
            , lootbag_capacity_(dog->GetLootBagCapacity())
            , movement_speed_(dog->GetMovementSpeed())
            , width_(dog->GetWidth()) {

            name_ = RemovePreviousId(name_, id_); // удаляем ID собаки из имени, во избежание дублирования при восстановлении
            for (const auto& loot : dog->GetLootBag()) {
                loot_id_to_value[loot->GetId()] = loot->GetValue();
                loot_id_to_type[loot->GetId()] = loot->GetType();
            }
        }

        [[nodiscard]] Dog Restore() const {
            Dog dog{ name_ };
            dog.SetId(id_);
            dog.SetPos(pos_);
            dog.SetPreviousPos(previous_pos_);
            dog.SetDirection(GetDirectionString(dir_));
            dog.SetMovementSpeed(movement_speed_);
            dog.SetBagCapacity(lootbag_capacity_);
            dog.SetWidth(width_);
            dog.UpdateDogCounter();
            for (const auto& [id, val] : loot_id_to_value) {
                int type = loot_id_to_type.at(id);
                LootSharedPtr loot = std::make_shared<Loot>(type, val);
                dog.AddLoot(loot);
            }
            dog.AddScore(score_);
            return dog;
        }

        template <typename Archive>
        void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
            ar& id_;
            ar& name_;
            ar& pos_;
            ar& previous_pos_;
            ar& speed_;
            ar& dir_;
            ar& score_;
            ar& lootbag_capacity_;
            ar& movement_speed_;
            ar& width_;
            ar& loot_id_to_value;
            ar& loot_id_to_type;
        }

        uint64_t GetId() const {
            return id_;
        }

    private:
        std::uint64_t id_;
        std::string name_;
        MapPoint pos_;
        MapPoint previous_pos_;
        MapSpeed speed_;
        DIRECTION dir_;
        double movement_speed_;
        double width_;
        std::unordered_map<uint64_t, int> loot_id_to_value;
        std::unordered_map<uint64_t, int> loot_id_to_type;
        size_t lootbag_capacity_;
        int64_t score_;

        static std::string GetDirectionString(DIRECTION dir) {
            switch (dir) {
            case DIRECTION::NORTH: return "U";
            case DIRECTION::SOUTH: return "D";
            case DIRECTION::EAST: return "R";
            case DIRECTION::WEST: return "L";
            default: return "";
            }
        }
    };

    // Сериализация для PlayerRepr
    class PlayerRepr {
    public:
        PlayerRepr() = default;

        explicit PlayerRepr(const Player& player)
            : id_(player.GetId())
            , name_(player.GetName())
            , token_(player.GetAuthToken())
            , session_id_(player.GetSessionId())
            , dog_id_(player.GetDog()->GetId())
        {}

        [[nodiscard]] Player Restore(DogSharedPtr dog) const {
            Player player(id_, name_, token_);
            player.SetDog(dog);
            player.ChangeSession(session_id_);
            return player;
        }

        template <typename Archive>
        void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
            ar& id_;
            ar& name_;
            ar& token_;
            ar& session_id_;
            ar& dog_id_;
        }

        uint64_t GetDogId() {
            return dog_id_;
        }

    private:
        int id_;
        std::string name_;
        std::string token_;
        uint64_t session_id_;
        uint64_t dog_id_;
    };

    // Сериализация для GameSessionRepr
    class GameSessionRepr {
    public:
        GameSessionRepr() = default;

        explicit GameSessionRepr(const GameSessionSharedPtr session)
            : id_(session->GetId())
            , map_id_(*(session->GetMap()->GetId()))
        {
            for (const auto& dog : session->GetDogs()) {
                dogs_.emplace_back(dog);
            }
            for (const auto& loot : session->GetLoots()) {
                loots_.emplace_back(loot);
            }
        }

        [[nodiscard]] GameSessionSharedPtr Restore(model::Game& game) const {
            auto map = game.FindMap(model::Map::Id(map_id_));
            if (map == nullptr) {
                throw std::logic_error("ERROR: map not found.");
            }

            auto session = std::make_shared<model::GameSession>(map);
            session->SetId(id_);
            session->UpdateGameSessionCounter();

            for (const auto& dog_repr : dogs_) {
                session->AddDog(std::make_shared<Dog>(dog_repr.Restore()));
                game.SetDogToSessionId(dog_repr.GetId(), session->GetId());
            }
            for (const auto& loot_repr : loots_) {
                session->AddLoot(loot_repr.Restore());
            }

            return session;
        }

        template <typename Archive>
        void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
            ar& id_;
            ar& dogs_;
            ar& loots_;
            ar& map_id_;
            ar& dog_id_to_playtime_;
        }

    private:
        uint64_t id_;
        std::vector<DogRepr> dogs_;
        std::vector<LootRepr> loots_;
        std::string map_id_;
        std::map <uint64_t, double> dog_id_to_playtime_;
    };


}  // namespace serialization