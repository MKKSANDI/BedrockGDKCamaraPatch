#pragma once

#include "installer/package_state.hpp"

#include <filesystem>
#include <string>

namespace mcfix::installer {

struct DiscoveredPackage {
    bool found{};
    std::wstring full_name;
    std::wstring publisher;
    std::filesystem::path install_location;
    PackageFacts facts;
};

DiscoveredPackage discover_minecraft_package();
std::string package_state_name(PackageState state);

}  // namespace mcfix::installer
