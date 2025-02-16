#include "model.h"
#include "model_serialization.h"

#include <stdexcept>
#include <fstream>
#include "logger.h"

namespace model {
    using namespace std::literals;

    uint64_t Dog::dog_counter = 0;
    uint64_t Loot::loot_counter = 0;

    void Map::AddOffice(Office office) {
        if (warehouse_id_to_index_.contains(office.GetId())) {
            throw std::invalid_argument("Duplicate warehouse");
        }

        const size_t index = offices_.size();
        Office& o = offices_.emplace_back(std::move(office));
        try {
            warehouse_id_to_index_.emplace(o.GetId(), index);
        } catch (...) {
            // Удаляем офис из вектора, если не удалось вставить в unordered_map
            offices_.pop_back();
            throw;
        }
    }

    void Game::AddMap(Map map) {
        const size_t index = maps_.size();
        if (auto [it, inserted] = map_id_to_index_.emplace(map.GetId(), index); !inserted) {
            throw std::invalid_argument("Map with id "s + *map.GetId() + " already exists"s);
        }
        else {
            maps_.emplace_back(std::make_shared<Map> (map));
        }
    }

    void Game::SaveGameState() {
        using OutputArch = boost::archive::text_oarchive;

        // Получаем директорию, где будет сохранен финальный файл
        FS::path state_file_dir = FS::path(state_file_path_).parent_path();

        // Проверяем, существует ли директория savefiles, и создаем её, если она отсутствует
        if (!state_file_dir.empty() && !FS::exists(state_file_dir)) {
            if (!FS::create_directories(state_file_dir)) {
                LogEventInfo("Saving file:", "Failed to create directory for game state.");
                return;
            }
        }

        // Создаем временный файл в той же директории
        FS::path temp_path = state_file_dir / "temp_game_save_data";

        std::ofstream ofs(temp_path, std::ios::binary);
        if (!ofs) {
            LogEventInfo("Saving file:", "Failed to open temporary file for saving game state.");
            return;
        }

        try {
            OutputArch output_archive{ ofs };

            // Сохраняем в какой сессии была каждая собака
            output_archive << dog_id_to_session_id_;

            // Сохраняем сессии
            size_t session_count = game_sessions_.size();
            output_archive << session_count;
            for (const auto& s : game_sessions_) {
                serialization::GameSessionRepr repr_session(s);
                output_archive << repr_session;
            }

            // Сохраняем игроков
            size_t players_count = players_.size();
            output_archive << players_count;
            for (const auto& p : players_) {
                serialization::PlayerRepr repr_player(p.second);
                output_archive << repr_player;
            }

            ofs.close();

            // Переименовываем временный файл в финальный
            FS::rename(temp_path, state_file_path_);
            LogEventInfo("Save game", "Game state saved successfully.");
        }
        catch (const std::exception& ex) {
            LogError(ex, "Error saving game state.");
            ofs.close();
            FS::remove(temp_path);
        }
    }



    void Game::LoadGameState() {
        using InputArchive = boost::archive::text_iarchive;

        FS::path p(state_file_path_);
        if (!FS::exists(p)) {
            LogEventInfo("State file does not exist: ", state_file_path_);
            throw std::runtime_error("State file does not exist: " + state_file_path_);
        }

        std::ifstream ifs(p);
        if (!ifs) {
            LogEventInfo("Failed to open state file: ", state_file_path_);
            throw std::runtime_error("Failed to open state file: " + state_file_path_);
        }

        try {
            InputArchive input_archive{ ifs };

            input_archive >> dog_id_to_session_id_;

            size_t sessions_count = 0;
            input_archive >> sessions_count;
            for (size_t i = 0; i < sessions_count; i++) {
                serialization::GameSessionRepr sess_repr;
                input_archive >> sess_repr;
                const auto& sess = sess_repr.Restore(*this);
                game_sessions_.push_back(sess);
                sessions_[sess->GetMap()->GetId()] = sess;
            }


            size_t players_count = 0;
            input_archive >> players_count;
            for (size_t i = 0; i < players_count; i++) {
                serialization::PlayerRepr player_repr;
                input_archive >> player_repr;

                if (auto current_dogs_session = dog_id_to_session_id_.find(player_repr.GetDogId()); current_dogs_session != dog_id_to_session_id_.end()) {
                    Player player = player_repr.Restore(game_sessions_.at(current_dogs_session->second)->GetDog(player_repr.GetDogId()));
                    players_.emplace( player.GetAuthToken(), player);
                    game_sessions_.at(current_dogs_session->second)->UpdateSessionPlayersIdCounter();
                }
                else {
                    LogEventInfo("Unable to restore a player", "Dog session not found.");
                    throw std::logic_error("Unable to restore a player: Dog session not found.");
                }
            }
            
        }
        catch (const std::exception& ex) {
            LogError(ex, ex.what());
            throw;
        }
    }
    

}  // namespace model
