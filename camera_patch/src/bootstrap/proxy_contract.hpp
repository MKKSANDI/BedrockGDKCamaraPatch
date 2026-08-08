#pragma once

#include <string>
#include <string_view>

namespace mcfix::bootstrap {

std::wstring_view original_runtime_name() noexcept;
std::wstring_view camera_patch_name() noexcept;
std::wstring sibling_payload_path(
    std::wstring_view proxy_module_path,
    std::wstring_view payload_name);
bool should_load_camera_patch(std::wstring_view process_path) noexcept;
std::wstring camera_patch_heartbeat_event_name(unsigned long process_id);

}  // namespace mcfix::bootstrap
