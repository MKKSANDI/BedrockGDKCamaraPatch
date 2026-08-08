#include "loader_safety.hpp"

namespace mcfix {

RemoteCleanupDecision remote_cleanup(RemoteLoadState state) {
    switch (state) {
        case RemoteLoadState::completed_nonzero:
            return {true, true};
        case RemoteLoadState::completed_zero:
            return {true, false};
        case RemoteLoadState::still_running:
            return {false, false};
    }
    return {};
}

bool stable_target_pid(std::uint32_t expected, std::span<const std::uint32_t> current) {
    return current.size() == 1 && current.front() == expected;
}

}  // namespace mcfix
