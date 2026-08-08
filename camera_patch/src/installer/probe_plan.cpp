#include "installer/probe_plan.hpp"

#include <array>
#include <cwctype>

namespace mcfix::installer {
namespace {

std::wstring normalized_lower(const std::filesystem::path& path) {
    auto value = path.lexically_normal().native();
    for (auto& character : value) {
        if (character == L'/') {
            character = L'\\';
        } else {
            character = static_cast<wchar_t>(std::towlower(character));
        }
    }
    return value;
}

bool inside_root(const std::filesystem::path& child, const std::filesystem::path& root) {
    auto child_text = normalized_lower(child);
    auto root_text = normalized_lower(root);
    while (!root_text.empty() && root_text.back() == L'\\') {
        root_text.pop_back();
    }
    root_text.push_back(L'\\');
    return child_text.starts_with(root_text);
}

bool pinned_sdk_tool(const std::filesystem::path& path, std::wstring_view filename) {
    const auto value = normalized_lower(path);
    const auto suffix = std::wstring(L"\\windows kits\\10\\bin\\10.0.26100.0\\x64\\") +
        std::wstring(filename);
    return value.ends_with(suffix);
}

}  // namespace

ProbePlan make_probe_plan(const ProbeInputs& inputs) {
    if (inputs.package_state != PackageState::clean) {
        return {.allowed = false, .reason = ProbePlanReason::unsafe_package_state};
    }
    const std::array paths{
        inputs.stage_root,
        inputs.stage_path,
        inputs.makeappx_path,
        inputs.signtool_path,
        inputs.manifest_path,
        inputs.bridge_path,
    };
    for (const auto& path : paths) {
        if (!path.is_absolute()) {
            return {.allowed = false, .reason = ProbePlanReason::non_absolute_path};
        }
    }
    if (!inside_root(inputs.stage_path, inputs.stage_root) ||
        !inside_root(inputs.manifest_path, inputs.stage_root)) {
        return {.allowed = false, .reason = ProbePlanReason::stage_outside_root};
    }
    if (!pinned_sdk_tool(inputs.makeappx_path, L"makeappx.exe") ||
        !pinned_sdk_tool(inputs.signtool_path, L"signtool.exe")) {
        return {.allowed = false, .reason = ProbePlanReason::unpinned_sdk_tool};
    }
    return {
        .allowed = true,
        .reason = ProbePlanReason::none,
        .operations = {
            Operation::stage_files,
            Operation::create_certificate,
            Operation::pack,
            Operation::sign,
            Operation::register_package,
            Operation::verify_registration,
        },
        .rollback = {
            RollbackOperation::remove_package,
            RollbackOperation::remove_certificate,
            RollbackOperation::remove_stage,
        },
    };
}

}  // namespace mcfix::installer
