#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mcfix::bootstrap {

std::wstring heartbeat_event_name(std::uint32_t process_id);
std::wstring heartbeat_record_path(std::wstring_view local_app_data);

}  // namespace mcfix::bootstrap
