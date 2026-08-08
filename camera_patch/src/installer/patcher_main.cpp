#include "installer/file_hash.hpp"
#include "installer/manifest.hpp"
#include "installer/package_probe.hpp"
#include "installer/package_state.hpp"
#include "installer/patcher_command.hpp"
#include "installer/probe_runner.hpp"
#include "installer/shipping_installer.hpp"

#include <windows.h>
#include <shellapi.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), size, nullptr, nullptr) != size) {
        return {};
    }
    return result;
}

std::string json_escape(std::string_view value) {
    std::string escaped;
    for (const auto character : value) {
        switch (character) {
        case '\\': escaped += "\\\\"; break;
        case '\"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += character; break;
        }
    }
    return escaped;
}

std::filesystem::path executable_path() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size()) return {};
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

bool is_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(
        token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

std::wstring quote_argument(std::wstring_view argument) {
    std::wstring result(1, L'"');
    std::size_t slashes = 0;
    for (const auto character : argument) {
        if (character == L'\\') {
            ++slashes;
        } else if (character == L'"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(character);
            slashes = 0;
        } else {
            result.append(slashes, L'\\');
            result.push_back(character);
            slashes = 0;
        }
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

int relaunch_elevated(const std::vector<std::wstring>& arguments, bool silent) {
    const auto executable = executable_path();
    std::wstring parameters;
    for (const auto& argument : arguments) {
        if (!parameters.empty()) parameters.push_back(L' ');
        parameters += quote_argument(argument);
    }
    SHELLEXECUTEINFOW request{};
    request.cbSize = sizeof(request);
    request.fMask = SEE_MASK_NOCLOSEPROCESS;
    request.lpVerb = L"runas";
    request.lpFile = executable.c_str();
    request.lpParameters = parameters.c_str();
    request.nShow = silent ? SW_HIDE : SW_SHOWNORMAL;
    if (!ShellExecuteExW(&request) || request.hProcess == nullptr) {
        return GetLastError() == ERROR_CANCELLED ? ERROR_CANCELLED : 24;
    }
    WaitForSingleObject(request.hProcess, INFINITE);
    DWORD exit_code = 24;
    GetExitCodeProcess(request.hProcess, &exit_code);
    CloseHandle(request.hProcess);
    return static_cast<int>(exit_code);
}

int probe_status() {
    try {
        const auto package = mcfix::installer::discover_minecraft_package();
        if (!package.found) {
            std::cout << "{\"command\":\"probe-status\",\"found\":false}\n";
            return 3;
        }
        const auto state = mcfix::installer::classify_package_state(package.facts);
        std::cout << "{\"command\":\"probe-status\",\"found\":true,"
                  << "\"packageFullName\":\"" << json_escape(utf8(package.full_name)) << "\","
                  << "\"version\":\"" << json_escape(utf8(package.facts.package_version)) << "\","
                  << "\"architecture\":\"" << json_escape(utf8(package.facts.architecture)) << "\","
                  << "\"minecraftRunning\":" << (package.facts.minecraft_running ? "true" : "false") << ','
                  << "\"state\":\"" << mcfix::installer::package_state_name(state) << "\","
                  << "\"bridgeHash\":\"" << json_escape(utf8(package.facts.bridge_hash)) << "\","
                  << "\"origExists\":" << (package.facts.orig_exists ? "true" : "false") << ','
                  << "\"origHash\":\"" << json_escape(utf8(package.facts.orig_hash)) << "\","
                  << "\"mutations\":0}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cout << "{\"command\":\"probe-status\",\"error\":\""
                  << json_escape(error.what()) << "\"}\n";
        return 4;
    }
}

constexpr std::array<unsigned char, 68> transparent_png{
    0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x04,0x00,0x00,0x00,0xB5,0x1C,0x0C,
    0x02,0x00,0x00,0x00,0x0B,0x49,0x44,0x41,0x54,0x78,0xDA,0x63,0x64,0xF8,0x0F,0x00,
    0x01,0x05,0x01,0x01,0x27,0x18,0xE3,0x66,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,
    0xAE,0x42,0x60,0x82,
};

int probe_stage() {
    try {
        const auto executable = executable_path();
        if (executable.empty()) throw std::runtime_error("Patcher executable path is unavailable");
        const auto configuration_dir = executable.parent_path();
        const auto binary_root = configuration_dir.parent_path();
        const auto stage_root = binary_root / L"probe-stage";
        const auto package_dir = stage_root / L"package";
        const auto asset_dir = package_dir / L"Assets";
        const auto bridge = configuration_dir / L"vcruntime140_1.dll";
        const auto manifest_path = package_dir / L"AppxManifest.xml";
        const auto package_path = stage_root / L"MCFIXBootstrapProbe.msix";
        const auto makeappx = std::filesystem::path(
            L"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\x64\\makeappx.exe");

        if (!std::filesystem::is_regular_file(bridge)) {
            throw std::runtime_error("bootstrap bridge is missing beside Patcher.exe");
        }
        if (!std::filesystem::is_regular_file(makeappx)) {
            throw std::runtime_error("pinned Windows SDK MakeAppx.exe is unavailable");
        }

        std::error_code error;
        std::filesystem::remove_all(package_dir, error);
        if (error) throw std::runtime_error("could not reset the MCFIX probe stage");
        std::filesystem::create_directories(asset_dir);
        std::filesystem::copy_file(
            bridge, package_dir / L"vcruntime140_1.dll",
            std::filesystem::copy_options::overwrite_existing);

        const auto manifest = mcfix::installer::render_modification_manifest({
            .identity_name = "MCFIX.CameraPatch.Modification",
            .publisher = "CN=MCFIX Local Package",
            .version = "1.0.0.0",
            .main_package_name = "Microsoft.MinecraftUWP",
            .main_package_publisher =
                "CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, "
                "S=Washington, C=US",
            .architecture = "x64",
        });
        {
            std::ofstream output(manifest_path, std::ios::binary | std::ios::trunc);
            output.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
            if (!output) throw std::runtime_error("could not write AppxManifest.xml");
        }
        {
            std::ofstream output(asset_dir / L"StoreLogo.png", std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char*>(transparent_png.data()),
                static_cast<std::streamsize>(transparent_png.size()));
            if (!output) throw std::runtime_error("could not write the generated probe logo");
        }

        std::filesystem::remove(package_path, error);
        const auto packed = mcfix::installer::run_hidden({
            .executable = makeappx,
            .arguments = {L"pack", L"/d", package_dir.native(), L"/p", package_path.native(), L"/o"},
            .timeout_ms = 30000,
        });
        if (!packed.launched || packed.timed_out || packed.exit_code != 0) {
            throw std::runtime_error("MakeAppx failed: " + packed.output);
        }
        const auto hash = mcfix::installer::sha256_file(package_path);
        std::cout << "{\"command\":\"probe-stage\",\"ok\":true,"
                  << "\"package\":\"" << json_escape(utf8(package_path.native())) << "\","
                  << "\"sha256\":\"" << hash << "\",\"minecraftMutations\":0}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cout << "{\"command\":\"probe-stage\",\"ok\":false,\"error\":\""
                  << json_escape(error.what()) << "\"}\n";
        return 5;
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::vector<std::wstring> arguments;
    for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
    if (arguments.empty()) arguments.emplace_back(L"install");
    const auto command = mcfix::installer::parse_patcher_command(arguments);
    const bool mutating =
        command.kind == mcfix::installer::CommandKind::install ||
        command.kind == mcfix::installer::CommandKind::repair ||
        command.kind == mcfix::installer::CommandKind::uninstall ||
        command.kind == mcfix::installer::CommandKind::watch;
    if (mutating && !is_elevated()) {
        return relaunch_elevated(arguments, command.silent);
    }
    switch (command.kind) {
    case mcfix::installer::CommandKind::status:
        return mcfix::installer::shipping_status(false);
    case mcfix::installer::CommandKind::verify:
        return mcfix::installer::shipping_status(true);
    case mcfix::installer::CommandKind::install:
        return mcfix::installer::shipping_install(false, command.silent);
    case mcfix::installer::CommandKind::repair:
        return mcfix::installer::shipping_install(true, command.silent);
    case mcfix::installer::CommandKind::uninstall:
        return mcfix::installer::shipping_uninstall(command.silent);
    case mcfix::installer::CommandKind::watch:
        return mcfix::installer::shipping_watch();
    case mcfix::installer::CommandKind::probe_status: return probe_status();
    case mcfix::installer::CommandKind::probe_stage: return probe_stage();
    case mcfix::installer::CommandKind::probe_install:
    case mcfix::installer::CommandKind::probe_uninstall:
        std::cerr << "live package mutation is disabled until the clean-baseline gate passes\n";
        return 10;
    case mcfix::installer::CommandKind::invalid:
        std::cerr << "usage: Patcher.exe install|status|verify|repair [--silent]|uninstall|watch --silent\n";
        return 2;
    }
    return 2;
}
