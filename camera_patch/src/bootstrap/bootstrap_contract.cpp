#include "bootstrap/bootstrap_contract.hpp"

namespace mcfix::bootstrap {

std::wstring heartbeat_event_name(std::uint32_t process_id) {
    return L"Local\\MCFIX.BootstrapProbe." + std::to_wstring(process_id);
}

std::wstring heartbeat_record_path(std::wstring_view local_app_data) {
    std::wstring path(local_app_data);
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    path += L"\\MCFIX\\probe-heartbeat.json";
    return path;
}

}  // namespace mcfix::bootstrap
