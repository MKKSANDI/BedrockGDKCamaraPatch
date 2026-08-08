#include <windows.h>
#include <tlhelp32.h>
#include <appmodel.h>

#include <MinHook.h>

#include "hook_callbacks.hpp"
#include "hook_targets.hpp"
#include "mcfix/target_policy.hpp"
#include "telemetry_writer.hpp"
#include "turn_distributor.hpp"
#include "cadence_classifier.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace {

mcfix::RuntimeTelemetryRing telemetry_ring;
mcfix::InputTickOriginal original_input_tick = nullptr;
mcfix::TurnDeltaOriginal original_turn_delta = nullptr;
mcfix::CameraUpdateOriginal original_camera_update = nullptr;
std::uint64_t qpc_frequency = 0;
std::atomic<int> writer_state{0};
std::atomic<bool> capture_enabled{true};
std::atomic<bool> capture_started{false};

std::uint64_t current_qpc() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}

std::uint32_t current_thread_id() {
    return GetCurrentThreadId();
}

void record_event(const mcfix::TelemetryEvent& event) {
    telemetry_ring.push(event);
}

void* input_tick_hook(void* a1, void* a2, void* a3, void* a4) {
    if (!capture_enabled.load(std::memory_order_relaxed) ||
        !capture_started.load(std::memory_order_acquire)) {
        return original_input_tick(a1, a2, a3, a4);
    }
    return mcfix::forward_input_tick(
        original_input_tick, record_event, current_qpc, current_thread_id, a1, a2, a3, a4);
}

void turn_delta_hook(void* object, mcfix::Vec2& delta) {
    const auto qpc = current_qpc();
    const auto thread_id = current_thread_id();
    const auto input = delta;
    thread_local mcfix::TurnDistributor distributor(qpc_frequency);
    thread_local mcfix::CadenceClassifier classifier(qpc_frequency);
    if (classifier.observe(qpc, input)) {
        delta = distributor.distribute(object, qpc, input);
    } else {
        static_cast<void>(distributor.distribute(object, qpc, {}));
        delta = input;
    }

    if (capture_enabled.load(std::memory_order_relaxed)) {
        if (!capture_started.load(std::memory_order_relaxed) &&
            (input.x != 0.0F || input.y != 0.0F)) {
            capture_started.store(true, std::memory_order_release);
        }
        if (capture_started.load(std::memory_order_acquire)) {
            record_event(mcfix::TelemetryEvent{
                .qpc = qpc,
                .thread_id = thread_id,
                .kind = mcfix::EventKind::turn_delta,
                .x = input.x,
                .y = input.y,
            });
            record_event(mcfix::TelemetryEvent{
                .qpc = qpc,
                .thread_id = thread_id,
                .kind = mcfix::EventKind::turn_distributed,
                .x = delta.x,
                .y = delta.y,
            });
        }
    }
    original_turn_delta(object, delta);
}

void* camera_update_hook(void* camera, void* a2, void* a3) {
    if (!capture_enabled.load(std::memory_order_relaxed) ||
        !capture_started.load(std::memory_order_acquire)) {
        return original_camera_update(camera, a2, a3);
    }
    return mcfix::forward_camera_update(
        original_camera_update, record_event, current_qpc, current_thread_id, camera, a2, a3);
}

std::wstring current_package_name() {
    UINT32 length = 0;
    const auto first = GetCurrentPackageFullName(&length, nullptr);
    if (first != ERROR_INSUFFICIENT_BUFFER || length == 0) {
        return {};
    }
    std::wstring package(length, L'\0');
    if (GetCurrentPackageFullName(&length, package.data()) != ERROR_SUCCESS) {
        return {};
    }
    if (!package.empty() && package.back() == L'\0') {
        package.pop_back();
    }
    return package;
}

std::wstring lowercase_basename(std::wstring_view path) {
    const auto slash = path.find_last_of(L"/\\");
    std::wstring name(slash == std::wstring_view::npos ? path : path.substr(slash + 1));
    for (auto& value : name) {
        value = static_cast<wchar_t>(towlower(value));
    }
    return name;
}

bool has_conflicting_module() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        return true;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool conflict = false;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            const auto name = lowercase_basename(entry.szModule);
            if (name == L"flarial.client.release.dll" || name == L"flarial.dll" ||
                name == L"fix.dll" || name == L"cam_final.dll") {
                conflict = true;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    } else {
        conflict = true;
    }
    CloseHandle(snapshot);
    return conflict;
}

std::span<const std::byte> executable_text_section() {
    const auto module = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (module == nullptr) {
        return {};
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return {};
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return {};
    }

    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
        const std::string_view name(reinterpret_cast<const char*>(section->Name),
                                    strnlen_s(reinterpret_cast<const char*>(section->Name), IMAGE_SIZEOF_SHORT_NAME));
        if (name == ".text" && (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
            return {module + section->VirtualAddress, section->Misc.VirtualSize};
        }
    }
    return {};
}

void remove_created_hooks(void* input, void* turn, void* camera) {
    MH_DisableHook(MH_ALL_HOOKS);
    if (input != nullptr) {
        MH_RemoveHook(input);
    }
    if (turn != nullptr) {
        MH_RemoveHook(turn);
    }
    if (camera != nullptr) {
        MH_RemoveHook(camera);
    }
    MH_Uninitialize();
}

DWORD WINAPI telemetry_writer_thread(void*) {
    mcfix::run_telemetry_writer(
        telemetry_ring, qpc_frequency, writer_state, capture_enabled);
    return 0;
}

DWORD WINAPI initialize_patch(void*) {
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency)) {
        qpc_frequency = static_cast<std::uint64_t>(frequency.QuadPart);
    }

    if (!mcfix::is_supported_package_full_name(current_package_name())) {
        mcfix::write_patch_status(false, "in-process package identity mismatch", nullptr, qpc_frequency);
        return 0;
    }
    if (has_conflicting_module()) {
        mcfix::write_patch_status(false, "conflicting hook module detected", nullptr, qpc_frequency);
        return 0;
    }

    const auto text = executable_text_section();
    if (text.empty()) {
        mcfix::write_patch_status(false, "valid x64 executable .text section not found", nullptr, qpc_frequency);
        return 0;
    }
    const auto scan = mcfix::scan_hook_targets(text);
    if (scan.reason != mcfix::HookScanReason::none) {
        mcfix::write_patch_status(false, mcfix::hook_scan_reason_text(scan.reason), &scan, qpc_frequency);
        return 0;
    }

    auto* input_target = const_cast<std::byte*>(text.data()) + scan.targets.input_tick;
    auto* turn_target = const_cast<std::byte*>(text.data()) + scan.targets.turn_delta;
    auto* camera_target = const_cast<std::byte*>(text.data()) + scan.targets.camera_update;

    if (MH_Initialize() != MH_OK) {
        mcfix::write_patch_status(false, "MH_Initialize failed", &scan, qpc_frequency);
        return 0;
    }

    void* created_input = nullptr;
    void* created_turn = nullptr;
    void* created_camera = nullptr;
    if (MH_CreateHook(input_target, reinterpret_cast<void*>(input_tick_hook),
                      reinterpret_cast<void**>(&original_input_tick)) != MH_OK) {
        remove_created_hooks(created_input, created_turn, created_camera);
        mcfix::write_patch_status(false, "failed to create InputHandler::tick hook", &scan, qpc_frequency);
        return 0;
    }
    created_input = input_target;
    if (MH_CreateHook(turn_target, reinterpret_cast<void*>(turn_delta_hook),
                      reinterpret_cast<void**>(&original_turn_delta)) != MH_OK) {
        remove_created_hooks(created_input, created_turn, created_camera);
        mcfix::write_patch_status(false, "failed to create LocalPlayer::applyTurnDelta hook", &scan, qpc_frequency);
        return 0;
    }
    created_turn = turn_target;
    if (MH_CreateHook(camera_target, reinterpret_cast<void*>(camera_update_hook),
                      reinterpret_cast<void**>(&original_camera_update)) != MH_OK) {
        remove_created_hooks(created_input, created_turn, created_camera);
        mcfix::write_patch_status(false, "failed to create MinecraftCamera::updateCamera hook", &scan, qpc_frequency);
        return 0;
    }
    created_camera = camera_target;

    if (MH_QueueEnableHook(input_target) != MH_OK ||
        MH_QueueEnableHook(turn_target) != MH_OK ||
        MH_QueueEnableHook(camera_target) != MH_OK ||
        MH_ApplyQueued() != MH_OK) {
        remove_created_hooks(created_input, created_turn, created_camera);
        mcfix::write_patch_status(false, "failed to enable the complete hook batch", &scan, qpc_frequency);
        return 0;
    }

    const HANDLE writer = CreateThread(nullptr, 0, telemetry_writer_thread, nullptr, 0, nullptr);
    if (writer == nullptr) {
        remove_created_hooks(created_input, created_turn, created_camera);
        mcfix::write_patch_status(false, "failed to start telemetry writer", &scan, qpc_frequency);
        return 0;
    }
    CloseHandle(writer);
    for (int attempt = 0; attempt < 200 && writer_state.load(std::memory_order_acquire) == 0; ++attempt) {
        Sleep(10);
    }
    if (writer_state.load(std::memory_order_acquire) != 1) {
        remove_created_hooks(created_input, created_turn, created_camera);
        mcfix::write_patch_status(false, "telemetry writer could not create its CSV artifact", &scan, qpc_frequency);
        return 0;
    }
    mcfix::write_patch_status(
        true,
        "cadence classifier armed; turn distribution activates only for repeated coarse batches",
        &scan,
        qpc_frequency);
    return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        const HANDLE worker = CreateThread(nullptr, 0, initialize_patch, nullptr, 0, nullptr);
        if (worker != nullptr) {
            CloseHandle(worker);
        }
    }
    return TRUE;
}
