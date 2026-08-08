#include "installer/watchdog_task.hpp"

#include <windows.h>
#include <sddl.h>
#include <taskschd.h>
#include <wrl/client.h>
#include <comdef.h>

#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace mcfix::installer {
namespace {

using Microsoft::WRL::ComPtr;

std::wstring current_user_sid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD required = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &required);
    std::vector<std::byte> bytes(required);
    if (required == 0 || !GetTokenInformation(
            token, TokenUser, bytes.data(), required, &required)) {
        CloseHandle(token);
        return {};
    }
    CloseHandle(token);
    const auto* user = reinterpret_cast<const TOKEN_USER*>(bytes.data());
    wchar_t* sid = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &sid) || sid == nullptr) return {};
    std::wstring result(sid);
    LocalFree(sid);
    return result;
}

bool initialize_task_service(ComPtr<ITaskService>& service, bool& uninitialize) noexcept {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    uninitialize = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return false;
    if (FAILED(CoCreateInstance(
            CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(service.GetAddressOf())))) {
        if (uninitialize) CoUninitialize();
        uninitialize = false;
        return false;
    }
    const VARIANT empty{};
    const HRESULT connected = service->Connect(empty, empty, empty, empty);
    if (FAILED(connected)) {
        service.Reset();
        if (uninitialize) CoUninitialize();
        uninitialize = false;
        return false;
    }
    return true;
}

ComPtr<ITaskFolder> mcfix_folder(ITaskService* service, bool create) {
    ComPtr<ITaskFolder> root;
    ComPtr<ITaskFolder> folder;
    if (FAILED(service->GetFolder(_bstr_t(L"\\"), root.GetAddressOf()))) return folder;
    if (SUCCEEDED(root->GetFolder(_bstr_t(L"MCFIX"), folder.GetAddressOf()))) return folder;
    if (create) {
        const VARIANT empty{};
        static_cast<void>(root->CreateFolder(
            _bstr_t(L"MCFIX"), empty, folder.GetAddressOf()));
    }
    return folder;
}

std::string hresult_error(const char* operation, HRESULT status) {
    std::ostringstream message;
    message << operation << " failed: 0x" << std::hex << std::uppercase
            << static_cast<unsigned long>(status);
    return message.str();
}

}  // namespace

bool register_and_start_watchdog_task(
    const std::filesystem::path& watchdog,
    std::string& error) noexcept {
    try {
        if (!std::filesystem::is_regular_file(watchdog)) {
            error = "watchdog executable is missing";
            return false;
        }
        bool uninitialize = false;
        ComPtr<ITaskService> service;
        if (!initialize_task_service(service, uninitialize)) {
            error = "Task Scheduler service connection failed";
            return false;
        }
        const auto cleanup = [&]() { if (uninitialize) CoUninitialize(); };
        auto folder = mcfix_folder(service.Get(), true);
        if (!folder) {
            cleanup();
            error = "MCFIX Task Scheduler folder could not be created";
            return false;
        }
        ComPtr<ITaskDefinition> definition;
        HRESULT status = service->NewTask(0, definition.GetAddressOf());
        if (FAILED(status)) {
            cleanup();
            error = hresult_error("NewTask", status);
            return false;
        }

        ComPtr<IRegistrationInfo> registration;
        definition->get_RegistrationInfo(registration.GetAddressOf());
        if (registration) registration->put_Author(_bstr_t(L"MCFIX"));

        const auto policy = watchdog_task_policy();
        ComPtr<ITaskSettings> settings;
        definition->get_Settings(settings.GetAddressOf());
        if (!settings ||
            FAILED(settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW)) ||
            FAILED(settings->put_ExecutionTimeLimit(
                _bstr_t(policy.execution_time_limit.data()))) ||
            FAILED(settings->put_Hidden(policy.hidden ? VARIANT_TRUE : VARIANT_FALSE)) ||
            FAILED(settings->put_StartWhenAvailable(
                policy.start_when_available ? VARIANT_TRUE : VARIANT_FALSE)) ||
            FAILED(settings->put_DisallowStartIfOnBatteries(
                policy.disallow_start_on_battery ? VARIANT_TRUE : VARIANT_FALSE)) ||
            FAILED(settings->put_StopIfGoingOnBatteries(
                policy.stop_on_battery ? VARIANT_TRUE : VARIANT_FALSE))) {
            cleanup();
            error = "watchdog task settings could not be applied";
            return false;
        }

        const auto sid = current_user_sid();
        if (sid.empty()) {
            cleanup();
            error = "current user SID is unavailable";
            return false;
        }
        ComPtr<IPrincipal> principal;
        definition->get_Principal(principal.GetAddressOf());
        if (!principal || FAILED(principal->put_UserId(_bstr_t(sid.c_str()))) ||
            FAILED(principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN)) ||
            FAILED(principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST))) {
            cleanup();
            error = "watchdog task principal could not be applied";
            return false;
        }

        ComPtr<ITriggerCollection> triggers;
        definition->get_Triggers(triggers.GetAddressOf());
        ComPtr<ITrigger> trigger;
        if (!triggers || FAILED(triggers->Create(
                TASK_TRIGGER_LOGON, trigger.GetAddressOf()))) {
            cleanup();
            error = "watchdog logon trigger could not be created";
            return false;
        }
        ComPtr<ILogonTrigger> logon_trigger;
        if (FAILED(trigger.As(&logon_trigger)) ||
            FAILED(logon_trigger->put_UserId(_bstr_t(sid.c_str())))) {
            cleanup();
            error = "watchdog logon trigger user could not be bound";
            return false;
        }

        ComPtr<IActionCollection> actions;
        definition->get_Actions(actions.GetAddressOf());
        ComPtr<IAction> action;
        if (!actions || FAILED(actions->Create(TASK_ACTION_EXEC, action.GetAddressOf()))) {
            cleanup();
            error = "watchdog execution action could not be created";
            return false;
        }
        ComPtr<IExecAction> execute;
        if (FAILED(action.As(&execute)) ||
            FAILED(execute->put_Path(_bstr_t(watchdog.c_str()))) ||
            FAILED(execute->put_WorkingDirectory(_bstr_t(watchdog.parent_path().c_str())))) {
            cleanup();
            error = "watchdog executable action could not be configured";
            return false;
        }

        const VARIANT empty{};
        ComPtr<IRegisteredTask> task;
        status = folder->RegisterTaskDefinition(
            _bstr_t(L"CameraPatchWatchdog"), definition.Get(), TASK_CREATE_OR_UPDATE,
            empty, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, task.GetAddressOf());
        if (FAILED(status) || !task) {
            cleanup();
            error = hresult_error("RegisterTaskDefinition", status);
            return false;
        }
        TASK_STATE task_state = TASK_STATE_UNKNOWN;
        if (SUCCEEDED(task->get_State(&task_state)) && task_state == TASK_STATE_RUNNING) {
            status = task->Stop(0);
            if (FAILED(status)) {
                cleanup();
                error = hresult_error("Stop", status);
                return false;
            }
            for (unsigned attempt = 0; attempt < 50; ++attempt) {
                Sleep(100);
                task_state = TASK_STATE_UNKNOWN;
                if (FAILED(task->get_State(&task_state)) ||
                    task_state != TASK_STATE_RUNNING) {
                    break;
                }
            }
            // Task Scheduler can report Ready just before the prior process
            // releases the watchdog's single-instance mutex.
            Sleep(250);
        }
        ComPtr<IRunningTask> running;
        status = task->Run(empty, running.GetAddressOf());
        if (FAILED(status)) {
            cleanup();
            error = hresult_error("Run", status);
            return false;
        }
        cleanup();
        return true;
    } catch (...) {
        error = "unexpected Task Scheduler registration failure";
        return false;
    }
}

bool stop_and_remove_watchdog_task(std::string& error) noexcept {
    bool uninitialize = false;
    ComPtr<ITaskService> service;
    if (!initialize_task_service(service, uninitialize)) {
        error = "Task Scheduler service connection failed";
        return false;
    }
    auto folder = mcfix_folder(service.Get(), false);
    if (!folder) {
        if (uninitialize) CoUninitialize();
        return true;
    }
    ComPtr<IRegisteredTask> task;
    if (SUCCEEDED(folder->GetTask(_bstr_t(L"CameraPatchWatchdog"), task.GetAddressOf())) && task) {
        static_cast<void>(task->Stop(0));
    }
    const HRESULT status = folder->DeleteTask(_bstr_t(L"CameraPatchWatchdog"), 0);
    if (uninitialize) CoUninitialize();
    if (FAILED(status) && status != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
        error = hresult_error("DeleteTask", status);
        return false;
    }
    return true;
}

}  // namespace mcfix::installer
