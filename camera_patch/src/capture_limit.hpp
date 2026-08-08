#pragma once

#include "mcfix/telemetry.hpp"

#include <cstdint>

namespace mcfix {

enum class CaptureDecision {
    wait,
    record,
    stop,
};

class CaptureLimiter {
public:
    CaptureLimiter(std::uint64_t duration_qpc, std::uint64_t max_events);
    CaptureDecision observe(const TelemetryEvent& event);
    [[nodiscard]] bool started() const noexcept;

private:
    std::uint64_t duration_qpc_{};
    std::uint64_t max_events_{};
    std::uint64_t start_qpc_{};
    std::uint64_t captured_events_{};
    bool started_{false};
};

}  // namespace mcfix
