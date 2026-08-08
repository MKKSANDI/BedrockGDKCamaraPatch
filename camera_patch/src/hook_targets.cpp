#include "hook_targets.hpp"

#include "mcfix/signature.hpp"

namespace mcfix {

HookScanResult scan_hook_targets(std::span<const std::byte> text) {
    HookScanResult result;
    const auto input_matches = find_all(text, parse_pattern(input_tick_pattern));
    const auto turn_matches = find_all(text, parse_pattern(turn_delta_pattern));
    const auto camera_matches = find_all(text, parse_pattern(camera_update_pattern));
    result.input_tick_matches = input_matches.size();
    result.turn_delta_matches = turn_matches.size();
    result.camera_update_matches = camera_matches.size();

    if (input_matches.size() != 1) {
        result.reason = HookScanReason::input_tick_match_count;
        return result;
    }
    if (turn_matches.size() != 1) {
        result.reason = HookScanReason::turn_delta_match_count;
        return result;
    }
    if (camera_matches.size() != 1) {
        result.reason = HookScanReason::camera_update_match_count;
        return result;
    }

    result.reason = HookScanReason::none;
    result.targets = HookOffsets{
        .input_tick = input_matches.front(),
        .turn_delta = turn_matches.front(),
        .camera_update = camera_matches.front(),
    };
    return result;
}

std::string_view hook_scan_reason_text(HookScanReason reason) {
    switch (reason) {
        case HookScanReason::none:
            return "accepted";
        case HookScanReason::input_tick_match_count:
            return "InputHandler::tick did not match exactly once";
        case HookScanReason::turn_delta_match_count:
            return "LocalPlayer::applyTurnDelta did not match exactly once";
        case HookScanReason::camera_update_match_count:
            return "MinecraftCamera::updateCamera did not match exactly once";
    }
    return "unknown hook-scan result";
}

}  // namespace mcfix
