#include "installer/package_probe.hpp"

#include "installer/file_hash.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.Storage.h>
#include <winrt/base.h>

#include <sstream>

namespace mcfix::installer {
namespace {

bool minecraft_running() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return true;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"Minecraft.Windows.exe") == 0 ||
                _wcsicmp(entry.szExeFile, L"GameLaunchHelper.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

std::wstring version_text(const winrt::Windows::ApplicationModel::PackageVersion& version) {
    std::wostringstream text;
    text << version.Major << L'.' << version.Minor << L'.'
         << version.Build << L'.' << version.Revision;
    return text.str();
}

std::wstring architecture_text(winrt::Windows::System::ProcessorArchitecture architecture) {
    using winrt::Windows::System::ProcessorArchitecture;
    switch (architecture) {
    case ProcessorArchitecture::X64: return L"x64";
    case ProcessorArchitecture::X86: return L"x86";
    case ProcessorArchitecture::Arm64: return L"arm64";
    case ProcessorArchitecture::Arm: return L"arm";
    default: return L"unknown";
    }
}

}  // namespace

DiscoveredPackage discover_minecraft_package() {
    DiscoveredPackage result{};
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    const winrt::Windows::Management::Deployment::PackageManager manager;
    for (const auto& package : manager.FindPackagesForUser(L"")) {
        const auto id = package.Id();
        if (id.Name() != L"Microsoft.MinecraftUWP") {
            continue;
        }

        result.found = true;
        result.full_name = id.FullName().c_str();
        result.publisher = id.Publisher().c_str();
        result.install_location = package.InstalledLocation().Path().c_str();
        result.facts.package_name = id.Name().c_str();
        result.facts.package_version = version_text(id.Version());
        result.facts.architecture = architecture_text(id.Architecture());
        result.facts.minecraft_running = minecraft_running();

        const auto bridge = result.install_location / L"vcruntime140_1.dll";
        const auto original = result.install_location / L"vcruntime140_1_orig.dll";
        result.facts.bridge_exists = std::filesystem::is_regular_file(bridge);
        result.facts.orig_exists = std::filesystem::is_regular_file(original);
        if (result.facts.bridge_exists) {
            const auto hash = sha256_file(bridge);
            result.facts.bridge_hash.assign(hash.begin(), hash.end());
        }
        if (result.facts.orig_exists) {
            const auto hash = sha256_file(original);
            result.facts.orig_hash.assign(hash.begin(), hash.end());
        }
        return result;
    }
    return result;
}

std::string package_state_name(PackageState state) {
    switch (state) {
    case PackageState::clean: return "clean";
    case PackageState::minecraft_running: return "minecraft_running";
    case PackageState::wrong_package: return "wrong_package";
    case PackageState::wrong_architecture: return "wrong_architecture";
    case PackageState::missing_bridge: return "missing_bridge";
    case PackageState::existing_unmanaged_proxy: return "existing_unmanaged_proxy";
    case PackageState::unknown_bridge: return "unknown_bridge";
    }
    return "unknown_bridge";
}

}  // namespace mcfix::installer
