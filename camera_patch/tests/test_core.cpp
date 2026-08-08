#include "hook_targets.hpp"
#include "hook_callbacks.hpp"
#include "capture_limit.hpp"
#include "loader_args.hpp"
#include "loader_safety.hpp"
#include "pe_image.hpp"
#include "turn_distributor.hpp"
#include "cadence_classifier.hpp"
#include "mcfix/signature.hpp"
#include "mcfix/target_policy.hpp"
#include "mcfix/telemetry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <iostream>
#include <span>
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

void expect_near(float actual, float expected, std::string_view message) {
    expect(std::fabs(actual - expected) < 0.00001F, message);
}

void test_signature_scanner_reports_every_match() {
    const auto pattern = mcfix::parse_pattern("48 8B ?? 10");
    const std::array<std::byte, 9> bytes{
        std::byte{0x48}, std::byte{0x8B}, std::byte{0x01}, std::byte{0x10},
        std::byte{0x90},
        std::byte{0x48}, std::byte{0x8B}, std::byte{0xFF}, std::byte{0x10},
    };

    const auto matches = mcfix::find_all(std::span{bytes}, pattern);
    expect(matches.size() == 2, "duplicate signatures must never collapse to the first match");
    expect(matches.size() >= 2 && matches[0] == 0 && matches[1] == 5,
           "signature match offsets must be exact");
}

void test_signature_parser_rejects_malformed_tokens() {
    bool rejected = false;
    try {
        static_cast<void>(mcfix::parse_pattern("48 8G 10"));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "malformed hex tokens must be rejected");
}

mcfix::TargetFacts valid_target() {
    return mcfix::TargetFacts{
        .process_count = 1,
        .executable_name = L"Minecraft.Windows.exe",
        .package_full_name = L"Microsoft.MinecraftUWP_1.26.4201.0_x64__8wekyb3d8bbwe",
        .modules_enumerated = true,
        .loaded_modules = {L"Minecraft.Windows.exe", L"GameInputRedist.dll"},
    };
}

void test_target_policy_accepts_matching_minecraft_x64_updates_but_not_other_packages() {
    const auto accepted = mcfix::evaluate_target(valid_target());
    expect(accepted.allowed && accepted.reason == mcfix::TargetReason::none,
           "the validated clean 1.26.4201.0 package must be accepted");

    auto newer_version = valid_target();
    newer_version.package_full_name = L"Microsoft.MinecraftUWP_1.27.100.0_x64__8wekyb3d8bbwe";
    expect(mcfix::evaluate_target(newer_version).allowed,
           "a future version with the exact Minecraft x64 package identity must reach signature validation");

    auto wrong_architecture = valid_target();
    wrong_architecture.package_full_name =
        L"Microsoft.MinecraftUWP_1.26.4201.0_arm64__8wekyb3d8bbwe";
    expect(mcfix::evaluate_target(wrong_architecture).reason == mcfix::TargetReason::package_mismatch,
           "a non-x64 Minecraft package must be refused");

    auto wrong_family = valid_target();
    wrong_family.package_full_name = L"Microsoft.MinecraftUWP_1.26.4201.0_x64__untrusted";
    expect(mcfix::evaluate_target(wrong_family).reason == mcfix::TargetReason::package_mismatch,
           "an untrusted publisher family must be refused");

    auto unpackaged = valid_target();
    unpackaged.package_full_name.clear();
    expect(mcfix::evaluate_target(unpackaged).reason == mcfix::TargetReason::package_missing,
           "an unpackaged process must be refused");

    auto multiple = valid_target();
    multiple.process_count = 2;
    expect(mcfix::evaluate_target(multiple).reason == mcfix::TargetReason::process_count,
           "multiple Minecraft targets must be refused");

    auto opaque = valid_target();
    opaque.modules_enumerated = false;
    expect(mcfix::evaluate_target(opaque).reason == mcfix::TargetReason::module_enumeration_failed,
           "failure to inspect loaded modules must refuse the target");
}

void test_target_policy_rejects_conflicting_hooks() {
    struct Case {
        std::wstring module;
        mcfix::TargetReason reason;
    };
    const std::array cases{
        Case{L"C:\\Users\\tester\\AppData\\Local\\Flarial\\Flarial.Client.Release.dll",
             mcfix::TargetReason::flarial_loaded},
        Case{L"C:\\Users\\tester\\Desktop\\MCFIX\\fix.dll", mcfix::TargetReason::obsolete_patch_loaded},
        Case{L"C:\\temp\\cam_final.dll", mcfix::TargetReason::obsolete_patch_loaded},
        Case{L"C:\\temp\\MCFIXCameraPatch.dll", mcfix::TargetReason::patch_already_loaded},
    };

    for (const auto& item : cases) {
        auto facts = valid_target();
        facts.loaded_modules.push_back(item.module);
        expect(mcfix::evaluate_target(facts).reason == item.reason,
               "every conflicting hook module must be refused by basename");
    }
}

mcfix::TelemetryEvent event_with_qpc(std::uint64_t qpc) {
    return mcfix::TelemetryEvent{
        .qpc = qpc,
        .thread_id = 7,
        .kind = mcfix::EventKind::turn_delta,
        .x = static_cast<float>(qpc),
        .y = -static_cast<float>(qpc),
    };
}

void test_telemetry_ring_drains_committed_events_in_order() {
    mcfix::TelemetryRing<4> ring;
    for (std::uint64_t qpc = 1; qpc <= 4; ++qpc) {
        ring.push(event_with_qpc(qpc));
    }

    std::vector<mcfix::TelemetryEvent> drained;
    ring.drain([&](const mcfix::TelemetryEvent& event) { drained.push_back(event); });

    expect(drained.size() == 4, "a four-slot ring must drain four committed events");
    for (std::size_t index = 0; index < drained.size(); ++index) {
        expect(drained[index].sequence == index + 1, "drained event sequence must be monotonic");
        expect(drained[index].qpc == index + 1, "drained event payload must remain intact");
    }
    expect(ring.dropped() == 0, "an unoverflowed ring must not report drops");
}

void test_telemetry_ring_reports_overwritten_events() {
    mcfix::TelemetryRing<4> ring;
    for (std::uint64_t qpc = 1; qpc <= 5; ++qpc) {
        ring.push(event_with_qpc(qpc));
    }

    std::vector<mcfix::TelemetryEvent> drained;
    ring.drain([&](const mcfix::TelemetryEvent& event) { drained.push_back(event); });

    expect(ring.dropped() == 1, "one overwritten event must increment the drop count once");
    expect(drained.size() == 4, "the ring must retain exactly its newest four events");
    expect(!drained.empty() && drained.front().qpc == 2 && drained.back().qpc == 5,
           "overflow must retain the newest complete event payloads");
}

void insert_pattern(std::vector<std::byte>& bytes, std::size_t offset, std::string_view text) {
    const auto pattern = mcfix::parse_pattern(text);
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        bytes[offset + index] = pattern[index].wildcard ? std::byte{0x44} : pattern[index].value;
    }
}

std::vector<std::byte> valid_text_fixture() {
    std::vector<std::byte> bytes(1024, std::byte{0x90});
    insert_pattern(bytes, 32, mcfix::input_tick_pattern);
    insert_pattern(bytes, 192, mcfix::turn_delta_pattern);
    insert_pattern(bytes, 512, mcfix::camera_update_pattern);
    return bytes;
}

void test_hook_signatures_are_pinned_to_observed_1_26_42_cache() {
    expect(mcfix::input_tick_pattern ==
               "41 57 41 56 56 57 53 48 83 EC ? 0F 29 74 24 ? 66 0F 28 F1 48 89 CE "
               "F2 0F 11 89 ? ? ? ? 0F B6 81 ? ? ? ? 3C 03",
           "InputHandler::tick must use the exact locally observed 1.26.42 pattern");
    expect(mcfix::turn_delta_pattern ==
               "55 41 56 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 44 0F 29 5D ? "
               "44 0F 29 55 ? 44 0F 29 4D ? 44 0F 29 45 ? 0F 29 7D ? 0F 29 75 ? "
               "48 C7 45 ? ? ? ? ? 48 89 D7 48 89 CE 48 8B 89 ? ? ? ?",
           "LocalPlayer::applyTurnDelta must use the exact locally observed 1.26.42 pattern");
    expect(mcfix::camera_update_pattern ==
               "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC ? ? ? ? 44 0F 29 94 24 ? ? ? ? "
               "44 0F 29 8C 24 ? ? ? ? 44 0F 29 84 24 ? ? ? ? 0F 29 BC 24 ? ? ? ? "
               "0F 29 B4 24 ? ? ? ? 4C 89 C6",
           "MinecraftCamera::updateCamera must use the exact locally observed 1.26.42 pattern");
}

void test_hook_target_scan_accepts_only_three_unique_current_build_matches() {
    const auto bytes = valid_text_fixture();
    const auto result = mcfix::scan_hook_targets(bytes);

    expect(result.reason == mcfix::HookScanReason::none, "three unique valid targets must scan cleanly");
    expect(result.targets.input_tick == 32, "the current direct InputHandler::tick offset must be exact");
    expect(result.targets.turn_delta == 192, "the turn-delta function offset must be exact");
    expect(result.targets.camera_update == 512, "the camera-update function offset must be exact");
}

void test_hook_target_scan_refuses_required_byte_mutation_and_duplicates() {
    auto mutated = valid_text_fixture();
    mutated[192] = std::byte{0x49};
    expect(mcfix::scan_hook_targets(mutated).reason == mcfix::HookScanReason::turn_delta_match_count,
           "a required turn-delta prologue mutation must refuse the patch");

    auto duplicated = valid_text_fixture();
    insert_pattern(duplicated, 700, mcfix::camera_update_pattern);
    expect(mcfix::scan_hook_targets(duplicated).reason == mcfix::HookScanReason::camera_update_match_count,
           "duplicate camera signatures must refuse the patch");
}

mcfix::TelemetryEvent captured_event{};
bool sink_called = false;
void* captured_object = nullptr;
mcfix::Vec2* captured_delta_address = nullptr;

void capture_event(const mcfix::TelemetryEvent& event) {
    captured_event = event;
    sink_called = true;
}

std::uint64_t fixed_qpc() {
    return 123456;
}

std::uint32_t fixed_thread_id() {
    return 77;
}

void capture_original(void* object, mcfix::Vec2& delta) {
    captured_object = object;
    captured_delta_address = &delta;
}

void test_turn_delta_forwarder_records_and_forwards_the_original_reference_unchanged() {
    sink_called = false;
    captured_object = nullptr;
    captured_delta_address = nullptr;
    mcfix::Vec2 delta{1.25F, -2.5F};
    auto* expected_address = &delta;
    auto* object = reinterpret_cast<void*>(0x1234);

    mcfix::forward_turn_delta(capture_original, capture_event, fixed_qpc, fixed_thread_id, object, delta);

    expect(sink_called, "turn-delta forwarding must emit telemetry");
    expect(captured_event.qpc == 123456 && captured_event.thread_id == 77,
           "turn-delta telemetry must use the supplied clock and thread identity");
    expect(captured_event.kind == mcfix::EventKind::turn_delta &&
               captured_event.x == 1.25F && captured_event.y == -2.5F,
           "turn-delta telemetry must preserve both input values exactly");
    expect(captured_object == object && captured_delta_address == expected_address,
           "the original function must receive the exact object and delta reference");
    expect(delta.x == 1.25F && delta.y == -2.5F,
           "the forwarding layer must not modify camera delta values");
}

std::array<void*, 4> captured_marker_args{};

void* capture_marker_original(void* a1, void* a2, void* a3, void* a4) {
    captured_marker_args = {a1, a2, a3, a4};
    return reinterpret_cast<void*>(0xBEEF);
}

void test_input_tick_forwarder_preserves_arguments_and_return_value() {
    const std::array<void*, 4> args{
        reinterpret_cast<void*>(1), reinterpret_cast<void*>(2),
        reinterpret_cast<void*>(3), reinterpret_cast<void*>(4),
    };

    sink_called = false;
    captured_marker_args = {};
    const auto result = mcfix::forward_input_tick(
        capture_marker_original, capture_event, fixed_qpc, fixed_thread_id,
        args[0], args[1], args[2], args[3]);

    expect(result == reinterpret_cast<void*>(0xBEEF),
           "input-tick forwarding must preserve the original return value");
    expect(captured_marker_args == args, "input-tick forwarding must preserve all four arguments");
    expect(sink_called && captured_event.kind == mcfix::EventKind::input_tick,
           "input-tick forwarding must emit an input_tick event");
    expect(captured_event.x == 0.0F && captured_event.y == 0.0F,
           "input-tick events must not invent camera delta values");
}

std::array<void*, 3> captured_camera_args{};

void* capture_camera_original(void* a1, void* a2, void* a3) {
    captured_camera_args = {a1, a2, a3};
    return reinterpret_cast<void*>(0xCAFE);
}

void test_camera_forwarder_uses_the_three_argument_camera_abi() {
    const std::array<void*, 3> args{
        reinterpret_cast<void*>(5), reinterpret_cast<void*>(6), reinterpret_cast<void*>(7),
    };
    sink_called = false;
    captured_camera_args = {};

    const auto result = mcfix::forward_camera_update(
        capture_camera_original, capture_event, fixed_qpc, fixed_thread_id,
        args[0], args[1], args[2]);

    expect(result == reinterpret_cast<void*>(0xCAFE),
           "camera forwarding must preserve the original return value");
    expect(captured_camera_args == args, "camera forwarding must preserve exactly three arguments");
    expect(sink_called && captured_event.kind == mcfix::EventKind::camera_update,
           "camera forwarding must emit a camera_update event");
}

void test_loader_mode_requires_one_explicit_safe_operation() {
    expect(mcfix::parse_loader_mode({}).mode == mcfix::LoaderMode::invalid,
           "the loader must not choose a mutating default mode");
    expect(mcfix::parse_loader_mode({"--inspect"}).mode == mcfix::LoaderMode::inspect,
           "--inspect must select read-only inspection");
    expect(mcfix::parse_loader_mode({"--launch"}).mode == mcfix::LoaderMode::launch,
           "--launch must select the one-shot launch-and-load operation");
    expect(mcfix::parse_loader_mode({"--launch", "extra"}).mode == mcfix::LoaderMode::invalid,
           "extra loader arguments must be rejected");
}

void test_remote_path_is_freed_only_after_the_load_thread_finishes() {
    const auto success = mcfix::remote_cleanup(mcfix::RemoteLoadState::completed_nonzero);
    expect(success.free_remote_path && success.load_succeeded,
           "a completed nonzero LoadLibrary thread may free its path and report success");

    const auto failed = mcfix::remote_cleanup(mcfix::RemoteLoadState::completed_zero);
    expect(failed.free_remote_path && !failed.load_succeeded,
           "a completed failed LoadLibrary thread may free its path but must report failure");

    const auto timed_out = mcfix::remote_cleanup(mcfix::RemoteLoadState::still_running);
    expect(!timed_out.free_remote_path && !timed_out.load_succeeded,
           "a possibly running remote thread must keep its path allocation alive");
}

void test_target_pid_must_remain_unique_and_unchanged_before_injection() {
    const std::array<std::uint32_t, 1> same{123};
    const std::array<std::uint32_t, 1> changed{456};
    const std::array<std::uint32_t, 2> duplicate{123, 456};
    expect(mcfix::stable_target_pid(123, same), "one unchanged PID must remain eligible");
    expect(!mcfix::stable_target_pid(123, changed), "a replaced PID must be refused");
    expect(!mcfix::stable_target_pid(123, duplicate), "multiple PIDs must be refused");
    expect(!mcfix::stable_target_pid(123, {}), "a closed target must be refused");
}

void test_capture_limit_starts_on_camera_motion_and_stops_at_fixed_boundaries() {
    mcfix::CaptureLimiter limiter(100, 3);
    expect(limiter.observe(mcfix::TelemetryEvent{
               .qpc = 10, .kind = mcfix::EventKind::camera_update}) == mcfix::CaptureDecision::wait,
           "camera updates before motion must be discarded without consuming the capture window");
    expect(limiter.observe(mcfix::TelemetryEvent{
               .qpc = 20, .kind = mcfix::EventKind::turn_delta, .x = 0.0F, .y = 0.0F}) ==
               mcfix::CaptureDecision::wait,
           "zero turn deltas must be discarded without starting the capture window");
    expect(limiter.observe(mcfix::TelemetryEvent{
               .qpc = 30, .kind = mcfix::EventKind::turn_delta, .x = 1.0F, .y = 0.0F}) ==
               mcfix::CaptureDecision::record,
           "the first nonzero turn delta must start capture");
    expect(limiter.observe(mcfix::TelemetryEvent{
               .qpc = 40, .kind = mcfix::EventKind::camera_update}) == mcfix::CaptureDecision::record,
           "capture must continue below its event limit");
    expect(limiter.observe(mcfix::TelemetryEvent{
               .qpc = 50, .kind = mcfix::EventKind::input_tick}) == mcfix::CaptureDecision::stop,
           "capture must stop exactly at its event limit");

    mcfix::CaptureLimiter timed(10, 100);
    expect(timed.observe(mcfix::TelemetryEvent{
               .qpc = 100, .kind = mcfix::EventKind::turn_delta, .x = 1.0F}) ==
               mcfix::CaptureDecision::record,
           "timed capture must start on motion");
    expect(timed.observe(mcfix::TelemetryEvent{
               .qpc = 110, .kind = mcfix::EventKind::camera_update}) == mcfix::CaptureDecision::stop,
           "capture must stop exactly at its QPC duration");
}

void test_turn_distributor_splits_one_20ms_chunk_and_preserves_the_exact_total() {
    mcfix::TurnDistributor distributor(1000);
    auto* object = reinterpret_cast<void*>(0x1234);
    static_cast<void>(distributor.distribute(object, 100, {}));
    static_cast<void>(distributor.distribute(object, 104, {}));

    mcfix::Vec2 total{};
    const std::array<std::uint64_t, 5> calls{108, 112, 116, 120, 124};
    for (std::size_t index = 0; index < calls.size(); ++index) {
        const auto input = index == 0 ? mcfix::Vec2{10.0F, -5.0F} : mcfix::Vec2{};
        const auto output = distributor.distribute(object, calls[index], input);
        expect_near(output.x, 2.0F, "a 20 ms X chunk at 4 ms cadence must be split into five equal calls");
        expect_near(output.y, -1.0F, "a 20 ms Y chunk at 4 ms cadence must be split into five equal calls");
        total.x += output.x;
        total.y += output.y;
    }

    expect_near(total.x, 10.0F, "distributed X output must preserve the exact input total");
    expect_near(total.y, -5.0F, "distributed Y output must preserve the exact input total");
    const auto drained = distributor.distribute(object, 128, {});
    expect_near(drained.x, 0.0F, "the fixed queue must be empty after its bounded drain");
    expect_near(drained.y, 0.0F, "the fixed queue must not retain a Y tail");
}

void test_turn_distributor_passthrough_and_stale_gap_safety() {
    auto* object = reinterpret_cast<void*>(0x5678);
    mcfix::TurnDistributor low_rate(1000);
    static_cast<void>(low_rate.distribute(object, 100, {}));
    const auto direct = low_rate.distribute(object, 125, {7.0F, -3.0F});
    expect_near(direct.x, 7.0F, "a frame interval longer than the horizon must pass X through");
    expect_near(direct.y, -3.0F, "a frame interval longer than the horizon must pass Y through");

    mcfix::TurnDistributor stale(1000);
    static_cast<void>(stale.distribute(object, 100, {}));
    static_cast<void>(stale.distribute(object, 104, {}));
    const auto first = stale.distribute(object, 108, {10.0F, 0.0F});
    expect_near(first.x, 2.0F, "the first scheduled portion must be emitted immediately");
    const auto flushed = stale.distribute(object, 500, {1.0F, 0.0F});
    expect_near(flushed.x, 9.0F, "a stale timing gap must flush the pending total plus new input");
}

void test_cadence_classifier_arms_only_for_repeated_coarse_turn_batches() {
    mcfix::CadenceClassifier affected(1000);
    std::uint64_t qpc = 100;
    for (int frame = 0; frame < 36; ++frame) {
        const bool nonzero = frame % 5 == 0;
        static_cast<void>(affected.observe(
            qpc, nonzero ? mcfix::Vec2{0.129925653F, 0.0F} : mcfix::Vec2{}));
        qpc += 4;
    }
    expect(affected.affected(),
           "five repeated 20 ms turn batches on a 4 ms camera cadence must arm correction");

    mcfix::CadenceClassifier already_smooth(1000);
    qpc = 100;
    for (int frame = 0; frame < 36; ++frame) {
        static_cast<void>(already_smooth.observe(qpc, {0.03F, 0.0F}));
        qpc += 4;
    }
    expect(!already_smooth.affected(),
           "non-zero input arriving every camera call must remain untouched");

    mcfix::CadenceClassifier low_frame_rate(1000);
    qpc = 100;
    for (int frame = 0; frame < 12; ++frame) {
        static_cast<void>(low_frame_rate.observe(qpc, {0.13F, 0.0F}));
        qpc += 20;
    }
    expect(!low_frame_rate.affected(),
           "a low camera/render cadence must not be mistaken for the GDK batching bug");
}

std::vector<std::byte> synthetic_pe(std::uint16_t machine, std::uint16_t optional_magic) {
    std::vector<std::byte> bytes(512, std::byte{0});
    bytes[0] = std::byte{'M'};
    bytes[1] = std::byte{'Z'};
    const std::uint32_t pe_offset = 0x80;
    std::memcpy(bytes.data() + 0x3C, &pe_offset, sizeof(pe_offset));
    bytes[0x80] = std::byte{'P'};
    bytes[0x81] = std::byte{'E'};
    std::memcpy(bytes.data() + 0x84, &machine, sizeof(machine));
    std::memcpy(bytes.data() + 0x98, &optional_magic, sizeof(optional_magic));
    return bytes;
}

void test_pe_gate_accepts_only_a_complete_x64_image_header() {
    expect(mcfix::is_pe64_image(synthetic_pe(0x8664, 0x020B)),
           "AMD64 PE32+ must pass the local DLL architecture gate");
    expect(!mcfix::is_pe64_image(synthetic_pe(0x014C, 0x010B)),
           "x86 PE32 must fail the local DLL architecture gate");
    expect(!mcfix::is_pe64_image(std::array<std::byte, 8>{}),
           "truncated images must fail without an out-of-bounds read");
}

}  // namespace

int main() {
    test_signature_scanner_reports_every_match();
    test_signature_parser_rejects_malformed_tokens();
    test_target_policy_accepts_matching_minecraft_x64_updates_but_not_other_packages();
    test_target_policy_rejects_conflicting_hooks();
    test_telemetry_ring_drains_committed_events_in_order();
    test_telemetry_ring_reports_overwritten_events();
    test_hook_signatures_are_pinned_to_observed_1_26_42_cache();
    test_hook_target_scan_accepts_only_three_unique_current_build_matches();
    test_hook_target_scan_refuses_required_byte_mutation_and_duplicates();
    test_turn_delta_forwarder_records_and_forwards_the_original_reference_unchanged();
    test_input_tick_forwarder_preserves_arguments_and_return_value();
    test_camera_forwarder_uses_the_three_argument_camera_abi();
    test_loader_mode_requires_one_explicit_safe_operation();
    test_remote_path_is_freed_only_after_the_load_thread_finishes();
    test_target_pid_must_remain_unique_and_unchanged_before_injection();
    test_capture_limit_starts_on_camera_motion_and_stops_at_fixed_boundaries();
    test_turn_distributor_splits_one_20ms_chunk_and_preserves_the_exact_total();
    test_turn_distributor_passthrough_and_stale_gap_safety();
    test_cadence_classifier_arms_only_for_repeated_coarse_turn_batches();
    test_pe_gate_accepts_only_a_complete_x64_image_header();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "all core tests passed\n";
    return 0;
}
