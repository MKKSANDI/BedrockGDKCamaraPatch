#include "installer/direct_plan.hpp"

namespace mcfix::installer {

DirectInstallPlan make_direct_install_plan(const DirectInstallFacts& facts) {
    if (facts.minecraft_running) {
        return {.layout = DirectLayout::minecraft_running};
    }
    if (!facts.proxy_payload_exists || !facts.camera_payload_exists) {
        return {.layout = DirectLayout::payload_missing};
    }
    if (!facts.proxy_payload_looks_forwarder) {
        return {.layout = DirectLayout::invalid_proxy_payload};
    }
    if (!facts.current_runtime_exists) {
        return {.layout = DirectLayout::missing_runtime};
    }

    if (facts.managed_original_exists) {
        if (!facts.managed_original_looks_original) {
            return {.layout = DirectLayout::invalid_managed_original};
        }
        if (facts.current_runtime_is_proxy_payload) {
            if (facts.installed_camera_exists && facts.installed_camera_matches_payload) {
                return {.allowed = true, .layout = DirectLayout::managed_healthy};
            }
            return {
                .allowed = true,
                .layout = DirectLayout::managed_needs_repair,
                .operations = {
                    DirectInstallOperation::install_camera_patch,
                    DirectInstallOperation::write_install_state,
                },
            };
        }
        if (facts.current_runtime_is_known_managed_proxy) {
            return {
                .allowed = true,
                .layout = DirectLayout::managed_needs_repair,
                .operations = {
                    DirectInstallOperation::install_proxy,
                    DirectInstallOperation::install_camera_patch,
                    DirectInstallOperation::write_install_state,
                },
            };
        }

        if (facts.current_runtime_looks_original) {
            return {
                .allowed = true,
                .layout = DirectLayout::managed_needs_repair,
                .operations = {
                    DirectInstallOperation::preserve_current_runtime,
                    DirectInstallOperation::install_proxy,
                    DirectInstallOperation::install_camera_patch,
                    DirectInstallOperation::write_install_state,
                },
            };
        }
        return {.layout = DirectLayout::unknown_runtime};
    }

    if (facts.legacy_original_exists) {
        if (!facts.legacy_original_looks_original) {
            return {.layout = DirectLayout::invalid_legacy_original};
        }
        return {
            .allowed = true,
            .layout = DirectLayout::legacy_proxy,
            .operations = {
                DirectInstallOperation::preserve_legacy_original,
                DirectInstallOperation::install_proxy,
                DirectInstallOperation::install_camera_patch,
                DirectInstallOperation::write_install_state,
            },
        };
    }

    if (facts.current_runtime_is_proxy_payload || !facts.current_runtime_looks_original) {
        return {.layout = DirectLayout::unknown_runtime};
    }
    return {
        .allowed = true,
        .layout = DirectLayout::clean_store,
        .operations = {
            DirectInstallOperation::preserve_current_runtime,
            DirectInstallOperation::install_proxy,
            DirectInstallOperation::install_camera_patch,
            DirectInstallOperation::write_install_state,
        },
    };
}

const char* direct_layout_name(DirectLayout layout) noexcept {
    switch (layout) {
    case DirectLayout::clean_store: return "clean_store";
    case DirectLayout::legacy_proxy: return "legacy_proxy";
    case DirectLayout::managed_healthy: return "managed_healthy";
    case DirectLayout::managed_needs_repair: return "managed_needs_repair";
    case DirectLayout::minecraft_running: return "minecraft_running";
    case DirectLayout::payload_missing: return "payload_missing";
    case DirectLayout::invalid_proxy_payload: return "invalid_proxy_payload";
    case DirectLayout::missing_runtime: return "missing_runtime";
    case DirectLayout::invalid_managed_original: return "invalid_managed_original";
    case DirectLayout::invalid_legacy_original: return "invalid_legacy_original";
    case DirectLayout::unknown_runtime: return "unknown_runtime";
    }
    return "unknown_runtime";
}

}  // namespace mcfix::installer
