#pragma once

#include "installer/direct_plan.hpp"

#include <filesystem>
#include <string>

namespace mcfix::installer {

struct DirectTransactionResult {
    bool ok{};
    bool changed{};
    DirectLayout layout{DirectLayout::unknown_runtime};
    std::string error;
};

DirectInstallPlan inspect_direct_patch(
    const std::filesystem::path& target_root,
    const std::filesystem::path& proxy_payload,
    const std::filesystem::path& camera_payload,
    bool minecraft_running,
    const std::filesystem::path& known_installed_proxy = {});

DirectTransactionResult install_direct_patch(
    const std::filesystem::path& target_root,
    const std::filesystem::path& proxy_payload,
    const std::filesystem::path& camera_payload,
    bool minecraft_running,
    const std::filesystem::path& known_installed_proxy = {});

DirectTransactionResult uninstall_direct_patch(
    const std::filesystem::path& target_root,
    const std::filesystem::path& proxy_payload,
    bool minecraft_running);

}  // namespace mcfix::installer
