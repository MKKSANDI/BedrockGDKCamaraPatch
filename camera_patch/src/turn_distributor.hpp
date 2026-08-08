#pragma once

#include "hook_callbacks.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mcfix {

class TurnDistributor {
public:
    explicit TurnDistributor(std::uint64_t qpc_frequency);
    Vec2 distribute(void* object, std::uint64_t qpc, Vec2 input);

private:
    static constexpr std::size_t max_slots = 8;

    Vec2 pending_total() const noexcept;
    void clear_queue() noexcept;

    std::array<Vec2, max_slots> scheduled_{};
    std::uint64_t qpc_frequency_{};
    std::uint64_t horizon_qpc_{};
    std::uint64_t last_qpc_{};
    double frame_interval_qpc_{};
    void* object_{};
    bool primed_{false};
};

}  // namespace mcfix
