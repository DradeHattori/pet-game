#include "collision_detector.h"
#include <cassert>

namespace collision_detector {

    CollectionResult TryCollectPoint(geom::Point2D a, geom::Point2D b, geom::Point2D c) {
        // Проверим, что перемещение ненулевое.
        // Тут приходится использовать строгое равенство, а не приближённое,
        // пскольку при сборе заказов придётся учитывать перемещение даже на небольшое
        // расстояние.
        // assert(b.x != a.x || b.y != a.y);
        const double u_x = c.x - a.x;
        const double u_y = c.y - a.y;
        const double v_x = b.x - a.x;
        const double v_y = b.y - a.y;
        const double u_dot_v = u_x * v_x + u_y * v_y;
        const double u_len2 = u_x * u_x + u_y * u_y;
        const double v_len2 = v_x * v_x + v_y * v_y;
        const double proj_ratio = u_dot_v / v_len2;
        const double sq_distance = u_len2 - (u_dot_v * u_dot_v) / v_len2;

        return CollectionResult(sq_distance, proj_ratio);
    }

    std::vector<GatheringEvent> FindGatherEvents(const ItemGathererProvider& provider) {
        std::vector<GatheringEvent> events;
        for (size_t gatherer_idx = 0; gatherer_idx < provider.GatherersCount(); ++gatherer_idx) {
            const Gatherer gatherer = provider.GetGatherer(gatherer_idx);
            const auto& start_pos = gatherer.start_pos;
            const auto& end_pos = gatherer.end_pos;
            const double gatherer_radius = gatherer.width / 2.0;
            for (size_t item_idx = 0; item_idx < provider.ItemsCount(); ++item_idx) {
                const Item item = provider.GetItem(item_idx);
                const auto& item_pos = item.position;
                const double item_radius = item.width / 2.0;
                const double collect_radius = gatherer_radius + item_radius;
                // Проверяем возможность сбора
                const CollectionResult result = TryCollectPoint(start_pos, end_pos, item_pos);
                if (result.IsCollected(collect_radius)) {
                    events.push_back({
                        item_idx,
                        gatherer_idx,
                        result.sq_distance,
                        result.proj_ratio
                        });
                }
            }
        }
        // Сортируем события сначала по времени, затем по gatherer_id, затем по item_id
        std::sort(events.begin(), events.end(), [](const GatheringEvent& lhs, const GatheringEvent& rhs) {
            if (lhs.time != rhs.time) return lhs.time < rhs.time;
            if (lhs.gatherer_id != rhs.gatherer_id) return lhs.gatherer_id < rhs.gatherer_id;
            return lhs.item_id < rhs.item_id;
            });
        return events;
    }

}  // namespace collision_detector