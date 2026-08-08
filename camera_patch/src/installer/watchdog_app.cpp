#include "bootstrap/proxy_contract.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <filesystem>
#include <string>

namespace {

using Microsoft::WRL::ComPtr;

DWORD running_minecraft_pid() noexcept {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD result = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"Minecraft.Windows.exe") == 0) {
                result = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

std::filesystem::path sibling_patcher() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return std::filesystem::path(path).parent_path() / L"Patcher.exe";
}

void run_silent_repair() noexcept {
    const auto patcher = sibling_patcher();
    if (!std::filesystem::is_regular_file(patcher)) return;
    std::wstring command = L"\"" + patcher.native() + L"\" repair --silent";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(
            patcher.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, patcher.parent_path().c_str(),
            &startup, &process)) {
        WaitForSingleObject(process.hProcess, 60000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

bool install_is_healthy() noexcept {
    const auto patcher = sibling_patcher();
    if (!std::filesystem::is_regular_file(patcher)) return true;
    std::wstring command = L"\"" + patcher.native() + L"\" verify --silent";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            patcher.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, patcher.parent_path().c_str(),
            &startup, &process)) {
        return true;
    }
    const DWORD wait = WaitForSingleObject(process.hProcess, 60000);
    DWORD exit_code = 0;
    const bool completed = wait == WAIT_OBJECT_0 &&
        GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    // A timeout or query failure is uncertainty, not permission to launch a
    // second mutating process alongside the verifier.
    return !completed || exit_code == 0;
}

bool wait_for_heartbeat(DWORD process_id) noexcept {
    const auto name = mcfix::bootstrap::camera_patch_heartbeat_event_name(process_id);
    for (int attempt = 0; attempt < 60; ++attempt) {
        const HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, name.c_str());
        if (event != nullptr) {
            const bool signaled = WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
            CloseHandle(event);
            if (signaled) return true;
        }
        const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
        if (process == nullptr) return false;
        const bool exited = WaitForSingleObject(process, 250) == WAIT_OBJECT_0;
        CloseHandle(process);
        if (exited) return false;
    }
    return false;
}

void observe_process(DWORD process_id) noexcept {
    if (process_id == 0 || wait_for_heartbeat(process_id)) return;
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
    if (process != nullptr) {
        WaitForSingleObject(process, INFINITE);
        CloseHandle(process);
    }
    Sleep(1500);
    if (!install_is_healthy()) run_silent_repair();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t*, int) {
    const HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\MCFIX.CameraPatchWatchdog");
    if (mutex == nullptr) return 30;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    observe_process(running_minecraft_pid());

    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        CloseHandle(mutex);
        return 31;
    }
    const HRESULT security = CoInitializeSecurity(
        nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    if (FAILED(security) && security != RPC_E_TOO_LATE) {
        if (SUCCEEDED(initialized)) CoUninitialize();
        CloseHandle(mutex);
        return 32;
    }

    ComPtr<IWbemLocator> locator;
    ComPtr<IWbemServices> services;
    ComPtr<IEnumWbemClassObject> events;
    HRESULT status = CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(locator.GetAddressOf()));
    if (SUCCEEDED(status)) {
        status = locator->ConnectServer(
            const_cast<wchar_t*>(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr,
            0, nullptr, nullptr, services.GetAddressOf());
    }
    if (SUCCEEDED(status)) {
        status = CoSetProxyBlanket(
            services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
            RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    }
    if (SUCCEEDED(status)) {
        status = services->ExecNotificationQuery(
            const_cast<wchar_t*>(L"WQL"),
            const_cast<wchar_t*>(
                L"SELECT * FROM Win32_ProcessStartTrace WHERE ProcessName='Minecraft.Windows.exe'"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, events.GetAddressOf());
    }
    if (FAILED(status)) {
        if (SUCCEEDED(initialized)) CoUninitialize();
        CloseHandle(mutex);
        return 33;
    }

    for (;;) {
        ComPtr<IWbemClassObject> event;
        ULONG returned = 0;
        status = events->Next(WBEM_INFINITE, 1, event.GetAddressOf(), &returned);
        if (FAILED(status) || returned == 0) break;
        VARIANT value{};
        VariantInit(&value);
        if (SUCCEEDED(event->Get(L"ProcessID", 0, &value, nullptr, nullptr))) {
            DWORD process_id = 0;
            if (value.vt == VT_UI4) process_id = value.ulVal;
            if (value.vt == VT_I4 && value.lVal > 0) {
                process_id = static_cast<DWORD>(value.lVal);
            }
            observe_process(process_id);
        }
        VariantClear(&value);
    }

    if (SUCCEEDED(initialized)) CoUninitialize();
    CloseHandle(mutex);
    return 34;
}
