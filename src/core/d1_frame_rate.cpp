// SPDX-FileCopyrightText: Copyright 2026 d1reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// D1 (CUSA00219, executable 56578): the game's frame rate, held at a setting while it runs.
//   D1_FPS=N   hold the rate at N (10..1000); D1_FPS=0 is uncapped (the guard finds the level).
// The rate lives in three values the game sets up once (d1reimagined-source notes/ps4-56578-frame-
// rate-2026-11-02.md):
//   the cadence table [0x26BCD08]: +0x08 the present period in us (1001000/N), +0x38 the rate N,
//                                  +0x40 the tick step in us (1000000/N), which the scheduler ticks on;
//   the engine clock  [0x26BC880]: +0x04 the ticks a second N (an integer: the game's helpers at
//                                  0x591BF0-0x591C80 turn seconds into ticks with it, such as a
//                                  weapon's shot interval), +0x08 the tick period in s (1/N),
//                                  +0x0C the tick count.
// The time scale [0x26BC888] +0x04 (1.0) stays as it is: it paces the engine (0.5 halved the ticks).
// Without +0x04 a weapon kept the start-up rate's ticks: booted at 120, a rifle fired every 16 ticks
// at any rate, 112 rounds a minute at 30 and 900 at 240.
// Written live they switch the rate with no restart. Each tick costs CPU, and the engine simulates
// only so many a second (in the mission on the lab PC about 70; in orbit nearly 1000): past that the
// game slows down. The guard lowers the rate to what the engine achieves (never below 60) and
// raises it back toward the setting after a stretch of real time. The presenter's vblank runs at the
// target rate while D1_FPS is set (500 Hz uncapped), so the vblank never holds the rate back.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "core/d1_frame_rate.h"

namespace Core::D1FrameRate {

namespace {

constexpr u64 CadencePtr = 0x26BCD08;
constexpr u64 ClockPtr = 0x26BC880;
constexpr u32 Uncapped = 1000;
constexpr u32 Floor = 60;

int g_setting = -1; // -1: off
std::atomic<u64> g_flips{0};

template <typename T>
T Read(uintptr_t addr) {
    T v;
    std::memcpy(&v, reinterpret_cast<const void*>(addr), sizeof(T));
    return v;
}

template <typename T>
void Write(uintptr_t addr, T v) {
    std::memcpy(reinterpret_cast<void*>(addr), &v, sizeof(T));
}

uintptr_t Pointer(u64 vaddr) {
    return static_cast<uintptr_t>(Read<u64>(MemoryPatcher::g_eboot_address + vaddr));
}

void Apply(uintptr_t table, uintptr_t clock, u32 rate) {
    Write<u32>(clock + 0x04, rate);
    Write<u64>(table + 0x08, static_cast<u64>(1001000.0 / rate + 0.5));
    Write<u64>(table + 0x38, rate);
    Write<u64>(table + 0x40, static_cast<u64>(1000000.0 / rate + 0.5));
    Write<float>(clock + 0x08, 1.0f / static_cast<float>(rate));
}

void Hold() {
    const u32 target = g_setting == 0 ? Uncapped : static_cast<u32>(g_setting);
    uintptr_t table = 0, clock = 0;
    while (table == 0 || clock == 0 || Read<u64>(table + 0x38) == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        table = Pointer(CadencePtr);
        clock = Pointer(ClockPtr);
    }
    u32 rate = g_setting == 0 ? Floor : target; // uncapped climbs from 60
    Apply(table, clock, rate);
    LOG_INFO(Lib_VideoOut, "D1_FPS: holding {} (setting {})", rate, g_setting);
    u32 last_ticks = Read<u32>(clock + 0x0C);
    u64 last_flips = g_flips.load();
    int follow = 0;
    // Uncapped: a rate the follower had to come down from is not probed again for 2 min (it
    // flapped 190 -> 228 -> 190 every 10 s in orbit).
    u32 probe_limit = Uncapped + 1;
    auto probe_limit_set = std::chrono::steady_clock::now();
    auto last = std::chrono::steady_clock::now();
    int slow = 0, real = 0;
    // The lowest rate that ran slow lately: climbing stops 10 below it, until it lapses (5 min), as a
    // scene changes what the PC can hold. Without it the guard climbed past and fell back every 30 s.
    u32 ceiling = target + 1;
    auto ceiling_set = std::chrono::steady_clock::now();
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        table = Pointer(CadencePtr);
        clock = Pointer(ClockPtr);
        if (table == 0 || clock == 0) {
            continue;
        }
        const auto now = std::chrono::steady_clock::now();
        const u32 ticks = Read<u32>(clock + 0x0C);
        const double seconds = std::chrono::duration<double>(now - last).count();
        // Game time per second: the ticks taken times each tick's length. 1.0 is real time.
        const double achieved = (ticks - last_ticks) / seconds;
        const double game = achieved * Read<float>(clock + 0x08);
        const u64 flips = g_flips.load();
        const double fps = (flips - last_flips) / seconds;
        last_flips = flips;
        last_ticks = ticks;
        last = now;
        // A loading transition may set the clock up again: put the rate back.
        if (Read<u64>(table + 0x38) != rate || Read<u64>(table + 0x40) != static_cast<u64>(1000000.0 / rate + 0.5) ||
            Read<u32>(clock + 0x04) != rate || Read<float>(clock + 0x08) != 1.0f / static_cast<float>(rate)) {
            Apply(table, clock, rate);
            continue;
        }
        // The clock stands still in loading and menus: judge only while it runs.
        if (achieved < 1.0) {
            continue;
        }
        // 0.97: at 0.9 the guard let a rate of 90 run at 0.95x real time (fps-g-guard).
        // Uncapped: one tick per presented frame, as PC games mean it, not as many ticks as the CPU
        // can run (in orbit 990 ticks a second for 192 frames). Every 5 s: presentation at the
        // rate's cap probes 20% higher; a screen below 80% of the rate brings the rate down to it.
        if (g_setting == 0 && ++follow >= 5) {
            follow = 0;
            u32 next = rate;
            if (now - probe_limit_set > std::chrono::minutes(2)) {
                probe_limit = Uncapped + 1;
            }
            if (fps >= 0.95 * rate) {
                next = std::min<u32>({Uncapped, rate + std::max<u32>(10, rate / 5), probe_limit - 1});
                next = std::max(next, rate); // a probe never lowers
            } else if (fps < 0.80 * rate) {
                probe_limit = rate;
                probe_limit_set = now;
                next = std::max<u32>(Floor, static_cast<u32>(fps) / 10 * 10);
            }
            if (next != rate) {
                LOG_INFO(Lib_VideoOut, "D1_FPS uncapped: {:.0f} frames/s at {}; holding {}", fps, rate, next);
                rate = next;
                Apply(table, clock, rate);
                continue;
            }
        }
        if (game < 0.97) {
            real = 0;
            if (++slow >= 2 && rate > Floor) {
                const u32 lower = std::max<u32>(Floor, static_cast<u32>(achieved) / 10 * 10);
                LOG_INFO(Lib_VideoOut, "D1_FPS: {} ran at {:.0f} ticks/s; holding {}", rate, achieved,
                         lower);
                ceiling = std::min(ceiling, rate);
                ceiling_set = now;
                rate = lower;
                Apply(table, clock, rate);
                slow = 0;
            }
        } else {
            slow = 0;
            // 30 s of real time before trying higher again: rising sooner would only fall back.
            if (now - ceiling_set > std::chrono::minutes(5)) {
                ceiling = target + 1;
            }
            const u32 top = std::min(target, ceiling > Floor + 10 ? ceiling - 10 : Floor);
            if (g_setting != 0 && rate < top && ++real >= 30) {
                rate = std::min<u32>(top, rate + std::max<u32>(10, rate / 5));
                Apply(table, clock, rate);
                real = 0;
            }
        }
    }
}

} // namespace

void Install() {
    const char* s = std::getenv("D1_FPS");
    if (s == nullptr || *s == '\0') {
        return;
    }
    const int n = std::atoi(s);
    if (n != 0 && (n < 10 || n > 1000)) {
        LOG_ERROR(Lib_VideoOut, "D1_FPS={} is outside 10..1000 (0 is uncapped); ignored", s);
        return;
    }
    g_setting = n;
    std::thread(Hold).detach();
}

void NoteFlip() {
    g_flips.fetch_add(1, std::memory_order_relaxed);
}

u32 VblankOverride() {
    // The vblank at the target rate (500 uncapped): fast enough never to hold the rate back, without
    // running the presenter at 1000 Hz for nothing.
    const char* s = std::getenv("D1_FPS");
    if (s == nullptr || *s == '\0') {
        return 0;
    }
    const int n = std::atoi(s);
    return n == 0 ? 500 : static_cast<u32>(std::clamp(n, 60, 1000));
}

} // namespace Core::D1FrameRate
