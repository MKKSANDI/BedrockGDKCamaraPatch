#include "mcfix/target_policy.hpp"

#include <algorithm>
#include <cwctype>

namespace mcfix {
namespace {

std::wstring lower(std::wstring_view text) {
    std::wstring result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return result;
}

}  // namespace

bool is_supported_package_full_name(std::wstring_view package_full_name) {
    const auto value = lower(package_full_name);
    const auto prefix = lower(required_package_name) + L"_";
    const auto suffix = L"_x64__" + lower(required_package_publisher_id);
    if (!value.starts_with(prefix) || !value.ends_with(suffix) ||
        value.size() <= prefix.size() + suffix.size()) {
        return false;
    }
    const auto version = value.substr(
        prefix.size(), value.size() - prefix.size() - suffix.size());
    std::size_t parts = 1;
    bool digit_in_part = false;
    for (const auto character : version) {
        if (character >= L'0' && character <= L'9') {
            digit_in_part = true;
        } else if (character == L'.' && digit_in_part && parts < 4) {
            ++parts;
            digit_in_part = false;
        } else {
            return false;
        }
    }
    return parts == 4 && digit_in_part;
}

TargetDecision evaluate_target(const TargetFacts& facts) {
    if (facts.process_count != 1) {
        return {false, TargetReason::process_count};
    }
    if (lower(facts.executable_name) != lower(required_executable)) {
        return {false, TargetReason::executable_mismatch};
    }
    if (facts.package_full_name.empty()) {
        return {false, TargetReason::package_missing};
    }
    if (!is_supported_package_full_name(facts.package_full_name)) {
        return {false, TargetReason::package_mismatch};
    }
    return {true, TargetReason::none};
}

std::string_view target_reason_text(TargetReason reason) {
    switch (reason) {
        case TargetReason::none:
            return "accepted";
        case TargetReason::process_count:
            return "expected exactly one Minecraft.Windows.exe process";
        case TargetReason::executable_mismatch:
            return "target executable name is not Minecraft.Windows.exe";
        case TargetReason::package_missing:
            return "target process has no package identity";
        case TargetReason::package_mismatch:
            return "target package is not Minecraft 1.26.4201.0 x64";
    }
    return "unknown target-policy result";
}

}  // namespace mcfix
