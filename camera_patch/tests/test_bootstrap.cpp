#include "bootstrap/bootstrap_contract.hpp"
#include "bootstrap/proxy_contract.hpp"

#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_heartbeat_names_are_pid_scoped() {
    expect(mcfix::bootstrap::heartbeat_event_name(1234) ==
               L"Local\\MCFIX.BootstrapProbe.1234",
           "probe heartbeat must be local and PID scoped");
}

void test_heartbeat_record_stays_in_mcfix_local_state() {
    expect(mcfix::bootstrap::heartbeat_record_path(L"C:\\Users\\tester\\AppData\\Local") ==
               L"C:\\Users\\tester\\AppData\\Local\\MCFIX\\probe-heartbeat.json",
           "probe heartbeat record must stay in the MCFIX-owned LocalAppData directory");
}

void test_proxy_uses_fixed_sibling_payload_names() {
    expect(mcfix::bootstrap::original_runtime_name() ==
               L"vcruntime140_1_mcfix_original.dll",
           "proxy must forward to the MCFIX-owned original-runtime sibling");
    expect(mcfix::bootstrap::camera_patch_name() == L"MCFIXCameraPatch.dll",
           "proxy must load only the fixed MCFIX camera patch sibling");
}

void test_proxy_derives_payload_path_from_its_own_module() {
    expect(mcfix::bootstrap::sibling_payload_path(
               LR"(C:\Program Files\WindowsApps\Minecraft\vcruntime140_1.dll)",
               L"MCFIXCameraPatch.dll") ==
               LR"(C:\Program Files\WindowsApps\Minecraft\MCFIXCameraPatch.dll)",
           "proxy payload path must remain beside the proxy");
}

void test_proxy_loads_patch_only_in_minecraft() {
    expect(mcfix::bootstrap::should_load_camera_patch(
               LR"(C:\Program Files\WindowsApps\Minecraft\Minecraft.Windows.exe)"),
           "proxy must load the patch inside Minecraft.Windows.exe");
    expect(mcfix::bootstrap::should_load_camera_patch(L"MINECRAFT.WINDOWS.EXE"),
           "process-name comparison must be case insensitive");
    expect(!mcfix::bootstrap::should_load_camera_patch(L"GameLaunchHelper.exe"),
           "proxy must not load the camera patch into the launch helper");
}

void test_runtime_proxy_heartbeat_is_pid_scoped() {
    expect(mcfix::bootstrap::camera_patch_heartbeat_event_name(1234) ==
               L"Local\\MCFIX.CameraPatch.1234",
           "camera-patch bootstrap heartbeat must be local and PID scoped");
}

}  // namespace

int main() {
    test_heartbeat_names_are_pid_scoped();
    test_heartbeat_record_stays_in_mcfix_local_state();
    test_proxy_uses_fixed_sibling_payload_names();
    test_proxy_derives_payload_path_from_its_own_module();
    test_proxy_loads_patch_only_in_minecraft();
    test_runtime_proxy_heartbeat_is_pid_scoped();

    if (failures != 0) {
        std::cerr << failures << " bootstrap test assertion(s) failed\n";
        return 1;
    }
    std::cout << "all bootstrap tests passed\n";
    return 0;
}
