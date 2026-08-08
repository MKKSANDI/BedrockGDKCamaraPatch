#pragma once

#include <cstddef>
#include <filesystem>

namespace mcfix::installer {

struct RuntimeContract {
    bool valid_x64_dll{};
    std::size_t named_export_count{};
    std::size_t handler_export_count{};
    bool handler_is_forwarded{};

    bool looks_original() const noexcept {
        return valid_x64_dll && named_export_count == 1 && handler_export_count == 1 &&
               !handler_is_forwarded;
    }
    bool looks_forwarding_proxy() const noexcept {
        return valid_x64_dll && named_export_count == 1 && handler_export_count == 1 &&
               handler_is_forwarded;
    }
};

RuntimeContract inspect_runtime_contract(const std::filesystem::path& path);

}  // namespace mcfix::installer
