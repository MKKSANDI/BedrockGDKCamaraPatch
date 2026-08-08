#pragma once

#include <string>

namespace mcfix::installer {

struct ManifestInput {
    std::string identity_name;
    std::string publisher;
    std::string version;
    std::string main_package_name;
    std::string main_package_publisher;
    std::string architecture;
};

std::string render_modification_manifest(const ManifestInput& input);

}  // namespace mcfix::installer
