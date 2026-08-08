#include "capture_limit.hpp"

namespace mcfix {

CaptureLimiter::CaptureLimiter(std::uint64_t duration_qpc, std::uint64_t max_events)
    : duration_qpc_(duration_qpc), max_events_(max_events) {}

CaptureDecision CaptureLimiter::observe(const TelemetryEvent& event) {
    if (!started_) {
        const bool motion = event.kind == EventKind::turn_delta && (event.x != 0.0F || event.y != 0.0F);
        if (!motion) {
            return CaptureDecision::wait;
        }
        started_ = true;
        start_qpc_ = event.qpc;
    }

    ++captured_events_;
    if (captured_events_ >= max_events_) {
        return CaptureDecision::stop;
    }
    if (event.qpc >= start_qpc_ && event.qpc - start_qpc_ >= duration_qpc_) {
        return CaptureDecision::stop;
    }
    return CaptureDecision::record;
}

bool CaptureLimiter::started() const noexcept {
    return started_;
}

}  // namespace mcfix
