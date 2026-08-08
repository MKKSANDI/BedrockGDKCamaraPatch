#include "installer/direct_transaction.hpp"

#include "installer/file_hash.hpp"
#include "installer/runtime_contract.hpp"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mcfix::installer {
namespace {

constexpr std::wstring_view runtime_name = L"vcruntime140_1.dll";
constexpr std::wstring_view managed_original_name = L"vcruntime140_1_mcfix_original.dll";
constexpr std::wstring_view legacy_original_name = L"vcruntime140_1_orig.dll";
constexpr std::wstring_view camera_name = L"MCFIXCameraPatch.dll";

std::filesystem::path temp_sibling(
    const std::filesystem::path& path,
    std::wstring_view tag) {
    auto result = path;
    result += L".mcfix.";
    result += tag;
    result += L".";
    result += std::to_wstring(GetCurrentProcessId());
    return result;
}

void remove_if_present(const std::filesystem::path& path) noexcept {
    std::error_code error;
    std::filesystem::remove(path, error);
}

void copy_verified(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    remove_if_present(destination);
    std::filesystem::copy_file(
        source, destination, std::filesystem::copy_options::overwrite_existing);
    if (sha256_file(source) != sha256_file(destination)) {
        remove_if_present(destination);
        throw std::runtime_error("staged file hash mismatch");
    }
}

void replace_with(const std::filesystem::path& staged, const std::filesystem::path& target) {
    if (!MoveFileExW(
            staged.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("atomic file replacement failed: " +
                                 std::to_string(GetLastError()));
    }
}

DirectInstallFacts inspect_facts(
    const std::filesystem::path& target_root,
    const std::filesystem::path& proxy_payload,
    const std::filesystem::path& camera_payload,
    bool minecraft_running,
    const std::filesystem::path& known_installed_proxy) {
    const auto current = target_root / runtime_name;
    const auto managed = target_root / managed_original_name;
    const auto legacy = target_root / legacy_original_name;
    const auto installed_camera = target_root / camera_name;
    DirectInstallFacts facts{
        .minecraft_running = minecraft_running,
        .proxy_payload_exists = std::filesystem::is_regular_file(proxy_payload),
        .camera_payload_exists = std::filesystem::is_regular_file(camera_payload),
        .current_runtime_exists = std::filesystem::is_regular_file(current),
        .managed_original_exists = std::filesystem::is_regular_file(managed),
        .legacy_original_exists = std::filesystem::is_regular_file(legacy),
        .installed_camera_exists = std::filesystem::is_regular_file(installed_camera),
    };
    if (facts.proxy_payload_exists) {
        facts.proxy_payload_looks_forwarder =
            inspect_runtime_contract(proxy_payload).looks_forwarding_proxy();
    }
    if (facts.current_runtime_exists) {
        facts.current_runtime_looks_original = inspect_runtime_contract(current).looks_original();
    }
    if (facts.managed_original_exists) {
        facts.managed_original_looks_original = inspect_runtime_contract(managed).looks_original();
    }
    if (facts.legacy_original_exists) {
        facts.legacy_original_looks_original = inspect_runtime_contract(legacy).looks_original();
    }
    if (facts.current_runtime_exists && facts.proxy_payload_exists) {
        facts.current_runtime_is_proxy_payload =
            sha256_file(current) == sha256_file(proxy_payload);
    }
    if (facts.current_runtime_exists && !known_installed_proxy.empty() &&
        std::filesystem::is_regular_file(known_installed_proxy)) {
        facts.current_runtime_is_known_managed_proxy =
            sha256_file(current) == sha256_file(known_installed_proxy);
    }
    if (facts.installed_camera_exists && facts.camera_payload_exists) {
        facts.installed_camera_matches_payload =
            sha256_file(installed_camera) == sha256_file(camera_payload);
    }
    return facts;
}

}  // namespace

DirectInstallPlan inspect_direct_patch(
    const std::filesystem::path& target_root,
    const std::filesystem::path& proxy_payload,
    const std::filesystem::path& camera_payload,
    bool minecraft_running,
    const std::filesystem::path& known_installed_proxy) {
    return make_direct_install_plan(inspect_facts(
        target_root, proxy_payload, camera_payload, minecraft_running,
        known_installed_proxy));
}

DirectTransactionResult install_direct_patch(
    const std::filesystem::path& target_root,
    const std::filesystem::path& proxy_payload,
    const std::filesystem::path& camera_payload,
    bool minecraft_running,
    const std::filesystem::path& known_installed_proxy) {
    const auto plan = inspect_direct_patch(
        target_root, proxy_payload, camera_payload, minecraft_running,
        known_installed_proxy);
    if (!plan.allowed) {
        return {.layout = plan.layout, .error = "direct-install safety gate refused the package layout"};
    }
    if (plan.operations.empty()) {
        return {.ok = true, .layout = plan.layout};
    }

    const auto current = target_root / runtime_name;
    const auto managed = target_root / managed_original_name;
    const auto legacy = target_root / legacy_original_name;
    const auto installed_camera = target_root / camera_name;
    const auto managed_temp = temp_sibling(managed, L"new");
    const auto proxy_temp = temp_sibling(current, L"new");
    const auto camera_temp = temp_sibling(installed_camera, L"new");
    bool proxy_committed = false;

    try {
        const bool preserve_current = !plan.operations.empty() &&
            plan.operations.front() == DirectInstallOperation::preserve_current_runtime;
        const bool preserve_legacy = !plan.operations.empty() &&
            plan.operations.front() == DirectInstallOperation::preserve_legacy_original;
        if (preserve_current || preserve_legacy) {
            const auto& original_source = preserve_current ? current : legacy;
            copy_verified(original_source, managed_temp);
            if (!inspect_runtime_contract(managed_temp).looks_original()) {
                throw std::runtime_error("preserved runtime failed the direct-export contract");
            }
        }
        const bool install_proxy = std::find(
            plan.operations.begin(), plan.operations.end(),
            DirectInstallOperation::install_proxy) != plan.operations.end();
        const bool install_camera = std::find(
            plan.operations.begin(), plan.operations.end(),
            DirectInstallOperation::install_camera_patch) != plan.operations.end();
        if (install_proxy) {
            copy_verified(proxy_payload, proxy_temp);
        }
        if (install_camera) {
            copy_verified(camera_payload, camera_temp);
        }

        if (preserve_current || preserve_legacy) {
            replace_with(managed_temp, managed);
        }
        if (install_camera) {
            replace_with(camera_temp, installed_camera);
        }
        if (install_proxy) {
            replace_with(proxy_temp, current);
            proxy_committed = true;
        }

        if (sha256_file(current) != sha256_file(proxy_payload) ||
            sha256_file(installed_camera) != sha256_file(camera_payload) ||
            !inspect_runtime_contract(managed).looks_original()) {
            throw std::runtime_error("post-commit verification failed");
        }
        return {.ok = true, .changed = true, .layout = plan.layout};
    } catch (const std::exception& error) {
        remove_if_present(managed_temp);
        remove_if_present(proxy_temp);
        remove_if_present(camera_temp);
        if (proxy_committed && inspect_runtime_contract(managed).looks_original()) {
            try {
                const auto rollback = temp_sibling(current, L"rollback");
                copy_verified(managed, rollback);
                replace_with(rollback, current);
            } catch (...) {
            }
        }
        return {.layout = plan.layout, .error = error.what()};
    }
}

DirectTransactionResult uninstall_direct_patch(
    const std::filesystem::path& target_root,
    const std::filesystem::path& proxy_payload,
    bool minecraft_running) {
    if (minecraft_running) {
        return {.layout = DirectLayout::minecraft_running, .error = "Minecraft is running"};
    }
    const auto current = target_root / runtime_name;
    const auto managed = target_root / managed_original_name;
    const auto installed_camera = target_root / camera_name;
    if (!std::filesystem::is_regular_file(managed)) {
        if (inspect_runtime_contract(current).looks_original()) {
            return {.ok = true, .layout = DirectLayout::clean_store};
        }
        return {.layout = DirectLayout::invalid_managed_original,
                .error = "managed original runtime is unavailable and current runtime is not stock"};
    }
    if (!inspect_runtime_contract(managed).looks_original()) {
        return {.layout = DirectLayout::invalid_managed_original,
                .error = "valid managed original runtime is unavailable"};
    }
    try {
        bool changed = false;
        const bool current_is_proxy = std::filesystem::is_regular_file(proxy_payload) &&
            std::filesystem::is_regular_file(current) &&
            sha256_file(current) == sha256_file(proxy_payload);
        if (current_is_proxy) {
            const auto restored = temp_sibling(current, L"restore");
            copy_verified(managed, restored);
            replace_with(restored, current);
            changed = true;
        } else if (!inspect_runtime_contract(current).looks_original()) {
            return {.layout = DirectLayout::unknown_runtime,
                    .error = "current runtime is neither MCFIX proxy nor a valid original"};
        }
        std::error_code error;
        if (std::filesystem::remove(installed_camera, error)) changed = true;
        if (error) throw std::filesystem::filesystem_error("remove camera payload", installed_camera, error);
        if (std::filesystem::remove(managed, error)) changed = true;
        if (error) throw std::filesystem::filesystem_error("remove managed original", managed, error);
        return {.ok = true, .changed = changed, .layout = DirectLayout::clean_store};
    } catch (const std::exception& error) {
        return {.layout = DirectLayout::unknown_runtime, .error = error.what()};
    }
}

}  // namespace mcfix::installer
