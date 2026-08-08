#include "hook_callbacks.hpp"

namespace mcfix {

void forward_turn_delta(
    TurnDeltaOriginal original,
    EventSink sink,
    QpcProvider qpc,
    ThreadIdProvider thread_id,
    void* object,
    Vec2& delta) {
    sink(TelemetryEvent{
        .qpc = qpc(),
        .thread_id = thread_id(),
        .kind = EventKind::turn_delta,
        .x = delta.x,
        .y = delta.y,
    });
    original(object, delta);
}

void* forward_input_tick(
    InputTickOriginal original,
    EventSink sink,
    QpcProvider qpc,
    ThreadIdProvider thread_id,
    void* a1,
    void* a2,
    void* a3,
    void* a4) {
    sink(TelemetryEvent{
        .qpc = qpc(),
        .thread_id = thread_id(),
        .kind = EventKind::input_tick,
    });
    return original(a1, a2, a3, a4);
}

void* forward_camera_update(
    CameraUpdateOriginal original,
    EventSink sink,
    QpcProvider qpc,
    ThreadIdProvider thread_id,
    void* camera,
    void* a2,
    void* a3) {
    sink(TelemetryEvent{
        .qpc = qpc(),
        .thread_id = thread_id(),
        .kind = EventKind::camera_update,
    });
    return original(camera, a2, a3);
}

}  // namespace mcfix
