#include "bootstrap/bootstrap_contract.hpp"

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::filesystem::path heartbeat_path() {
    DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::vector<wchar_t> buffer(required);
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), required) == 0) {
        return {};
    }
    return mcfix::bootstrap::heartbeat_record_path(buffer.data());
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::wcerr << L"usage: MCFIXBootstrapProbeHost.exe <absolute-dll-path>\n";
        return 2;
    }

    const std::filesystem::path dll_path(argv[1]);
    if (!dll_path.is_absolute() || !std::filesystem::is_regular_file(dll_path)) {
        std::wcerr << L"bridge path must be an existing absolute file\n";
        return 3;
    }

    const auto record = heartbeat_path();
    if (record.empty()) {
        std::wcerr << L"LOCALAPPDATA is unavailable\n";
        return 4;
    }
    std::error_code error;
    std::filesystem::remove(record, error);

    const auto module = LoadLibraryW(dll_path.c_str());
    if (module == nullptr) {
        std::wcerr << L"LoadLibraryW failed: " << GetLastError() << L'\n';
        return 5;
    }

    const auto handler = GetProcAddress(module, "__CxxFrameHandler4");
    if (handler == nullptr) {
        std::wcerr << L"bridge is missing __CxxFrameHandler4\n";
        FreeLibrary(module);
        return 6;
    }

    const auto event_name = mcfix::bootstrap::heartbeat_event_name(GetCurrentProcessId());
    HANDLE event = nullptr;
    for (int attempt = 0; attempt < 100 && event == nullptr; ++attempt) {
        event = OpenEventW(SYNCHRONIZE, FALSE, event_name.c_str());
        if (event == nullptr) {
            Sleep(10);
        }
    }
    if (event == nullptr || WaitForSingleObject(event, 1000) != WAIT_OBJECT_0) {
        std::wcerr << L"bridge heartbeat event was not signaled\n";
        if (event != nullptr) CloseHandle(event);
        FreeLibrary(module);
        return 7;
    }
    CloseHandle(event);

    bool record_ready = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (std::filesystem::is_regular_file(record) &&
            std::filesystem::file_size(record) > 0 &&
            std::filesystem::file_size(record) <= 2048) {
            record_ready = true;
            break;
        }
        Sleep(10);
    }

    FreeLibrary(module);
    if (!record_ready) {
        std::wcerr << L"bounded heartbeat record was not created\n";
        return 8;
    }
    std::wcout << L"bootstrap probe heartbeat verified\n";
    return 0;
}
