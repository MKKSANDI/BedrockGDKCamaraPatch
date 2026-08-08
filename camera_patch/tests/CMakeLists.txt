add_executable(mcfix_patch_core_tests
    test_core.cpp
    ${PROJECT_SOURCE_DIR}/src/signature.cpp
    ${PROJECT_SOURCE_DIR}/src/target_policy.cpp
    ${PROJECT_SOURCE_DIR}/src/telemetry.cpp
    ${PROJECT_SOURCE_DIR}/src/hook_targets.cpp
    ${PROJECT_SOURCE_DIR}/src/hook_callbacks.cpp
    ${PROJECT_SOURCE_DIR}/src/loader_args.cpp
    ${PROJECT_SOURCE_DIR}/src/loader_safety.cpp
    ${PROJECT_SOURCE_DIR}/src/capture_limit.cpp
    ${PROJECT_SOURCE_DIR}/src/pe_image.cpp
    ${PROJECT_SOURCE_DIR}/src/turn_distributor.cpp
    ${PROJECT_SOURCE_DIR}/src/cadence_classifier.cpp
)

target_include_directories(mcfix_patch_core_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)

add_test(NAME mcfix_patch_core_tests COMMAND mcfix_patch_core_tests)

add_executable(mcfix_installer_tests
    test_installer.cpp
    ${PROJECT_SOURCE_DIR}/src/installer/package_state.cpp
    ${PROJECT_SOURCE_DIR}/src/installer/manifest.cpp
    ${PROJECT_SOURCE_DIR}/src/installer/probe_plan.cpp
    ${PROJECT_SOURCE_DIR}/src/installer/probe_runner.cpp
    ${PROJECT_SOURCE_DIR}/src/installer/patcher_command.cpp
    ${PROJECT_SOURCE_DIR}/src/installer/file_hash.cpp
    ${PROJECT_SOURCE_DIR}/src/installer/direct_plan.cpp
    ${PROJECT_SOURCE_DIR}/src/installer/runtime_contract.cpp
    ${PROJECT_SOURCE_DIR}/src/installer/direct_transaction.cpp
    ${PROJECT_SOURCE_DIR}/src/installer/directory_access.cpp
    ${PROJECT_SOURCE_DIR}/src/installer/watchdog_task.cpp
)

target_include_directories(mcfix_installer_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(mcfix_installer_tests PRIVATE bcrypt advapi32 ole32 oleaut32 taskschd comsuppw)

add_test(
    NAME mcfix_installer_tests
    COMMAND mcfix_installer_tests
        $<TARGET_FILE:MCFIXFakeRuntime>
        $<TARGET_FILE:MCFIXRuntimeProxy>
)

add_executable(mcfix_bootstrap_tests
    test_bootstrap.cpp
    ${PROJECT_SOURCE_DIR}/src/bootstrap/bootstrap_contract.cpp
    ${PROJECT_SOURCE_DIR}/src/bootstrap/proxy_contract.cpp
)

target_include_directories(mcfix_bootstrap_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

add_test(NAME mcfix_bootstrap_tests COMMAND mcfix_bootstrap_tests)

add_library(MCFIXFakeRuntime SHARED fake_runtime.cpp fake_runtime.def)
set_target_properties(MCFIXFakeRuntime PROPERTIES
    OUTPUT_NAME "vcruntime140_1_mcfix_original"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_executable(MCFIXRuntimeProxyTestHost proxy_host.cpp)
set_property(TARGET MCFIXRuntimeProxyTestHost PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
add_dependencies(MCFIXRuntimeProxyTestHost MCFIXRuntimeProxy MCFIXFakeRuntime)
add_dependencies(mcfix_installer_tests MCFIXRuntimeProxy MCFIXFakeRuntime)
add_test(
    NAME mcfix_runtime_proxy_host
    COMMAND MCFIXRuntimeProxyTestHost $<TARGET_FILE:MCFIXRuntimeProxy>
)
