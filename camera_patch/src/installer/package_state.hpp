#pragma once

#include <string>

namespace mcfix::installer {

struct PackageFacts {
    std::wstring package_name;
    std::wstring package_version;
    std::wstring architecture;
    bool minecraft_running{};
    bool bridge_exists{};
    std::wstring bridge_hash;
    std::wstring expected_store_bridge_hash;
    bool orig_exists{};
    std::wstring orig_hash;
};

enum class PackageState {
    clean,
    minecraft_running,
    wrong_package,
    wrong_architecture,
    missing_bridge,
    existing_unmanaged_proxy,
    unknown_bridge,
};

PackageState classify_package_state(const PackageFacts& facts) noexcept;

}  // namespace mcfix::installer
