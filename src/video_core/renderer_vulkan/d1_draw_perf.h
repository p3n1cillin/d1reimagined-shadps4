// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// D1: the draw path's CPU cost. Destiny records many small command buffers per view and render
// stage, with thousands of shader techniques a frame and each gear piece drawn about five times,
// and re-binds the same state at the start of each command buffer (Tatarchuk, GDC 2015). The GPU
// command processor turns every one of those draws into Vulkan commands on one thread, so a
// command that only repeats the current state is pure cost there.
//
// - D1_DRAW_FAST (default on, =0 restores upstream's path): a pipeline, vertex input, vertex
//   buffer or index buffer bind that repeats what the current command buffer already has is not
//   recorded again; the graphics pipeline lookup remembers the last key; the scheduler doesn't
//   query the GPU's timeline when nothing is waiting on it.
// - D1_DRAW_STATS=1: once a second, logs draws, dispatches, submits, guest command buffers, the
//   CPU time per draw in each phase, the share of the GPU thread spent in draws and how many binds
//   the filters skipped.
// - D1_GPU_WAIT_YIELD=1 (default off): a guest wait that has spun 256 times without being
//   satisfied gives the rest of its time slice to other threads on each further spin.
//
// This header has no emulator dependencies, so tests/test_d1_draw_perf.cpp builds it alone.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace Vulkan::D1DrawPerf {

namespace Detail {
inline bool EnvIs(const char* name, char value, bool fallback) {
    const char* v = std::getenv(name);
    return (v && v[0] != '\0') ? v[0] == value : fallback;
}
} // namespace Detail

/// D1_DRAW_FAST=0 turns the redundant-bind filters and the pipeline memo off.
inline bool FastEnabled() {
    static const bool on = !Detail::EnvIs("D1_DRAW_FAST", '0', false);
    return on;
}

/// D1_DRAW_STATS=1 logs the draw path's cost once a second.
inline bool StatsEnabled() {
    static const bool on = Detail::EnvIs("D1_DRAW_STATS", '1', false);
    return on;
}

/// D1_GPU_WAIT_YIELD=1 yields the GPU thread's time slice in long guest waits.
inline bool WaitYieldEnabled() {
    static const bool on = Detail::EnvIs("D1_GPU_WAIT_YIELD", '1', false);
    return on;
}

/// The last value a command set on the current command buffer. Changed() says whether the command
/// has to be recorded, and remembers the new value when it does.
template <typename T>
class LastValue {
public:
    bool Changed(const T& value) {
        if (valid && last == value) {
            return false;
        }
        last = value;
        valid = true;
        return true;
    }

    void Invalidate() {
        valid = false;
    }

private:
    T last{};
    bool valid = false;
};

/// The same for a list of up to N elements (vertex bindings, attributes, buffers). A longer list is
/// always recorded and forgets the previous one.
template <typename T, std::size_t N>
class LastList {
public:
    bool Changed(std::span<const T> values) {
        if (valid && values.size() == count &&
            std::equal(values.begin(), values.end(), items.begin())) {
            return false;
        }
        if (values.size() > N) {
            valid = false;
            return true;
        }
        std::copy(values.begin(), values.end(), items.begin());
        count = values.size();
        valid = true;
        return true;
    }

    void Invalidate() {
        valid = false;
    }

private:
    std::array<T, N> items{};
    std::size_t count = 0;
    bool valid = false;
};

/// One vertex buffer binding as the rasterizer records it.
struct VertexBufferBind {
    std::uint64_t buffer;
    std::uint64_t offset;
    std::uint64_t size;
    std::uint64_t stride;
    bool operator==(const VertexBufferBind&) const = default;
};

/// The index buffer binding.
struct IndexBufferBind {
    std::uint64_t buffer;
    std::uint64_t offset;
    std::uint32_t type;
    bool operator==(const IndexBufferBind&) const = default;
};

/// Counters of one stats window. Written by the GPU thread (and submits from other threads),
/// read when the window is logged; relaxed atomics are enough for a log.
enum class Phase : std::uint32_t { Pipeline, Resources, Record, Count };
enum class Kind : std::uint32_t { Draw, DrawIndirect, Dispatch, Count };
enum class Counter : std::uint32_t {
    PipelineBinds,
    PipelineBindsSkipped,
    VertexInputs,
    VertexInputsSkipped,
    VertexBuffers,
    VertexBuffersSkipped,
    IndexBuffers,
    IndexBuffersSkipped,
    PipelineMemoHits,
    PipelineLookups,
    PendingRefreshSkipped,
    Submits,
    GuestSubmits,
    GuestCommandBuffers,
    WaitSpins,
    WaitYields,
    Count,
};

struct Window {
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Kind::Count)> calls{};
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Phase::Count)> phase_ns{};
    std::atomic<std::uint64_t> total_ns{};
    std::atomic<std::uint64_t> max_ns{};
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Counter::Count)> counters{};

    void Add(Counter c, std::uint64_t n = 1) {
        counters[static_cast<std::size_t>(c)].fetch_add(n, std::memory_order_relaxed);
    }

    void AddCall(Kind kind, const std::array<std::uint64_t, 3>& phases, std::uint64_t total) {
        calls[static_cast<std::size_t>(kind)].fetch_add(1, std::memory_order_relaxed);
        for (std::size_t i = 0; i < phases.size(); ++i) {
            phase_ns[i].fetch_add(phases[i], std::memory_order_relaxed);
        }
        total_ns.fetch_add(total, std::memory_order_relaxed);
        std::uint64_t prev = max_ns.load(std::memory_order_relaxed);
        while (total > prev && !max_ns.compare_exchange_weak(prev, total)) {
        }
    }

    std::uint64_t Get(Counter c) const {
        return counters[static_cast<std::size_t>(c)].load(std::memory_order_relaxed);
    }
};

/// What a window says, per second and per draw.
struct Summary {
    double seconds = 0;
    std::uint64_t draws = 0;    ///< direct and indirect draws
    std::uint64_t indirect = 0; ///< of which indirect
    std::uint64_t dispatches = 0;
    double us_per_call = 0;           ///< CPU time of a draw or dispatch, all phases
    std::array<double, 3> us_phase{}; ///< per call, in pipeline lookup, resources, recording
    double us_max = 0;                ///< the slowest call
    double busy = 0;                  ///< share of the window's wall time spent in calls
    double skipped_pipeline = 0;      ///< share of pipeline binds the filter skipped
    double skipped_vertex_input = 0;
    double skipped_vertex_buffers = 0;
    double skipped_index_buffer = 0;
    double memo_hits = 0; ///< share of pipeline lookups the one-entry memo answered
};

inline double Share(std::uint64_t part, std::uint64_t whole) {
    return whole ? static_cast<double>(part) / static_cast<double>(whole) : 0.0;
}

inline Summary Summarize(const Window& w, double seconds) {
    Summary s{};
    s.seconds = seconds;
    s.draws = w.calls[0].load() + w.calls[1].load();
    s.indirect = w.calls[1].load();
    s.dispatches = w.calls[2].load();
    const std::uint64_t calls = s.draws + s.dispatches;
    const double total = static_cast<double>(w.total_ns.load());
    s.us_per_call = calls ? total / 1000.0 / static_cast<double>(calls) : 0.0;
    for (std::size_t i = 0; i < s.us_phase.size(); ++i) {
        s.us_phase[i] =
            calls ? static_cast<double>(w.phase_ns[i].load()) / 1000.0 / static_cast<double>(calls)
                  : 0.0;
    }
    s.us_max = static_cast<double>(w.max_ns.load()) / 1000.0;
    s.busy = seconds > 0 ? total / (seconds * 1e9) : 0.0;
    s.skipped_pipeline =
        Share(w.Get(Counter::PipelineBindsSkipped),
              w.Get(Counter::PipelineBinds) + w.Get(Counter::PipelineBindsSkipped));
    s.skipped_vertex_input =
        Share(w.Get(Counter::VertexInputsSkipped),
              w.Get(Counter::VertexInputs) + w.Get(Counter::VertexInputsSkipped));
    s.skipped_vertex_buffers =
        Share(w.Get(Counter::VertexBuffersSkipped),
              w.Get(Counter::VertexBuffers) + w.Get(Counter::VertexBuffersSkipped));
    s.skipped_index_buffer =
        Share(w.Get(Counter::IndexBuffersSkipped),
              w.Get(Counter::IndexBuffers) + w.Get(Counter::IndexBuffersSkipped));
    s.memo_hits = Share(w.Get(Counter::PipelineMemoHits), w.Get(Counter::PipelineLookups));
    return s;
}

/// Spins of one guest wait (WAIT_REG_MEM, a memory semaphore, a rewind, the CE/DE counters).
/// Spin() is called each time the wait is found unsatisfied, before the task yields.
class WaitSpin {
public:
    static constexpr std::uint32_t YieldAfter = 256;

    /// Returns true when the caller should give up its time slice now.
    bool Spin(bool yield_enabled) {
        ++spins;
        return yield_enabled && spins > YieldAfter;
    }

    std::uint32_t Spins() const {
        return spins;
    }

private:
    std::uint32_t spins = 0;
};

/// The process-wide window (d1_draw_perf.cpp).
Window& Stats();

/// Logs and restarts the window once a second has passed. Cheap to call after every draw.
void MaybeLog();

inline void Count(Counter c, std::uint64_t n = 1) {
    if (StatsEnabled()) {
        Stats().Add(c, n);
    }
}

/// Times one draw or dispatch in phases, when D1_DRAW_STATS is on; otherwise does nothing.
class CallTimer {
public:
    explicit CallTimer(Kind kind_) : kind{kind_}, on{StatsEnabled()} {
        if (on) {
            start = last = Clock::now();
        }
    }

    /// Ends the given phase here; the next one starts.
    void Mark(Phase phase) {
        if (on) {
            const auto now = Clock::now();
            phases[static_cast<std::size_t>(phase)] += Nanoseconds(now - last);
            last = now;
        }
    }

    ~CallTimer() {
        if (on) {
            const auto now = Clock::now();
            phases[static_cast<std::size_t>(Phase::Record)] += Nanoseconds(now - last);
            Stats().AddCall(kind, phases, Nanoseconds(now - start));
            MaybeLog();
        }
    }

    CallTimer(const CallTimer&) = delete;
    CallTimer& operator=(const CallTimer&) = delete;

private:
    using Clock = std::chrono::steady_clock;
    static std::uint64_t Nanoseconds(Clock::duration d) {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
    }

    Kind kind;
    bool on;
    Clock::time_point start{}, last{};
    std::array<std::uint64_t, 3> phases{};
};

} // namespace Vulkan::D1DrawPerf
