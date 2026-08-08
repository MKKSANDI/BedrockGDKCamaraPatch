#include "bootstrap/proxy_contract.hpp"

#include <windows.h>

#include <array>
#include <string>

namespace {

HANDLE heartbeat_event = nullptr;

DWORD WINAPI load_camera_patch(void* parameter) {
    const auto instance = static_cast<HMODULE>(parameter);
    std::array<wchar_t, 32768> module_path{};
    std::array<wchar_t, 32768> process_path{};
    const DWORD module_length = GetModuleFileNameW(
        instance, module_path.data(), static_cast<DWORD>(module_path.size()));
    const DWORD process_length = GetModuleFileNameW(
        nullptr, process_path.data(), static_cast<DWORD>(process_path.size()));
    if (module_length == 0 || module_length >= module_path.size() ||
        process_length == 0 || process_length >= process_path.size()) {
        return 1;
    }
    if (!mcfix::bootstrap::should_load_camera_patch(
            std::wstring_view(process_path.data(), process_length))) {
        return 0;
    }

    const auto heartbeat_name = mcfix::bootstrap::camera_patch_heartbeat_event_name(
        GetCurrentProcessId());
    heartbeat_event = CreateEventW(nullptr, TRUE, TRUE, heartbeat_name.c_str());

    const auto patch_path = mcfix::bootstrap::sibling_payload_path(
        std::wstring_view(module_path.data(), module_length),
        mcfix::bootstrap::camera_patch_name());
    const HMODULE patch = LoadLibraryExW(
        patch_path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    return patch == nullptr ? 2 : 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        const HANDLE worker = CreateThread(nullptr, 0, load_camera_patch, instance, 0, nullptr);
        if (worker != nullptr) {
            CloseHandle(worker);
        }
    }
    return TRUE;
}
