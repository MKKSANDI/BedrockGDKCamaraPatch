#include "bootstrap/bootstrap_contract.hpp"

#include <windows.h>

#include <array>
#include <charconv>
#include <string>
#include <vector>

namespace {

HANDLE heartbeat_event = nullptr;

std::wstring local_app_data() {
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::vector<wchar_t> buffer(required);
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), required) == 0) {
        return {};
    }
    return buffer.data();
}

std::string heartbeat_json(DWORD process_id) {
    std::array<char, 32> digits{};
    const auto conversion = std::to_chars(digits.data(), digits.data() + digits.size(), process_id);
    if (conversion.ec != std::errc{}) {
        return {};
    }
    std::string json = "{\"schema\":1,\"kind\":\"bootstrap-probe\",\"pid\":";
    json.append(digits.data(), conversion.ptr);
    json += ",\"hooks\":false}\n";
    return json;
}

DWORD WINAPI heartbeat_worker(void*) {
    const auto process_id = GetCurrentProcessId();
    const auto event_name = mcfix::bootstrap::heartbeat_event_name(process_id);
    heartbeat_event = CreateEventW(nullptr, TRUE, TRUE, event_name.c_str());

    const auto local = local_app_data();
    if (local.empty()) {
        return 1;
    }
    const std::wstring directory = local + L"\\MCFIX";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return 2;
    }

    const auto path = mcfix::bootstrap::heartbeat_record_path(local);
    const HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return 3;
    }

    const auto json = heartbeat_json(process_id);
    DWORD written = 0;
    const BOOL wrote = !json.empty() && json.size() <= 2048 &&
        WriteFile(file, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) &&
        written == json.size();
    CloseHandle(file);
    return wrote ? 0 : 4;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        const HANDLE worker = CreateThread(nullptr, 0, heartbeat_worker, nullptr, 0, nullptr);
        if (worker != nullptr) {
            CloseHandle(worker);
        }
    } else if (reason == DLL_PROCESS_DETACH && heartbeat_event != nullptr) {
        CloseHandle(heartbeat_event);
        heartbeat_event = nullptr;
    }
    return TRUE;
}
