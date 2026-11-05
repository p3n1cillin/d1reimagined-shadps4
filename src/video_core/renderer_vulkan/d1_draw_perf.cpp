// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "video_core/renderer_vulkan/d1_draw_perf.h"

namespace Vulkan::D1DrawPerf {

Window& Stats() {
    static Window window;
    return window;
}

static void Reset(Window& w) {
    for (auto& c : w.calls) {
        c.store(0, std::memory_order_relaxed);
    }
    for (auto& p : w.phase_ns) {
        p.store(0, std::memory_order_relaxed);
    }
    w.total_ns.store(0, std::memory_order_relaxed);
    w.max_ns.store(0, std::memory_order_relaxed);
    for (auto& c : w.counters) {
        c.store(0, std::memory_order_relaxed);
    }
}

void MaybeLog() {
    static std::atomic<std::int64_t> window_start{0};
    const std::int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
    std::int64_t start = window_start.load(std::memory_order_relaxed);
    if (start == 0) {
        window_start.compare_exchange_strong(start, now);
        return;
    }
    if (now - start < 1'000'000'000 || !window_start.compare_exchange_strong(start, now)) {
        return;
    }
    auto& w = Stats();
    const Summary s = Summarize(w, static_cast<double>(now - start) / 1e9);
    const double per_s = 1.0 / s.seconds;
    LOG_INFO(Render_Vulkan,
             "D1 draw stats: {:.0f} draws/s ({:.0f} indirect), {:.0f} dispatches/s, {:.0f} "
             "submits/s, {:.0f} guest submits/s with {:.0f} command buffers/s; {:.2f} us a call "
             "(pipeline {:.2f}, resources {:.2f}, record {:.2f}), slowest {:.0f} us; GPU thread "
             "{:.0f}% in calls; skipped binds: pipeline {:.0f}%, vertex input {:.0f}%, vertex "
             "buffers {:.0f}%, index buffer {:.0f}%; pipeline memo {:.0f}%; guest wait spins "
             "{:.0f}/s ({:.0f} yields)",
             s.draws * per_s, s.indirect * per_s, s.dispatches * per_s,
             w.Get(Counter::Submits) * per_s, w.Get(Counter::GuestSubmits) * per_s,
             w.Get(Counter::GuestCommandBuffers) * per_s, s.us_per_call, s.us_phase[0],
             s.us_phase[1], s.us_phase[2], s.us_max, s.busy * 100, s.skipped_pipeline * 100,
             s.skipped_vertex_input * 100, s.skipped_vertex_buffers * 100,
             s.skipped_index_buffer * 100, s.memo_hits * 100, w.Get(Counter::WaitSpins) * per_s,
             w.Get(Counter::WaitYields) * per_s);
    Reset(w);
}

} // namespace Vulkan::D1DrawPerf
