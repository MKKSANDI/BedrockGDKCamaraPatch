#include "telemetry_writer.hpp"
#include "capture_limit.hpp"

#include <windows.h>

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

namespace mcfix {
namespace {

constexpr std::wstring_view package_family = L"Microsoft.MinecraftUWP_8wekyb3d8bbwe";

bool write_bytes(HANDLE file, std::string_view bytes) {
    while (!bytes.empty()) {
        DWORD written = 0;
        const auto chunk = static_cast<DWORD>((std::min)(bytes.size(), static_cast<std::size_t>(0xFFFFFFFFu)));
        if (!WriteFile(file, bytes.data(), chunk, &written, nullptr) || written == 0) {
            return false;
        }
        bytes.remove_prefix(written);
    }
    return true;
}

std::wstring artifact_path(std::wstring_view name) {
    auto directory = telemetry_directory();
    if (directory.empty()) {
        return {};
    }
    directory.push_back(L'\\');
    directory.append(name);
    return directory;
}

}  // namespace

std::wstring telemetry_directory() {
    DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::wstring local_app_data(required, L'\0');
    const auto written = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data.data(), required);
    if (written == 0 || written >= required) {
        return {};
    }
    local_app_data.resize(written);

    std::filesystem::path path(local_app_data);
    path /= L"Packages";
    path /= package_family;
    path /= L"LocalState";
    path /= L"MCFIX";

    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        return {};
    }
    return path.wstring();
}

bool write_patch_status(
    bool active,
    std::string_view reason,
    const HookScanResult* scan,
    std::uint64_t qpc_frequency) {
    const auto path = artifact_path(L"patch-status.json");
    if (path.empty()) {
        return false;
    }
    const HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::ostringstream json;
    json << "{\n"
         << "  \"active\": " << (active ? "true" : "false") << ",\n"
         << "  \"process_id\": " << GetCurrentProcessId() << ",\n"
         << "  \"package\": \"Microsoft.MinecraftUWP_1.26.4201.0_x64__8wekyb3d8bbwe\",\n"
         << "  \"reason\": \"" << reason << "\",\n"
         << "  \"correction\": \"20ms_total_preserving_turn_distributor\",\n"
         << "  \"distribution_max_calls\": 8,\n"
         << "  \"capture_seconds_after_motion\": 90,\n"
         << "  \"capture_event_cap\": 200000,\n"
         << "  \"qpc_frequency\": " << qpc_frequency << ",\n"
         << "  \"input_tick_matches\": " << (scan ? scan->input_tick_matches : 0) << ",\n"
         << "  \"turn_delta_matches\": " << (scan ? scan->turn_delta_matches : 0) << ",\n"
         << "  \"camera_update_matches\": " << (scan ? scan->camera_update_matches : 0) << ",\n"
         << "  \"input_tick_offset\": " << (scan ? scan->targets.input_tick : 0) << ",\n"
         << "  \"turn_delta_offset\": " << (scan ? scan->targets.turn_delta : 0) << ",\n"
         << "  \"camera_update_offset\": " << (scan ? scan->targets.camera_update : 0) << "\n"
         << "}\n";
    const auto content = json.str();
    const auto ok = write_bytes(file, content);
    FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
}

void run_telemetry_writer(
    RuntimeTelemetryRing& ring,
    std::uint64_t qpc_frequency,
    std::atomic<int>& writer_state,
    std::atomic<bool>& capture_enabled) {
    const auto path = artifact_path(L"camera-telemetry.csv");
    if (path.empty()) {
        writer_state.store(-1, std::memory_order_release);
        return;
    }
    const HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        writer_state.store(-1, std::memory_order_release);
        return;
    }

    if (!write_bytes(file, "sequence,qpc,qpc_seconds,thread_id,event,x,y,dropped_total\r\n")) {
        CloseHandle(file);
        writer_state.store(-1, std::memory_order_release);
        return;
    }
    writer_state.store(1, std::memory_order_release);
    CaptureLimiter limiter(qpc_frequency * 90, 200000);
    const auto absolute_deadline = GetTickCount64() + 10 * 60 * 1000;
    std::uint32_t idle_cycles = 0;
    for (;;) {
        std::ostringstream batch;
        batch << std::setprecision(9);
        bool keep_capturing = true;
        const auto drained = ring.drain([&](const TelemetryEvent& event) {
            if (!keep_capturing) {
                return;
            }
            const auto decision = limiter.observe(event);
            if (decision == CaptureDecision::wait) {
                return;
            }
            if (decision == CaptureDecision::stop) {
                keep_capturing = false;
                return;
            }
            const auto seconds = qpc_frequency == 0
                ? 0.0
                : static_cast<double>(event.qpc) / static_cast<double>(qpc_frequency);
            batch << event.sequence << ',' << event.qpc << ',' << seconds << ','
                  << event.thread_id << ',' << event_kind_name(event.kind) << ','
                  << event.x << ',' << event.y << ',' << ring.dropped() << "\r\n";
        });

        if (drained != 0) {
            const auto content = batch.str();
            if (!write_bytes(file, content)) {
                break;
            }
            idle_cycles = 0;
        } else {
            ++idle_cycles;
        }
        if (idle_cycles >= 50) {
            FlushFileBuffers(file);
            idle_cycles = 0;
        }
        if (!keep_capturing || (!limiter.started() && GetTickCount64() >= absolute_deadline)) {
            capture_enabled.store(false, std::memory_order_release);
            break;
        }
        Sleep(20);
    }
    FlushFileBuffers(file);
    CloseHandle(file);
}

}  // namespace mcfix
