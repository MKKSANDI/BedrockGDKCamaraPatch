#include "bootstrap/proxy_contract.hpp"

#include <cwctype>

namespace mcfix::bootstrap {

std::wstring_view original_runtime_name() noexcept {
    return L"vcruntime140_1_mcfix_original.dll";
}

std::wstring_view camera_patch_name() noexcept {
    return L"MCFIXCameraPatch.dll";
}

std::wstring sibling_payload_path(
    std::wstring_view proxy_module_path,
    std::wstring_view payload_name) {
    const auto separator = proxy_module_path.find_last_of(L"/\\");
    std::wstring result(
        separator == std::wstring_view::npos
            ? std::wstring_view{}
            : proxy_module_path.substr(0, separator + 1));
    result.append(payload_name);
    return result;
}

bool should_load_camera_patch(std::wstring_view process_path) noexcept {
    const auto separator = process_path.find_last_of(L"/\\");
    const auto name = separator == std::wstring_view::npos
        ? process_path
        : process_path.substr(separator + 1);
    constexpr std::wstring_view expected = L"minecraft.windows.exe";
    if (name.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < name.size(); ++index) {
        if (std::towlower(name[index]) != expected[index]) {
            return false;
        }
    }
    return true;
}

std::wstring camera_patch_heartbeat_event_name(unsigned long process_id) {
    return L"Local\\MCFIX.CameraPatch." + std::to_wstring(process_id);
}

}  // namespace mcfix::bootstrap
