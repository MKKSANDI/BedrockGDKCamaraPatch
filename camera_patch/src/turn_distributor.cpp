#include "turn_distributor.hpp"

#include <algorithm>
#include <cmath>

namespace mcfix {
namespace {

bool finite(Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

Vec2 add(Vec2 left, Vec2 right) noexcept {
    return {left.x + right.x, left.y + right.y};
}

}  // namespace

TurnDistributor::TurnDistributor(std::uint64_t qpc_frequency)
    : qpc_frequency_(qpc_frequency),
      horizon_qpc_(qpc_frequency == 0 ? 0 : (std::max)(std::uint64_t{1}, qpc_frequency / 50)) {}

Vec2 TurnDistributor::pending_total() const noexcept {
    Vec2 total{};
    for (const auto value : scheduled_) {
        total = add(total, value);
    }
    return total;
}

void TurnDistributor::clear_queue() noexcept {
    scheduled_.fill({});
}

Vec2 TurnDistributor::distribute(void* object, std::uint64_t qpc, Vec2 input) {
    if (qpc_frequency_ == 0 || !finite(input)) {
        clear_queue();
        primed_ = false;
        return input;
    }

    if (!primed_ || object != object_) {
        clear_queue();
        primed_ = true;
        object_ = object;
        last_qpc_ = qpc;
        frame_interval_qpc_ = 0.0;
        return input;
    }

    if (qpc <= last_qpc_) {
        const auto output = add(pending_total(), input);
        clear_queue();
        last_qpc_ = qpc;
        frame_interval_qpc_ = 0.0;
        return output;
    }

    const auto elapsed = qpc - last_qpc_;
    last_qpc_ = qpc;
    if (elapsed >= horizon_qpc_) {
        const auto output = add(pending_total(), input);
        clear_queue();
        frame_interval_qpc_ = static_cast<double>(elapsed);
        return output;
    }

    const auto interval = static_cast<double>(elapsed);
    frame_interval_qpc_ = frame_interval_qpc_ == 0.0
        ? interval
        : frame_interval_qpc_ * 0.8 + interval * 0.2;

    Vec2 output = scheduled_.front();
    for (std::size_t index = 1; index < scheduled_.size(); ++index) {
        scheduled_[index - 1] = scheduled_[index];
    }
    scheduled_.back() = {};

    if (input.x == 0.0F && input.y == 0.0F) {
        return output;
    }

    const auto estimated = static_cast<std::size_t>(std::llround(
        static_cast<double>(horizon_qpc_) / frame_interval_qpc_));
    const auto slots = (std::clamp)(estimated, std::size_t{1}, max_slots);
    const Vec2 portion{
        input.x / static_cast<float>(slots),
        input.y / static_cast<float>(slots),
    };
    output = add(output, portion);

    for (std::size_t index = 0; index + 1 < slots; ++index) {
        scheduled_[index] = add(scheduled_[index], portion);
    }

    const Vec2 rounding_residual{
        input.x - portion.x * static_cast<float>(slots),
        input.y - portion.y * static_cast<float>(slots),
    };
    if (slots == 1) {
        output = add(output, rounding_residual);
    } else {
        scheduled_[slots - 2] = add(scheduled_[slots - 2], rounding_residual);
    }
    return output;
}

}  // namespace mcfix
