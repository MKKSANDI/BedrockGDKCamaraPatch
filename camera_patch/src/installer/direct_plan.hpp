#pragma once

#include <vector>

namespace mcfix::installer {

struct DirectInstallFacts {
    bool minecraft_running{};
    bool proxy_payload_exists{};
    bool proxy_payload_looks_forwarder{};
    bool camera_payload_exists{};
    bool current_runtime_exists{};
    bool current_runtime_is_proxy_payload{};
    bool current_runtime_is_known_managed_proxy{};
    bool current_runtime_looks_original{};
    bool managed_original_exists{};
    bool managed_original_looks_original{};
    bool legacy_original_exists{};
    bool legacy_original_looks_original{};
    bool installed_camera_exists{};
    bool installed_camera_matches_payload{};
};

enum class DirectLayout {
    clean_store,
    legacy_proxy,
    managed_healthy,
    managed_needs_repair,
    minecraft_running,
    payload_missing,
    invalid_proxy_payload,
    missing_runtime,
    invalid_managed_original,
    invalid_legacy_original,
    unknown_runtime,
};

enum class DirectInstallOperation {
    preserve_current_runtime,
    preserve_legacy_original,
    install_proxy,
    install_camera_patch,
    write_install_state,
};

struct DirectInstallPlan {
    bool allowed{};
    DirectLayout layout{DirectLayout::unknown_runtime};
    std::vector<DirectInstallOperation> operations;
};

DirectInstallPlan make_direct_install_plan(const DirectInstallFacts& facts);
const char* direct_layout_name(DirectLayout layout) noexcept;

}  // namespace mcfix::installer
