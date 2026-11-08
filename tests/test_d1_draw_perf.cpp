// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Checks the D1 draw-path filters and stats (src/video_core/renderer_vulkan/d1_draw_perf.h), and
// with --bench times them against the work they replace. Builds alone, like the other test_d1_*:
//   clang-cl /std:c++latest /O2 /EHsc /I src /I externals/xxhash /I externals/robin-map/include
//            tests/test_d1_draw_perf.cpp && test_d1_draw_perf --bench
// The filters and stats summary are header-only; MaybeLog and Stats() live in the emulator.

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>
#include "video_core/renderer_vulkan/d1_draw_perf.h"

#define XXH_INLINE_ALL
#include <tsl/robin_map.h>
#include <xxhash.h>

using namespace Vulkan::D1DrawPerf;

static void TestLastValue() {
    LastValue<std::uint64_t> pipeline;
    assert(pipeline.Changed(7));  // first bind on a command buffer is recorded
    assert(!pipeline.Changed(7)); // the same pipeline again is not
    assert(pipeline.Changed(8));
    assert(pipeline.Changed(7));
    pipeline.Invalidate(); // a new command buffer, or a foreign pipeline bind
    assert(pipeline.Changed(7));
    assert(!pipeline.Changed(7));

    LastValue<IndexBufferBind> index;
    assert(index.Changed({1, 0, 0}));
    assert(!index.Changed({1, 0, 0}));
    assert(index.Changed({1, 64, 0}));      // offset
    assert(index.Changed({1, 64, 1}));      // type
    assert(index.Changed({2, 64, 1}));      // buffer
    assert(index.Changed({2, 64, 1, 128})); // maintenance5 range
    assert(!index.Changed({2, 64, 1, 128}));
}

static void TestLastList() {
    LastList<VertexBufferBind, 4> vb;
    const std::vector<VertexBufferBind> a = {{1, 0, 64, 16}, {2, 32, 64, 8}};
    const std::vector<VertexBufferBind> b = {{1, 0, 64, 16}, {2, 32, 64, 12}};
    const std::vector<VertexBufferBind> shorter = {{1, 0, 64, 16}};
    assert(vb.Changed(a));
    assert(!vb.Changed(a));
    assert(vb.Changed(b)); // stride of the second binding
    assert(vb.Changed(shorter));
    assert(!vb.Changed(shorter));
    assert(vb.Changed(std::span<const VertexBufferBind>{}));
    assert(!vb.Changed(std::span<const VertexBufferBind>{})); // no bindings twice
    vb.Invalidate();
    assert(vb.Changed(std::span<const VertexBufferBind>{}));

    // Longer than remembered: always recorded, and the next list is recorded too.
    const std::vector<VertexBufferBind> five(5, VertexBufferBind{3, 0, 4, 4});
    assert(vb.Changed(five));
    assert(vb.Changed(five));
    assert(vb.Changed(a));
    assert(!vb.Changed(a));
}

static void TestSummary() {
    Window w;
    w.AddCall(Kind::Draw, {1000, 2000, 1000}, 4000);
    w.AddCall(Kind::Draw, {1000, 2000, 1000}, 4000);
    w.AddCall(Kind::DrawIndirect, {0, 0, 0}, 1000);
    w.AddCall(Kind::Dispatch, {500, 500, 0}, 3000);
    w.Add(Counter::PipelineBinds, 1);
    w.Add(Counter::PipelineBindsSkipped, 3);
    w.Add(Counter::VertexInputs, 2);
    w.Add(Counter::VertexInputsSkipped, 2);
    w.Add(Counter::PipelineLookups, 4);
    w.Add(Counter::PipelineMemoHits, 1);
    const Summary s = Summarize(w, 0.5);
    assert(s.draws == 3 && s.indirect == 1 && s.dispatches == 1);
    assert(s.us_per_call == 12.0 / 4);
    assert(s.us_phase[0] == 2.5 / 4 && s.us_phase[1] == 4.5 / 4 && s.us_phase[2] == 2.0 / 4);
    assert(s.us_max == 4.0);
    assert(s.busy == 12000.0 / 0.5e9);
    assert(s.skipped_pipeline == 0.75);
    assert(s.skipped_vertex_input == 0.5);
    assert(s.skipped_vertex_buffers == 0.0); // nothing bound: no share, not a division by zero
    assert(s.memo_hits == 0.25);
    assert(Summarize(Window{}, 0.0).busy == 0.0);
}

static void TestWaitSpin() {
    WaitSpin spin;
    for (std::uint32_t i = 0; i < WaitSpin::YieldAfter; ++i) {
        assert(!spin.Spin(true)); // short waits keep spinning
    }
    assert(spin.Spin(true));   // then yield on each further spin
    assert(!spin.Spin(false)); // never with the knob off
    assert(spin.Spins() == WaitSpin::YieldAfter + 2);
}

// The draw path's own work, for scale: what a filter costs per draw against what it saves (a
// command the driver no longer records), and the one-entry pipeline memo against a 384-byte key
// hash. Numbers are this machine's; the native test measures the real thing (D1_DRAW_STATS).
template <typename F>
static double NsPerCall(F&& f, int n) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
        f(i);
    }
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / n;
}

static void Bench() {
    constexpr int N = 10'000'000;
    volatile std::uint64_t sink = 0;

    LastValue<std::uint64_t> pipeline;
    const double pipe_ns = NsPerCall(
        [&](int i) { sink = sink + pipeline.Changed(static_cast<std::uint64_t>(i >> 3)); }, N);

    LastList<VertexBufferBind, 32> vb;
    std::vector<VertexBufferBind> binds = {{1, 0, 4096, 16}, {2, 0, 4096, 8}, {3, 0, 4096, 4}};
    const double vb_ns = NsPerCall(
        [&](int i) {
            binds[0].offset = static_cast<std::uint64_t>(i >> 2);
            sink = sink + vb.Changed(binds);
        },
        N);

    // A graphics pipeline key's size. The upstream lookup hashes it whole with XXH3 and probes a
    // robin map; the memo compares it with the last key first. Draws come in runs of eight with
    // the same pipeline here; D1_DRAW_STATS reports the game's real hit rate.
    struct Key {
        unsigned char bytes[384];
        bool operator==(const Key& o) const {
            return std::memcmp(bytes, o.bytes, sizeof(bytes)) == 0;
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const {
            return XXH3_64bits(&k, sizeof(k));
        }
    };
    std::vector<Key> keys(64);
    tsl::robin_map<Key, int, KeyHash> map;
    for (std::size_t k = 0; k < keys.size(); ++k) {
        std::memset(keys[k].bytes, 0, sizeof(Key));
        std::memcpy(keys[k].bytes, &k, sizeof(k));
        map[keys[k]] = static_cast<int>(k);
    }
    const double map_ns =
        NsPerCall([&](int i) { sink = sink + map.find(keys[(i >> 3) & 63])->second; }, N);
    const Key* last = nullptr;
    int last_value = 0;
    const double memo_ns = NsPerCall(
        [&](int i) {
            const Key& key = keys[(i >> 3) & 63];
            if (!last || !(key == *last)) {
                last_value = map.find(key)->second;
                last = &key;
            }
            sink = sink + last_value;
        },
        N);

    Window w;
    const double count_ns = NsPerCall([&](int) { w.Add(Counter::PipelineBindsSkipped); }, N);
    std::printf("LastValue<pipeline>::Changed  %6.2f ns\n", pipe_ns);
    std::printf("LastList<3 vertex buffers>    %6.2f ns\n", vb_ns);
    std::printf("pipeline lookup, XXH3 + map   %6.2f ns\n", map_ns);
    std::printf("same, behind the memo         %6.2f ns\n", memo_ns);
    std::printf("stats counter                 %6.2f ns\n", count_ns);
}

int main(int argc, char** argv) {
    TestLastValue();
    TestLastList();
    TestSummary();
    TestWaitSpin();
    if (argc > 1 && std::string_view{argv[1]} == "--bench") {
        Bench();
    }
    std::puts("test_d1_draw_perf: ok");
    return 0;
}
