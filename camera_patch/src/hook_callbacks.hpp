#pragma once

#include "mcfix/telemetry.hpp"

#include <cstdint>

namespace mcfix {

struct Vec2 {
    float x{};
    float y{};
};

using EventSink = void (*)(const TelemetryEvent&);
using QpcProvider = std::uint64_t (*)();
using ThreadIdProvider = std::uint32_t (*)();
using TurnDeltaOriginal = void (*)(void*, Vec2&);
using InputTickOriginal = void* (*)(void*, void*, void*, void*);
using CameraUpdateOriginal = void* (*)(void*, void*, void*);

void forward_turn_delta(
    TurnDeltaOriginal original,
    EventSink sink,
    QpcProvider qpc,
    ThreadIdProvider thread_id,
    void* object,
    Vec2& delta);

void* forward_input_tick(
    InputTickOriginal original,
    EventSink sink,
    QpcProvider qpc,
    ThreadIdProvider thread_id,
    void* a1,
    void* a2,
    void* a3,
    void* a4);

void* forward_camera_update(
    CameraUpdateOriginal original,
    EventSink sink,
    QpcProvider qpc,
    ThreadIdProvider thread_id,
    void* camera,
    void* a2,
    void* a3);

}  // namespace mcfix
