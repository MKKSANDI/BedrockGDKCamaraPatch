#include "mcfix/telemetry.hpp"

namespace mcfix {

std::string_view event_kind_name(EventKind kind) {
    switch (kind) {
        case EventKind::input_tick:
            return "input_tick";
        case EventKind::turn_delta:
            return "turn_delta";
        case EventKind::camera_update:
            return "camera_update";
        case EventKind::turn_distributed:
            return "turn_distributed";
    }
    return "unknown";
}

}  // namespace mcfix
