#include "installer/package_state.hpp"
#include "installer/manifest.hpp"
#include "installer/probe_plan.hpp"
#include "installer/probe_runner.hpp"
#include "installer/patcher_command.hpp"
#include "installer/file_hash.hpp"
#include "installer/direct_plan.hpp"
#include "installer/runtime_contract.hpp"
#include "installer/direct_transaction.hpp"
#include "installer/directory_access.hpp"
#include "installer/watchdog_task.hpp"

#include <windows.h>

#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_clean_package_state_is_installable() {
    const mcfix::installer::PackageFacts facts{
        .package_name = L"Microsoft.MinecraftUWP",
        .package_version = L"1.26.4201.0",
        .architecture = L"x64",
        .minecraft_running = false,
        .bridge_exists = true,
        .bridge_hash = L"KNOWN_STORE_HASH",
        .expected_store_bridge_hash = L"KNOWN_STORE_HASH",
        .orig_exists = false,
    };

    expect(mcfix::installer::classify_package_state(facts) ==
               mcfix::installer::PackageState::clean,
           "a known Store bridge without an _orig sibling is clean");
}

void test_existing_proxy_is_reported_not_overwritten() {
    const mcfix::installer::PackageFacts facts{
        .package_name = L"Microsoft.MinecraftUWP",
        .package_version = L"1.26.4201.0",
        .architecture = L"x64",
        .minecraft_running = false,
        .bridge_exists = true,
        .bridge_hash = L"A42228480A42D5411798E3F83E5A7433786A97AC5E199EB2500BE077B67DE181",
        .orig_exists = true,
        .orig_hash = L"F98C3A0B9C487235E909A918EEC4A33A453ED804A022CF2997855EEC18350398",
    };

    expect(mcfix::installer::classify_package_state(facts) ==
               mcfix::installer::PackageState::existing_unmanaged_proxy,
           "an _orig sibling must stop a clean-baseline claim");
}

mcfix::installer::PackageFacts clean_facts() {
    return {
        .package_name = L"Microsoft.MinecraftUWP",
        .package_version = L"1.26.4201.0",
        .architecture = L"x64",
        .minecraft_running = false,
        .bridge_exists = true,
        .bridge_hash = L"KNOWN_STORE_HASH",
        .expected_store_bridge_hash = L"KNOWN_STORE_HASH",
        .orig_exists = false,
    };
}

void test_each_unsafe_package_fact_has_a_specific_refusal() {
    auto facts = clean_facts();
    facts.minecraft_running = true;
    expect(mcfix::installer::classify_package_state(facts) ==
               mcfix::installer::PackageState::minecraft_running,
           "a running Minecraft process must stop package work");

    facts = clean_facts();
    facts.package_name = L"Different.Package";
    expect(mcfix::installer::classify_package_state(facts) ==
               mcfix::installer::PackageState::wrong_package,
           "a different package identity must be refused");

    facts = clean_facts();
    facts.architecture = L"arm64";
    expect(mcfix::installer::classify_package_state(facts) ==
               mcfix::installer::PackageState::wrong_architecture,
           "a non-x64 package must be refused");

    facts = clean_facts();
    facts.bridge_exists = false;
    expect(mcfix::installer::classify_package_state(facts) ==
               mcfix::installer::PackageState::missing_bridge,
           "a package without the proposed bridge seam must be refused");

    facts = clean_facts();
    facts.bridge_hash = L"UNRECOGNIZED_HASH";
    expect(mcfix::installer::classify_package_state(facts) ==
               mcfix::installer::PackageState::unknown_bridge,
           "an unrecognized bridge binary must be refused");
}

void test_manifest_binds_only_to_minecraft_main_package() {
    const mcfix::installer::ManifestInput input{
        .identity_name = "MCFIX.CameraPatch.Modification",
        .publisher = "CN=MCFIX Local Package",
        .version = "1.0.0.0",
        .main_package_name = "Microsoft.MinecraftUWP",
        .main_package_publisher =
            "CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, "
            "S=Washington, C=US",
        .architecture = "x64",
    };

    const auto xml = mcfix::installer::render_modification_manifest(input);
    expect(xml.find("uap4:MainPackageDependency Name=\"Microsoft.MinecraftUWP\"") !=
               std::string::npos,
           "manifest must bind to the exact main package identity");
    expect(xml.find("Executable=") == std::string::npos,
           "a modification package must not replace the Minecraft application entrypoint");
}

bool manifest_rejected(mcfix::installer::ManifestInput input) {
    try {
        static_cast<void>(mcfix::installer::render_modification_manifest(input));
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

mcfix::installer::ManifestInput valid_manifest_input() {
    return {
        .identity_name = "MCFIX.CameraPatch.Modification",
        .publisher = "CN=MCFIX Local Package",
        .version = "1.0.0.0",
        .main_package_name = "Microsoft.MinecraftUWP",
        .main_package_publisher =
            "CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, "
            "S=Washington, C=US",
        .architecture = "x64",
    };
}

void test_manifest_escapes_attributes_and_rejects_invalid_contracts() {
    auto escaped = valid_manifest_input();
    escaped.identity_name = "MCFIX&Camera";
    escaped.publisher = "CN=MCFIX \"Local\"";
    const auto xml = mcfix::installer::render_modification_manifest(escaped);
    expect(xml.find("Name=\"MCFIX&amp;Camera\"") != std::string::npos,
           "manifest attributes must escape ampersands");
    expect(xml.find("Publisher=\"CN=MCFIX &quot;Local&quot;\"") != std::string::npos,
           "manifest attributes must escape quotes");

    auto invalid = valid_manifest_input();
    invalid.main_package_name.clear();
    expect(manifest_rejected(invalid), "an empty main package name must be rejected");

    invalid = valid_manifest_input();
    invalid.version = "1.0";
    expect(manifest_rejected(invalid), "a non-four-part package version must be rejected");

    invalid = valid_manifest_input();
    invalid.architecture = "arm64";
    expect(manifest_rejected(invalid), "a non-x64 package architecture must be rejected");

    invalid = valid_manifest_input();
    invalid.identity_name = std::string("MCFIX") + static_cast<char>(1) + "Camera";
    expect(manifest_rejected(invalid), "XML control characters must be rejected");
}

mcfix::installer::ProbeInputs valid_probe_inputs() {
    return {
        .package_state = mcfix::installer::PackageState::clean,
        .stage_root = L"C:\\Users\\tester\\Desktop\\MCFIX\\camera_patch\\out\\probe-stage",
        .stage_path = L"C:\\Users\\tester\\Desktop\\MCFIX\\camera_patch\\out\\probe-stage\\package",
        .makeappx_path = L"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\x64\\makeappx.exe",
        .signtool_path = L"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\x64\\signtool.exe",
        .manifest_path = L"C:\\Users\\tester\\Desktop\\MCFIX\\camera_patch\\out\\probe-stage\\package\\AppxManifest.xml",
        .bridge_path = L"C:\\Users\\tester\\Desktop\\MCFIX\\camera_patch\\out\\Release\\vcruntime140_1.dll",
        .certificate_thumbprint = L"0123456789ABCDEF",
    };
}

void test_probe_plan_is_ordered_and_rollback_reverses_external_changes() {
    const auto plan = mcfix::installer::make_probe_plan(valid_probe_inputs());
    const std::vector expected_operations{
        mcfix::installer::Operation::stage_files,
        mcfix::installer::Operation::create_certificate,
        mcfix::installer::Operation::pack,
        mcfix::installer::Operation::sign,
        mcfix::installer::Operation::register_package,
        mcfix::installer::Operation::verify_registration,
    };
    const std::vector expected_rollback{
        mcfix::installer::RollbackOperation::remove_package,
        mcfix::installer::RollbackOperation::remove_certificate,
        mcfix::installer::RollbackOperation::remove_stage,
    };
    expect(plan.allowed && plan.operations == expected_operations,
           "probe install operations must be deterministic");
    expect(plan.rollback == expected_rollback,
           "probe rollback must reverse registered package, certificate, and stage state");
}

void test_probe_plan_refuses_unsafe_state_and_unbounded_paths() {
    auto inputs = valid_probe_inputs();
    inputs.package_state = mcfix::installer::PackageState::existing_unmanaged_proxy;
    expect(mcfix::installer::make_probe_plan(inputs).reason ==
               mcfix::installer::ProbePlanReason::unsafe_package_state,
           "an unmanaged existing proxy must refuse the mutating plan");

    inputs = valid_probe_inputs();
    inputs.bridge_path = L"relative\\vcruntime140_1.dll";
    expect(mcfix::installer::make_probe_plan(inputs).reason ==
               mcfix::installer::ProbePlanReason::non_absolute_path,
           "every probe input path must be absolute");

    inputs = valid_probe_inputs();
    inputs.stage_path = L"C:\\Windows\\Temp\\mcfix-package";
    expect(mcfix::installer::make_probe_plan(inputs).reason ==
               mcfix::installer::ProbePlanReason::stage_outside_root,
           "the package stage must stay under the validated probe root");

    inputs = valid_probe_inputs();
    inputs.makeappx_path =
        L"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.22621.0\\x64\\makeappx.exe";
    expect(mcfix::installer::make_probe_plan(inputs).reason ==
               mcfix::installer::ProbePlanReason::unpinned_sdk_tool,
           "the feasibility package must use the pinned Windows SDK tools");
}

void test_hidden_runner_captures_exact_exit_code() {
    const mcfix::installer::ProcessRequest request{
        .executable = L"C:\\Windows\\System32\\cmd.exe",
        .arguments = {L"/d", L"/c", L"exit 7"},
        .timeout_ms = 2000,
    };
    const auto result = mcfix::installer::run_hidden(request);
    expect(result.launched && !result.timed_out && result.exit_code == 7,
           "the hidden runner must capture the exact child exit code");
}

void test_hidden_runner_captures_output_and_bounds_process_lifetime() {
    const mcfix::installer::ProcessRequest echo_request{
        .executable = L"C:\\Windows\\System32\\cmd.exe",
        .arguments = {L"/d", L"/c", L"echo mcfix-probe-output"},
        .timeout_ms = 2000,
    };
    const auto echoed = mcfix::installer::run_hidden(echo_request);
    expect(echoed.launched && echoed.output.find("mcfix-probe-output") != std::string::npos,
           "the hidden runner must capture bounded child output");

    const mcfix::installer::ProcessRequest timeout_request{
        .executable = L"C:\\Windows\\System32\\cmd.exe",
        .arguments = {L"/d", L"/c", L"ping -n 6 127.0.0.1 >nul"},
        .timeout_ms = 100,
    };
    const auto timed = mcfix::installer::run_hidden(timeout_request);
    expect(timed.launched && timed.timed_out,
           "a child process tree that exceeds its bound must time out");

    auto missing_request = echo_request;
    missing_request.executable = L"C:\\Windows\\System32\\mcfix-does-not-exist.exe";
    const auto missing = mcfix::installer::run_hidden(missing_request);
    expect(!missing.launched && missing.win32_error != 0,
           "a missing executable must return a launch error without a child process");
}

void test_patcher_commands_require_exact_confirmation() {
    using mcfix::installer::CommandKind;
    expect(mcfix::installer::parse_patcher_command({}).kind == CommandKind::invalid,
           "an empty patcher command must be invalid");
    expect(mcfix::installer::parse_patcher_command({L"probe-status"}).kind ==
               CommandKind::probe_status,
           "probe-status must select the read-only status command");
    expect(mcfix::installer::parse_patcher_command({L"probe-stage"}).kind ==
               CommandKind::probe_stage,
           "probe-stage must select the workspace-only staging command");
    expect(mcfix::installer::parse_patcher_command({L"probe-install"}).kind ==
               CommandKind::invalid,
           "probe-install without confirmation must be refused");
    expect(mcfix::installer::parse_patcher_command({L"probe-install", L"--confirm"}).kind ==
               CommandKind::probe_install,
           "the exact confirmation token must enable only probe install");
    expect(mcfix::installer::parse_patcher_command({L"probe-uninstall", L"--confirm"}).kind ==
               CommandKind::probe_uninstall,
           "the exact confirmation token must enable only probe uninstall");
    expect(mcfix::installer::parse_patcher_command({L"probe-stage", L"extra"}).kind ==
               CommandKind::invalid,
           "extra patcher arguments must be rejected");
}

void test_shipping_patcher_commands_are_strict_and_preserve_silent_mode() {
    using mcfix::installer::CommandKind;
    auto parsed = mcfix::installer::parse_patcher_command({L"status"});
    expect(parsed.kind == CommandKind::status && !parsed.silent,
           "status must be a read-only non-silent command");
    parsed = mcfix::installer::parse_patcher_command({L"verify"});
    expect(parsed.kind == CommandKind::verify,
           "verify must select the shipping verification command");
    parsed = mcfix::installer::parse_patcher_command({L"verify", L"--silent"});
    expect(parsed.kind == CommandKind::verify && parsed.silent,
           "verify --silent must preserve the watchdog's hidden health check");
    parsed = mcfix::installer::parse_patcher_command({L"install"});
    expect(parsed.kind == CommandKind::install,
           "install must select the user-initiated shipping installer");
    parsed = mcfix::installer::parse_patcher_command({L"repair", L"--silent"});
    expect(parsed.kind == CommandKind::repair && parsed.silent,
           "repair --silent must preserve background mode");
    parsed = mcfix::installer::parse_patcher_command({L"watch", L"--silent"});
    expect(parsed.kind == CommandKind::watch && parsed.silent,
           "watch --silent must select the hidden low-resource watcher");
    parsed = mcfix::installer::parse_patcher_command({L"uninstall"});
    expect(parsed.kind == CommandKind::uninstall,
           "uninstall must select exact rollback");
    expect(mcfix::installer::parse_patcher_command({L"install", L"--confirm"}).kind ==
               CommandKind::invalid,
           "probe confirmation flags must not be accepted by shipping commands");
}

void test_sha256_matches_the_standard_abc_vector() {
    const std::string bytes = "abc";
    expect(mcfix::installer::sha256_hex(bytes) ==
               "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
           "SHA-256 must match the standard abc test vector");
}

void test_sha256_file_streams_the_same_standard_vector() {
    const auto path = std::filesystem::temp_directory_path() / "mcfix-sha256-file-test.bin";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "abc";
    }
    const auto digest = mcfix::installer::sha256_file(path);
    std::error_code error;
    std::filesystem::remove(path, error);
    expect(digest == "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
           "streamed file SHA-256 must match the standard abc test vector");
}

mcfix::installer::DirectInstallFacts clean_direct_facts() {
    return {
        .minecraft_running = false,
        .proxy_payload_exists = true,
        .proxy_payload_looks_forwarder = true,
        .camera_payload_exists = true,
        .current_runtime_exists = true,
        .current_runtime_is_proxy_payload = false,
        .current_runtime_is_known_managed_proxy = false,
        .current_runtime_looks_original = true,
        .managed_original_exists = false,
        .managed_original_looks_original = false,
        .legacy_original_exists = false,
        .legacy_original_looks_original = false,
        .installed_camera_exists = false,
        .installed_camera_matches_payload = false,
    };
}

void test_direct_install_plan_handles_clean_legacy_and_managed_layouts() {
    using mcfix::installer::DirectInstallOperation;
    using mcfix::installer::DirectLayout;

    auto plan = mcfix::installer::make_direct_install_plan(clean_direct_facts());
    expect(plan.allowed && plan.layout == DirectLayout::clean_store,
           "a non-forwarded packaged runtime is a clean direct-install baseline");
    expect(plan.operations == std::vector<DirectInstallOperation>{
               DirectInstallOperation::preserve_current_runtime,
               DirectInstallOperation::install_proxy,
               DirectInstallOperation::install_camera_patch,
               DirectInstallOperation::write_install_state},
           "clean install must preserve the Store runtime before installing MCFIX");

    auto legacy = clean_direct_facts();
    legacy.current_runtime_looks_original = false;
    legacy.legacy_original_exists = true;
    legacy.legacy_original_looks_original = true;
    plan = mcfix::installer::make_direct_install_plan(legacy);
    expect(plan.allowed && plan.layout == DirectLayout::legacy_proxy,
           "a valid legacy _orig runtime must be safely migratable");
    expect(plan.operations.front() == DirectInstallOperation::preserve_legacy_original,
           "legacy migration must preserve the real runtime, not the old proxy");

    auto managed = clean_direct_facts();
    managed.current_runtime_is_proxy_payload = true;
    managed.current_runtime_looks_original = false;
    managed.managed_original_exists = true;
    managed.managed_original_looks_original = true;
    managed.installed_camera_exists = true;
    managed.installed_camera_matches_payload = true;
    plan = mcfix::installer::make_direct_install_plan(managed);
    expect(plan.allowed && plan.layout == DirectLayout::managed_healthy && plan.operations.empty(),
           "an exact managed install must be a no-op");

    managed.installed_camera_exists = false;
    managed.installed_camera_matches_payload = false;
    plan = mcfix::installer::make_direct_install_plan(managed);
    expect(plan.allowed && plan.layout == DirectLayout::managed_needs_repair &&
               plan.operations == std::vector<DirectInstallOperation>{
                   DirectInstallOperation::install_camera_patch,
                   DirectInstallOperation::write_install_state},
           "repair must restore only a missing camera payload when the proxy is healthy");

    managed.current_runtime_is_proxy_payload = false;
    managed.current_runtime_is_known_managed_proxy = true;
    plan = mcfix::installer::make_direct_install_plan(managed);
    expect(plan.allowed && plan.layout == DirectLayout::managed_needs_repair &&
               plan.operations == std::vector<DirectInstallOperation>{
                   DirectInstallOperation::install_proxy,
                   DirectInstallOperation::install_camera_patch,
                   DirectInstallOperation::write_install_state},
           "a durable hash match must allow a managed proxy upgrade");
}

void test_direct_install_plan_fails_closed_on_unknown_or_incomplete_originals() {
    using mcfix::installer::DirectLayout;

    auto facts = clean_direct_facts();
    facts.minecraft_running = true;
    auto plan = mcfix::installer::make_direct_install_plan(facts);
    expect(!plan.allowed && plan.layout == DirectLayout::minecraft_running,
           "direct installation must stop while Minecraft is running");

    facts = clean_direct_facts();
    facts.camera_payload_exists = false;
    plan = mcfix::installer::make_direct_install_plan(facts);
    expect(!plan.allowed && plan.layout == DirectLayout::payload_missing,
           "direct installation must refuse an incomplete release payload");

    facts = clean_direct_facts();
    facts.proxy_payload_looks_forwarder = false;
    plan = mcfix::installer::make_direct_install_plan(facts);
    expect(!plan.allowed && plan.layout == DirectLayout::invalid_proxy_payload,
           "a release payload without the exact forwarding contract must be refused");

    facts = clean_direct_facts();
    facts.current_runtime_looks_original = false;
    plan = mcfix::installer::make_direct_install_plan(facts);
    expect(!plan.allowed && plan.layout == DirectLayout::unknown_runtime,
           "an unknown current runtime without a valid preserved original must be refused");

    facts = clean_direct_facts();
    facts.current_runtime_is_proxy_payload = true;
    facts.current_runtime_looks_original = false;
    facts.managed_original_exists = true;
    facts.managed_original_looks_original = false;
    plan = mcfix::installer::make_direct_install_plan(facts);
    expect(!plan.allowed && plan.layout == DirectLayout::invalid_managed_original,
           "a corrupt managed original must never be used for repair or rollback");
}

void test_runtime_contract_distinguishes_original_code_from_a_forwarding_proxy(
    const std::filesystem::path& original,
    const std::filesystem::path& proxy) {
    const auto original_contract = mcfix::installer::inspect_runtime_contract(original);
    expect(original_contract.valid_x64_dll && original_contract.handler_export_count == 1 &&
               !original_contract.handler_is_forwarded && original_contract.looks_original(),
           "the test runtime with a direct handler must satisfy the original-runtime contract");

    const auto proxy_contract = mcfix::installer::inspect_runtime_contract(proxy);
    expect(proxy_contract.valid_x64_dll && proxy_contract.handler_export_count == 1 &&
               proxy_contract.handler_is_forwarded && !proxy_contract.looks_original(),
           "the MCFIX forwarding proxy must never be preserved as the original runtime");
}

std::filesystem::path unique_test_directory(std::string_view label) {
    const auto name = std::string("mcfix-") + std::string(label) + "-" +
        std::to_string(GetCurrentProcessId());
    const auto path = std::filesystem::temp_directory_path() / name;
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path);
    return path;
}

void test_direct_transaction_installs_repairs_and_uninstalls_atomically(
    const std::filesystem::path& original_fixture,
    const std::filesystem::path& proxy_fixture) {
    const auto root = unique_test_directory("direct-transaction");
    const auto payload_root = root / "payload";
    const auto target_root = root / "target";
    std::filesystem::create_directories(payload_root);
    std::filesystem::create_directories(target_root);
    const auto proxy_payload = payload_root / "vcruntime140_1.dll";
    const auto camera_payload = payload_root / "MCFIXCameraPatch.dll";
    std::filesystem::copy_file(proxy_fixture, proxy_payload);
    std::filesystem::copy_file(original_fixture, target_root / "vcruntime140_1.dll");
    {
        std::ofstream camera(camera_payload, std::ios::binary);
        camera << "mcfix-camera-payload-v1";
    }

    auto result = mcfix::installer::install_direct_patch(
        target_root, proxy_payload, camera_payload, false);
    expect(result.ok && result.changed,
           "clean direct installation must complete and report a mutation");
    expect(mcfix::installer::sha256_file(target_root / "vcruntime140_1.dll") ==
               mcfix::installer::sha256_file(proxy_payload),
           "direct installation must commit the exact proxy payload");
    expect(mcfix::installer::sha256_file(
               target_root / "vcruntime140_1_mcfix_original.dll") ==
               mcfix::installer::sha256_file(original_fixture),
           "direct installation must retain the exact original runtime");
    expect(mcfix::installer::sha256_file(target_root / "MCFIXCameraPatch.dll") ==
               mcfix::installer::sha256_file(camera_payload),
           "direct installation must commit the exact camera payload");

    std::filesystem::remove(target_root / "MCFIXCameraPatch.dll");
    result = mcfix::installer::install_direct_patch(
        target_root, proxy_payload, camera_payload, false);
    expect(result.ok && result.changed &&
               std::filesystem::is_regular_file(target_root / "MCFIXCameraPatch.dll"),
           "repair must restore a missing camera payload");

    result = mcfix::installer::uninstall_direct_patch(target_root, proxy_payload, false);
    expect(result.ok && result.changed,
           "managed direct installation must uninstall cleanly");
    expect(mcfix::installer::sha256_file(target_root / "vcruntime140_1.dll") ==
               mcfix::installer::sha256_file(original_fixture),
           "uninstall must restore the byte-exact original runtime");
    expect(!std::filesystem::exists(target_root / "vcruntime140_1_mcfix_original.dll") &&
               !std::filesystem::exists(target_root / "MCFIXCameraPatch.dll"),
           "uninstall must remove only the two MCFIX-managed sibling files");

    result = mcfix::installer::uninstall_direct_patch(target_root, proxy_payload, false);
    expect(result.ok && !result.changed,
           "uninstall must be idempotent when an update or earlier uninstall already restored stock");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_direct_transaction_migrates_a_valid_legacy_original(
    const std::filesystem::path& original_fixture,
    const std::filesystem::path& proxy_fixture) {
    const auto root = unique_test_directory("legacy-transaction");
    const auto payload_root = root / "payload";
    const auto target_root = root / "target";
    std::filesystem::create_directories(payload_root);
    std::filesystem::create_directories(target_root);
    const auto proxy_payload = payload_root / "vcruntime140_1.dll";
    const auto camera_payload = payload_root / "MCFIXCameraPatch.dll";
    std::filesystem::copy_file(proxy_fixture, proxy_payload);
    std::filesystem::copy_file(proxy_fixture, target_root / "vcruntime140_1.dll");
    std::filesystem::copy_file(original_fixture, target_root / "vcruntime140_1_orig.dll");
    {
        std::ofstream camera(camera_payload, std::ios::binary);
        camera << "mcfix-camera-payload-v1";
    }

    const auto result = mcfix::installer::install_direct_patch(
        target_root, proxy_payload, camera_payload, false);
    expect(result.ok && result.changed &&
               mcfix::installer::sha256_file(
                   target_root / "vcruntime140_1_mcfix_original.dll") ==
                   mcfix::installer::sha256_file(original_fixture),
           "legacy migration must preserve the validated _orig runtime under the managed name");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_directory_access_guard_restores_the_exact_owner_and_dacl() {
    const auto root = unique_test_directory("directory-access");
    const auto before = mcfix::installer::directory_security_sddl(root);
    auto access = mcfix::installer::DirectoryAccessGuard::acquire(root);
    expect(access.active(), "directory access guard must acquire a writable test directory");
    expect(access.restore(), "directory access guard must explicitly restore security");
    const auto after = mcfix::installer::directory_security_sddl(root);
    if (before != after) {
        std::wcerr << L"directory ACL before: " << before << L"\n"
                   << L"directory ACL after:  " << after << L"\n";
    }
    expect(!before.empty() && before == after,
           "directory access guard must restore the byte-equivalent owner and DACL SDDL");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_file_security_copy_matches_the_runtime_template() {
    const auto root = unique_test_directory("file-security");
    const auto source = root / "source.dll";
    const auto target = root / "target.dll";
    {
        std::ofstream(source, std::ios::binary) << "source";
        std::ofstream(target, std::ios::binary) << "target";
    }
    expect(mcfix::installer::copy_file_security(source, target),
           "file security copy must succeed for writable fixtures");
    expect(mcfix::installer::file_security_sddl(source) ==
               mcfix::installer::file_security_sddl(target),
           "new MCFIX files must receive the same owner and DACL as the original runtime");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_watchdog_task_policy_is_persistent_hidden_and_battery_safe() {
    constexpr auto policy = mcfix::installer::watchdog_task_policy();
    expect(policy.execution_time_limit == L"PT0S",
           "watchdog task must never expire after the Task Scheduler default 72 hours");
    expect(policy.hidden && policy.start_when_available,
           "watchdog task must be hidden and start when a logon trigger was missed");
    expect(!policy.disallow_start_on_battery && !policy.stop_on_battery,
           "watchdog task must remain available on laptops and battery power");
}

}  // namespace

int main(int argc, char** argv) {
    test_clean_package_state_is_installable();
    test_existing_proxy_is_reported_not_overwritten();
    test_each_unsafe_package_fact_has_a_specific_refusal();
    test_manifest_binds_only_to_minecraft_main_package();
    test_manifest_escapes_attributes_and_rejects_invalid_contracts();
    test_probe_plan_is_ordered_and_rollback_reverses_external_changes();
    test_probe_plan_refuses_unsafe_state_and_unbounded_paths();
    test_hidden_runner_captures_exact_exit_code();
    test_hidden_runner_captures_output_and_bounds_process_lifetime();
    test_patcher_commands_require_exact_confirmation();
    test_shipping_patcher_commands_are_strict_and_preserve_silent_mode();
    test_sha256_matches_the_standard_abc_vector();
    test_sha256_file_streams_the_same_standard_vector();
    test_direct_install_plan_handles_clean_legacy_and_managed_layouts();
    test_direct_install_plan_fails_closed_on_unknown_or_incomplete_originals();
    if (argc == 3) {
        test_runtime_contract_distinguishes_original_code_from_a_forwarding_proxy(argv[1], argv[2]);
        test_direct_transaction_installs_repairs_and_uninstalls_atomically(argv[1], argv[2]);
        test_direct_transaction_migrates_a_valid_legacy_original(argv[1], argv[2]);
        test_directory_access_guard_restores_the_exact_owner_and_dacl();
        test_file_security_copy_matches_the_runtime_template();
        test_watchdog_task_policy_is_persistent_hidden_and_battery_safe();
    } else {
        expect(false, "installer tests require original-runtime and proxy fixture paths");
    }

    if (failures != 0) {
        std::cerr << failures << " installer test assertion(s) failed\n";
        return 1;
    }
    std::cout << "all installer tests passed\n";
    return 0;
}
