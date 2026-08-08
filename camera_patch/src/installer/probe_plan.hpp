#pragma once

#include "installer/package_state.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace mcfix::installer {

enum class Operation {
    stage_files,
    create_certificate,
    pack,
    sign,
    register_package,
    verify_registration,
};

enum class RollbackOperation {
    remove_package,
    remove_certificate,
    remove_stage,
};

enum class ProbePlanReason {
    none,
    unsafe_package_state,
    non_absolute_path,
    stage_outside_root,
    unpinned_sdk_tool,
};

struct ProbeInputs {
    PackageState package_state{PackageState::unknown_bridge};
    std::filesystem::path stage_root;
    std::filesystem::path stage_path;
    std::filesystem::path makeappx_path;
    std::filesystem::path signtool_path;
    std::filesystem::path manifest_path;
    std::filesystem::path bridge_path;
    std::wstring certificate_thumbprint;
};

struct ProbePlan {
    bool allowed{};
    ProbePlanReason reason{ProbePlanReason::unsafe_package_state};
    std::vector<Operation> operations;
    std::vector<RollbackOperation> rollback;
};

ProbePlan make_probe_plan(const ProbeInputs& inputs);

}  // namespace mcfix::installer
