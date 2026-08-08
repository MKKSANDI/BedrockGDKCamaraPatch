cmake_minimum_required(VERSION 3.24)

project(MCFIXCameraPatch LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "MCFIX camera telemetry supports Windows x64 only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "Suppress pinned dependency deprecation warnings" FORCE)

enable_testing()
add_subdirectory(tests)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies as static libraries" FORCE)
add_subdirectory(
    ${PROJECT_SOURCE_DIR}/../third_party/minhook
    ${CMAKE_BINARY_DIR}/minhook
    EXCLUDE_FROM_ALL
)

add_library(MCFIXCameraPatch SHARED
    src/capture_limit.cpp
    src/patch_dll.cpp
    src/hook_callbacks.cpp
    src/hook_targets.cpp
    src/signature.cpp
    src/telemetry.cpp
    src/telemetry_writer.cpp
    src/turn_distributor.cpp
    src/cadence_classifier.cpp
    src/target_policy.cpp
)

target_include_directories(MCFIXCameraPatch PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(MCFIXCameraPatch PRIVATE minhook)
target_compile_definitions(MCFIXCameraPatch PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(MCFIXCameraPatch PRIVATE /W4 /permissive-)
set_property(TARGET MCFIXCameraPatch minhook PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

add_library(MCFIXBootstrapProbe SHARED
    src/bootstrap/bootstrap_bridge.cpp
    src/bootstrap/bootstrap_bridge.def
    src/bootstrap/bootstrap_contract.cpp
)
set_target_properties(MCFIXBootstrapProbe PROPERTIES
    OUTPUT_NAME "vcruntime140_1_probe"
)
target_include_directories(MCFIXBootstrapProbe PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(MCFIXBootstrapProbe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(MCFIXBootstrapProbe PRIVATE /W4 /permissive-)
set_property(TARGET MCFIXBootstrapProbe PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

add_library(MCFIXRuntimeProxy SHARED
    src/bootstrap/runtime_proxy.cpp
    src/bootstrap/runtime_proxy.def
    src/bootstrap/proxy_contract.cpp
)
set_target_properties(MCFIXRuntimeProxy PROPERTIES OUTPUT_NAME "vcruntime140_1")
target_include_directories(MCFIXRuntimeProxy PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(MCFIXRuntimeProxy PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(MCFIXRuntimeProxy PRIVATE /W4 /permissive-)
set_property(TARGET MCFIXRuntimeProxy PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

add_executable(MCFIXBootstrapProbeHost
    src/bootstrap/probe_host.cpp
    src/bootstrap/bootstrap_contract.cpp
)
target_include_directories(MCFIXBootstrapProbeHost PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(MCFIXBootstrapProbeHost PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(MCFIXBootstrapProbeHost PRIVATE /W4 /permissive-)
set_property(TARGET MCFIXBootstrapProbeHost PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

add_test(
    NAME mcfix_bootstrap_probe_host
    COMMAND MCFIXBootstrapProbeHost $<TARGET_FILE:MCFIXBootstrapProbe>
)

add_executable(Patcher
    src/installer/patcher_main.cpp
    src/installer/package_probe.cpp
    src/installer/package_state.cpp
    src/installer/manifest.cpp
    src/installer/probe_plan.cpp
    src/installer/probe_runner.cpp
    src/installer/patcher_command.cpp
    src/installer/file_hash.cpp
    src/installer/direct_plan.cpp
    src/installer/direct_transaction.cpp
    src/installer/runtime_contract.cpp
    src/installer/directory_access.cpp
    src/installer/shipping_installer.cpp
    src/installer/watchdog.cpp
    src/installer/watchdog_task.cpp
    src/bootstrap/proxy_contract.cpp
)
target_include_directories(Patcher PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(Patcher PRIVATE bcrypt windowsapp advapi32 shell32 wbemuuid ole32 oleaut32 taskschd comsuppw)
target_compile_definitions(Patcher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(Patcher PRIVATE /W4 /permissive-)
set_property(TARGET Patcher PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

add_executable(MCFIXWatchdog WIN32
    src/installer/watchdog_app.cpp
    src/bootstrap/proxy_contract.cpp
)
target_include_directories(MCFIXWatchdog PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(MCFIXWatchdog PRIVATE wbemuuid ole32 oleaut32)
target_compile_definitions(MCFIXWatchdog PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(MCFIXWatchdog PRIVATE /W4 /permissive-)
set_property(TARGET MCFIXWatchdog PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
add_dependencies(Patcher MCFIXWatchdog MCFIXRuntimeProxy MCFIXCameraPatch)

add_test(NAME mcfix_patcher_probe_status COMMAND Patcher probe-status)
set_tests_properties(mcfix_patcher_probe_status PROPERTIES
    PASS_REGULAR_EXPRESSION "\\\"command\\\":\\\"probe-status\\\""
)
add_test(NAME mcfix_patcher_probe_stage COMMAND Patcher probe-stage)
set_tests_properties(mcfix_patcher_probe_stage PROPERTIES
    PASS_REGULAR_EXPRESSION "\\\"command\\\":\\\"probe-stage\\\""
)
