#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace mcfix::installer {

struct WatchdogTaskPolicy {
    std::wstring_view execution_time_limit;
    bool hidden;
    bool start_when_available;
    bool disallow_start_on_battery;
    bool stop_on_battery;
};

constexpr WatchdogTaskPolicy watchdog_task_policy() noexcept {
    return {
        .execution_time_limit = L"PT0S",
        .hidden = true,
        .start_when_available = true,
        .disallow_start_on_battery = false,
        .stop_on_battery = false,
    };
}

bool register_and_start_watchdog_task(
    const std::filesystem::path& watchdog,
    std::string& error) noexcept;
bool stop_and_remove_watchdog_task(std::string& error) noexcept;

}  // namespace mcfix::installer
