#include "installer/package_state.hpp"

namespace mcfix::installer {

PackageState classify_package_state(const PackageFacts& facts) noexcept {
    if (facts.minecraft_running) {
        return PackageState::minecraft_running;
    }
    if (facts.package_name != L"Microsoft.MinecraftUWP") {
        return PackageState::wrong_package;
    }
    if (facts.architecture != L"x64") {
        return PackageState::wrong_architecture;
    }
    if (!facts.bridge_exists) {
        return PackageState::missing_bridge;
    }
    if (facts.orig_exists) {
        return PackageState::existing_unmanaged_proxy;
    }
    if (facts.expected_store_bridge_hash.empty() ||
        facts.bridge_hash != facts.expected_store_bridge_hash) {
        return PackageState::unknown_bridge;
    }
    return PackageState::clean;
}

}  // namespace mcfix::installer
