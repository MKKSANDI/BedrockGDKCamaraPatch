#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mcfix {

inline constexpr std::wstring_view required_executable = L"Minecraft.Windows.exe";
inline constexpr std::wstring_view required_package_name = L"Microsoft.MinecraftUWP";
inline constexpr std::wstring_view required_package_publisher_id = L"8wekyb3d8bbwe";

enum class TargetReason {
    none,
    process_count,
    executable_mismatch,
    package_missing,
    package_mismatch,
};

struct TargetFacts {
    std::size_t process_count{};
    std::wstring executable_name;
    std::wstring package_full_name;
    bool modules_enumerated{false};
    std::vector<std::wstring> loaded_modules;
};

struct TargetDecision {
    bool allowed{false};
    TargetReason reason{TargetReason::process_count};
};

TargetDecision evaluate_target(const TargetFacts& facts);
bool is_supported_package_full_name(std::wstring_view package_full_name);
std::string_view target_reason_text(TargetReason reason);

}  // namespace mcfix
