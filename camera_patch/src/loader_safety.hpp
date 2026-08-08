#pragma once

#include <cstdint>
#include <initializer_list>
#include <span>

namespace mcfix {

enum class RemoteLoadState {
    completed_nonzero,
    completed_zero,
    still_running,
};

struct RemoteCleanupDecision {
    bool free_remote_path{false};
    bool load_succeeded{false};
};

RemoteCleanupDecision remote_cleanup(RemoteLoadState state);
bool stable_target_pid(std::uint32_t expected, std::span<const std::uint32_t> current);

inline bool stable_target_pid(
    std::uint32_t expected,
    std::initializer_list<std::uint32_t> current) {
    return stable_target_pid(expected, std::span<const std::uint32_t>(current.begin(), current.size()));
}

}  // namespace mcfix
