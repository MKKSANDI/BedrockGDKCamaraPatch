#pragma once

#include "hook_callbacks.hpp"

#include <cstdint>

namespace mcfix {

class CadenceClassifier {
public:
    explicit CadenceClassifier(std::uint64_t qpc_frequency) noexcept;
    bool observe(std::uint64_t qpc, Vec2 input) noexcept;
    bool affected() const noexcept { return affected_; }

private:
    std::uint64_t qpc_frequency_{};
    std::uint64_t last_call_qpc_{};
    std::uint64_t last_nonzero_qpc_{};
    double camera_interval_qpc_{};
    unsigned consecutive_batched_intervals_{};
    bool affected_{};
};

}  // namespace mcfix
