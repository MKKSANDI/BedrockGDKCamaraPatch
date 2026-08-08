#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace mcfix::installer {

std::string sha256_hex(std::string_view bytes);
std::string sha256_file(const std::filesystem::path& path);

}  // namespace mcfix::installer
