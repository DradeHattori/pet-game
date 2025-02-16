#pragma once

#include <string>
#include <boost/json.hpp>

namespace rawinfo {
	class FrontendInfo {
	public: 
        void SetRawInfo(const boost::json::array& mapsinfo) {
            mapsinfo_ = mapsinfo;
        }

        // Get loot information for a specific map by ID
        boost::json::array GetLootInfo(const std::string& map_id) const {
            for (const auto& map : mapsinfo_) {
                if (!map.is_object()) continue;

                const auto& map_obj = map.as_object();
                if (map_obj.contains("id") && map_obj.at("id").as_string() == map_id) {
                    if (map_obj.contains("lootTypes") && map_obj.at("lootTypes").is_array()) {
                        return map_obj.at("lootTypes").as_array();
                    }
                }
            }
            // Return an empty array if the map ID or lootTypes are not found
            return boost::json::array{};
        }

	private:
		boost::json::array mapsinfo_;
	};
}