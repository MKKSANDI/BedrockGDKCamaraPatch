#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace mcfix {

enum class EventKind : std::uint32_t {
    input_tick = 1,
    turn_delta = 2,
    camera_update = 3,
    turn_distributed = 4,
};

struct TelemetryEvent {
    std::uint64_t sequence{};
    std::uint64_t qpc{};
    std::uint32_t thread_id{};
    EventKind kind{EventKind::input_tick};
    float x{};
    float y{};
};

std::string_view event_kind_name(EventKind kind);

template <std::size_t Capacity>
class TelemetryRing {
    static_assert(Capacity > 0, "telemetry ring capacity must be positive");

public:
    void push(const TelemetryEvent& event) noexcept {
        const auto sequence = next_sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (lock_.test_and_set(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        if (count_ == Capacity) {
            read_index_ = (read_index_ + 1) % Capacity;
            --count_;
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        auto stored = event;
        stored.sequence = sequence;
        slots_[write_index_] = stored;
        write_index_ = (write_index_ + 1) % Capacity;
        ++count_;
        lock_.clear(std::memory_order_release);
    }

    template <typename Consumer>
    std::size_t drain(Consumer&& consumer) {
        std::size_t drained = 0;
        for (;;) {
            if (lock_.test_and_set(std::memory_order_acquire)) {
                break;
            }
            if (count_ == 0) {
                lock_.clear(std::memory_order_release);
                break;
            }
            const auto event = slots_[read_index_];
            read_index_ = (read_index_ + 1) % Capacity;
            --count_;
            lock_.clear(std::memory_order_release);
            std::forward<Consumer>(consumer)(event);
            ++drained;
        }
        return drained;
    }

    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    std::array<TelemetryEvent, Capacity> slots_{};
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::size_t read_index_{0};
    std::size_t write_index_{0};
    std::size_t count_{0};
    std::atomic<std::uint64_t> next_sequence_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace mcfix
