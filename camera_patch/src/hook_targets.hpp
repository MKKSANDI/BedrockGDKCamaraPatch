#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace mcfix {

inline constexpr std::string_view input_tick_pattern =
    "41 57 41 56 56 57 53 48 83 EC ? 0F 29 74 24 ? 66 0F 28 F1 48 89 CE "
    "F2 0F 11 89 ? ? ? ? 0F B6 81 ? ? ? ? 3C 03";
inline constexpr std::string_view turn_delta_pattern =
    "55 41 56 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 44 0F 29 5D ? "
    "44 0F 29 55 ? 44 0F 29 4D ? 44 0F 29 45 ? 0F 29 7D ? 0F 29 75 ? "
    "48 C7 45 ? ? ? ? ? 48 89 D7 48 89 CE 48 8B 89 ? ? ? ?";
inline constexpr std::string_view camera_update_pattern =
    "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC ? ? ? ? 44 0F 29 94 24 ? ? ? ? "
    "44 0F 29 8C 24 ? ? ? ? 44 0F 29 84 24 ? ? ? ? 0F 29 BC 24 ? ? ? ? "
    "0F 29 B4 24 ? ? ? ? 4C 89 C6";

enum class HookScanReason {
    none,
    input_tick_match_count,
    turn_delta_match_count,
    camera_update_match_count,
};

struct HookOffsets {
    std::size_t input_tick{};
    std::size_t turn_delta{};
    std::size_t camera_update{};
};

struct HookScanResult {
    HookScanReason reason{HookScanReason::input_tick_match_count};
    HookOffsets targets{};
    std::size_t input_tick_matches{};
    std::size_t turn_delta_matches{};
    std::size_t camera_update_matches{};
};

HookScanResult scan_hook_targets(std::span<const std::byte> text);
std::string_view hook_scan_reason_text(HookScanReason reason);

}  // namespace mcfix
