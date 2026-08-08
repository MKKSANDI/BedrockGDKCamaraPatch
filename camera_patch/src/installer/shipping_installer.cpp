#include "installer/shipping_installer.hpp"

#include "installer/direct_plan.hpp"
#include "installer/direct_transaction.hpp"
#include "installer/directory_access.hpp"
#include "installer/file_hash.hpp"
#include "installer/package_probe.hpp"
#include "installer/probe_runner.hpp"
#include "installer/watchdog_task.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mcfix::installer {
namespace {

struct Paths {
    std::filesystem::path executable;
    std::filesystem::path payload_root;
    std::filesystem::path proxy_payload;
    std::filesystem::path camera_payload;
    std::filesystem::path watchdog_payload;
    std::filesystem::path durable_root;
    std::filesystem::path durable_bin;
    std::filesystem::path state_file;
};

std::filesystem::path executable_path() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("Patcher executable path is unavailable");
    }
    buffer.resize(length);
    return buffer;
}

std::filesystem::path program_data_path() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(
        L"ProgramData", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("ProgramData path is unavailable");
    }
    buffer.resize(length);
    return buffer;
}

Paths resolve_paths() {
    Paths paths{};
    paths.executable = executable_path();
    paths.payload_root = paths.executable.parent_path();
    paths.proxy_payload = paths.payload_root / L"vcruntime140_1.dll";
    paths.camera_payload = paths.payload_root / L"MCFIXCameraPatch.dll";
    paths.watchdog_payload = paths.payload_root / L"MCFIXWatchdog.exe";
    paths.durable_root = program_data_path() / L"MCFIX";
    paths.durable_bin = paths.durable_root / L"bin";
    paths.state_file = paths.durable_root / L"install-manifest.json";
    return paths;
}

std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), count, nullptr, nullptr);
    return result;
}

std::string json_escape(std::string_view value) {
    std::string result;
    for (const char character : value) {
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(character); break;
        }
    }
    return result;
}

void copy_atomic_verified(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    if (std::filesystem::is_regular_file(destination) &&
        sha256_file(source) == sha256_file(destination)) {
        return;
    }
    auto temporary = destination;
    temporary += L".mcfix.new";
    std::error_code error;
    std::filesystem::remove(temporary, error);
    std::filesystem::copy_file(
        source, temporary, std::filesystem::copy_options::overwrite_existing);
    if (sha256_file(source) != sha256_file(temporary)) {
        std::filesystem::remove(temporary, error);
        throw std::runtime_error("durable payload staging hash mismatch");
    }
    if (!MoveFileExW(
            temporary.c_str(), destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error);
        throw std::runtime_error("durable payload atomic replacement failed");
    }
}

void stage_durable_files(const Paths& paths) {
    std::filesystem::create_directories(paths.durable_bin);
    std::filesystem::create_directories(paths.durable_root / L"logs");
    copy_atomic_verified(paths.executable, paths.durable_bin / L"Patcher.exe");
    copy_atomic_verified(paths.proxy_payload, paths.durable_bin / L"vcruntime140_1.dll");
    copy_atomic_verified(paths.camera_payload, paths.durable_bin / L"MCFIXCameraPatch.dll");
    copy_atomic_verified(paths.watchdog_payload, paths.durable_bin / L"MCFIXWatchdog.exe");
}

void write_state(
    const Paths& paths,
    const DiscoveredPackage& package,
    const DirectTransactionResult& transaction) {
    std::filesystem::create_directories(paths.durable_root);
    auto temporary = paths.state_file;
    temporary += L".new";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << "{\n"
           << "  \"schema\": 1,\n"
           << "  \"packageFullName\": \"" << json_escape(utf8(package.full_name)) << "\",\n"
           << "  \"packagePath\": \"" << json_escape(utf8(package.install_location.native())) << "\",\n"
           << "  \"proxySha256\": \"" << sha256_file(paths.proxy_payload) << "\",\n"
           << "  \"cameraPatchSha256\": \"" << sha256_file(paths.camera_payload) << "\",\n"
           << "  \"layout\": \"" << direct_layout_name(transaction.layout) << "\"\n"
           << "}\n";
    output.close();
    if (!output) throw std::runtime_error("could not write install manifest");
    if (!MoveFileExW(
            temporary.c_str(), paths.state_file.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("could not commit install manifest");
    }
}

void ensure_watchdog_task(const Paths& paths) {
    std::string error;
    if (!register_and_start_watchdog_task(
            paths.durable_bin / L"MCFIXWatchdog.exe", error)) {
        throw std::runtime_error("could not register the MCFIX watchdog task: " + error);
    }
}

void remove_watchdog_task() noexcept {
    std::string error;
    static_cast<void>(stop_and_remove_watchdog_task(error));
}

std::filesystem::path original_security_template(
    const std::filesystem::path& install_root,
    DirectLayout layout) {
    if (layout == DirectLayout::legacy_proxy) {
        return install_root / L"vcruntime140_1_orig.dll";
    }
    if (layout == DirectLayout::managed_healthy ||
        layout == DirectLayout::managed_needs_repair) {
        const auto managed = install_root / L"vcruntime140_1_mcfix_original.dll";
        if (std::filesystem::is_regular_file(managed)) return managed;
    }
    return install_root / L"vcruntime140_1.dll";
}

bool apply_runtime_security(
    std::wstring_view template_sddl,
    const std::filesystem::path& install_root) {
    bool ok = true;
    for (const auto name : {
             L"vcruntime140_1.dll",
             L"vcruntime140_1_mcfix_original.dll",
             L"MCFIXCameraPatch.dll"}) {
        const auto target = install_root / name;
        if (std::filesystem::is_regular_file(target)) {
            ok = apply_file_security_sddl(template_sddl, target) && ok;
        }
    }
    return ok;
}

}  // namespace

int shipping_status(bool verify_only) {
    try {
        const auto paths = resolve_paths();
        const auto package = discover_minecraft_package();
        if (!package.found) {
            std::cout << "{\"ok\":false,\"state\":\"package_missing\"}\n";
            return 3;
        }
        const auto plan = inspect_direct_patch(
            package.install_location, paths.proxy_payload, paths.camera_payload,
            package.facts.minecraft_running,
            paths.durable_bin / L"vcruntime140_1.dll");
        const bool healthy = plan.allowed && plan.layout == DirectLayout::managed_healthy;
        std::cout << "{\"ok\":" << (healthy ? "true" : "false")
                  << ",\"state\":\"" << direct_layout_name(plan.layout)
                  << "\",\"packageFullName\":\""
                  << json_escape(utf8(package.full_name)) << "\",\"minecraftRunning\":"
                  << (package.facts.minecraft_running ? "true" : "false") << "}\n";
        return verify_only && !healthy ? 6 : 0;
    } catch (const std::exception& error) {
        std::cout << "{\"ok\":false,\"error\":\"" << json_escape(error.what()) << "\"}\n";
        return 4;
    }
}

int shipping_install(bool repair, bool silent) {
    try {
        const auto paths = resolve_paths();
        if (!std::filesystem::is_regular_file(paths.proxy_payload) ||
            !std::filesystem::is_regular_file(paths.camera_payload) ||
            !std::filesystem::is_regular_file(paths.watchdog_payload)) {
            throw std::runtime_error("release payload is incomplete beside Patcher.exe");
        }
        const auto package = discover_minecraft_package();
        if (!package.found) throw std::runtime_error("Minecraft for Windows is not installed");
        const auto plan = inspect_direct_patch(
            package.install_location, paths.proxy_payload, paths.camera_payload,
            package.facts.minecraft_running,
            paths.durable_bin / L"vcruntime140_1.dll");
        if (!plan.allowed) {
            std::cout << "{\"ok\":false,\"state\":\""
                      << direct_layout_name(plan.layout) << "\"}\n";
            return plan.layout == DirectLayout::minecraft_running ? 20 : 21;
        }

        DirectTransactionResult transaction{
            .ok = true,
            .layout = plan.layout,
        };
        if (!plan.operations.empty()) {
            const auto security_template = original_security_template(
                package.install_location, plan.layout);
            const auto security_sddl = file_security_sddl(security_template);
            if (security_sddl.empty()) {
                throw std::runtime_error("could not capture the original runtime security");
            }
            auto access = DirectoryAccessGuard::acquire(package.install_location);
            if (!access.active()) {
                throw std::runtime_error("could not unlock the exact package directory: " + access.error());
            }
            transaction = install_direct_patch(
                package.install_location, paths.proxy_payload, paths.camera_payload, false,
                paths.durable_bin / L"vcruntime140_1.dll");
            if (!transaction.ok) {
                throw std::runtime_error(transaction.error);
            }
            if (!apply_runtime_security(security_sddl, package.install_location)) {
                throw std::runtime_error("installed files could not inherit the original runtime security");
            }
            if (!access.restore()) {
                throw std::runtime_error("package directory security could not be restored: " + access.error());
            }
        }

        stage_durable_files(paths);
        ensure_watchdog_task(paths);
        write_state(paths, package, transaction);
        std::cout << "{\"ok\":true,\"command\":\""
                  << (repair ? "repair" : "install") << "\",\"changed\":"
                  << (transaction.changed ? "true" : "false") << ",\"state\":\"managed_healthy\"}\n";
        static_cast<void>(silent);
        return 0;
    } catch (const std::exception& error) {
        std::cout << "{\"ok\":false,\"error\":\"" << json_escape(error.what()) << "\"}\n";
        return 22;
    }
}

int shipping_uninstall(bool silent) {
    try {
        auto paths = resolve_paths();
        const auto durable_proxy = paths.durable_bin / L"vcruntime140_1.dll";
        if (std::filesystem::is_regular_file(durable_proxy)) {
            paths.proxy_payload = durable_proxy;
        }
        const auto package = discover_minecraft_package();
        if (!package.found) throw std::runtime_error("Minecraft for Windows is not installed");
        if (package.facts.minecraft_running) return 20;
        auto access = DirectoryAccessGuard::acquire(package.install_location);
        if (!access.active()) {
            throw std::runtime_error("could not unlock the exact package directory: " + access.error());
        }
        const auto result = uninstall_direct_patch(
            package.install_location, paths.proxy_payload, false);
        if (!result.ok) throw std::runtime_error(result.error);
        if (!access.restore()) {
            throw std::runtime_error("package directory security could not be restored: " + access.error());
        }
        remove_watchdog_task();
        std::error_code error;
        std::filesystem::remove(paths.state_file, error);
        std::filesystem::remove(paths.durable_bin / L"MCFIXCameraPatch.dll", error);
        std::filesystem::remove(paths.durable_bin / L"vcruntime140_1.dll", error);
        std::filesystem::remove(paths.durable_bin / L"MCFIXWatchdog.exe", error);
        const auto installed_patcher = paths.durable_bin / L"Patcher.exe";
        if (std::filesystem::equivalent(paths.executable, installed_patcher, error)) {
            MoveFileExW(installed_patcher.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        } else {
            std::filesystem::remove(installed_patcher, error);
        }
        std::cout << "{\"ok\":true,\"command\":\"uninstall\"}\n";
        static_cast<void>(silent);
        return 0;
    } catch (const std::exception& error) {
        std::cout << "{\"ok\":false,\"error\":\"" << json_escape(error.what()) << "\"}\n";
        return 23;
    }
}

}  // namespace mcfix::installer
