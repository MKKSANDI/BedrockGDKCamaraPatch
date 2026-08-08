#pragma once

#include "hook_targets.hpp"
#include "mcfix/telemetry.hpp"

#include <cstdint>
#include <atomic>
#include <string>
#include <string_view>

namespace mcfix {

using RuntimeTelemetryRing = TelemetryRing<65536>;

std::wstring telemetry_directory();
bool write_patch_status(
    bool active,
    std::string_view reason,
    const HookScanResult* scan,
    std::uint64_t qpc_frequency);
void run_telemetry_writer(
    RuntimeTelemetryRing& ring,
    std::uint64_t qpc_frequency,
    std::atomic<int>& writer_state,
    std::atomic<bool>& capture_enabled);

}  // namespace mcfix
