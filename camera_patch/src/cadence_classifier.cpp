#include "cadence_classifier.hpp"

#include <cmath>

namespace mcfix {

CadenceClassifier::CadenceClassifier(std::uint64_t qpc_frequency) noexcept
    : qpc_frequency_(qpc_frequency) {}

bool CadenceClassifier::observe(std::uint64_t qpc, Vec2 input) noexcept {
    const bool nonzero = std::isfinite(input.x) && std::isfinite(input.y) &&
        (input.x != 0.0F || input.y != 0.0F);
    if (qpc_frequency_ == 0 || !std::isfinite(input.x) || !std::isfinite(input.y)) {
        consecutive_batched_intervals_ = 0;
        return affected_;
    }
    if (last_call_qpc_ == 0 || qpc <= last_call_qpc_) {
        last_call_qpc_ = qpc;
        if (nonzero) last_nonzero_qpc_ = qpc;
        camera_interval_qpc_ = 0.0;
        consecutive_batched_intervals_ = 0;
        return affected_;
    }

    const auto call_interval = qpc - last_call_qpc_;
    last_call_qpc_ = qpc;
    if (call_interval > qpc_frequency_ / 4) {
        camera_interval_qpc_ = 0.0;
        last_nonzero_qpc_ = nonzero ? qpc : 0;
        consecutive_batched_intervals_ = 0;
        return affected_;
    }
    camera_interval_qpc_ = camera_interval_qpc_ == 0.0
        ? static_cast<double>(call_interval)
        : camera_interval_qpc_ * 0.85 + static_cast<double>(call_interval) * 0.15;

    if (!nonzero) return affected_;
    if (last_nonzero_qpc_ != 0 && qpc > last_nonzero_qpc_) {
        const auto source_interval = qpc - last_nonzero_qpc_;
        const auto minimum_batch = qpc_frequency_ * 12 / 1000;
        const auto maximum_batch = qpc_frequency_ * 35 / 1000;
        const auto maximum_fast_camera = static_cast<double>(qpc_frequency_) * 0.009;
        const bool repeated_coarse_batch =
            source_interval >= minimum_batch && source_interval <= maximum_batch &&
            camera_interval_qpc_ > 0.0 && camera_interval_qpc_ <= maximum_fast_camera &&
            static_cast<double>(source_interval) >= camera_interval_qpc_ * 2.5;
        if (repeated_coarse_batch) {
            ++consecutive_batched_intervals_;
            if (consecutive_batched_intervals_ >= 5) affected_ = true;
        } else if (!affected_) {
            consecutive_batched_intervals_ = 0;
        }
    }
    last_nonzero_qpc_ = qpc;
    return affected_;
}

}  // namespace mcfix
