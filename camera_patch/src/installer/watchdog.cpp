#include "installer/shipping_installer.hpp"

#include "bootstrap/proxy_contract.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>

namespace mcfix::installer {
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

void repair_after_exit(DWORD process_id) noexcept {
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
    if (process != nullptr) {
        WaitForSingleObject(process, INFINITE);
        CloseHandle(process);
    }
    Sleep(1500);
    static_cast<void>(shipping_install(true, true));
}

void observe_process(DWORD process_id) noexcept {
    if (process_id == 0 || wait_for_heartbeat(process_id)) return;
    repair_after_exit(process_id);
}

bool configure_wmi_proxy(IWbemServices* services) noexcept {
    return SUCCEEDED(CoSetProxyBlanket(
        services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE));
}

}  // namespace

int shipping_watch() {
    const HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\MCFIX.CameraPatchWatchdog");
    if (mutex == nullptr) return 30;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    static_cast<void>(shipping_install(true, true));
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
            BSTR(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0, nullptr, nullptr,
            services.GetAddressOf());
    }
    if (SUCCEEDED(status) && !configure_wmi_proxy(services.Get())) {
        status = E_ACCESSDENIED;
    }
    if (SUCCEEDED(status)) {
        status = services->ExecNotificationQuery(
            BSTR(L"WQL"),
            BSTR(L"SELECT * FROM Win32_ProcessStartTrace WHERE ProcessName='Minecraft.Windows.exe'"),
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

}  // namespace mcfix::installer
