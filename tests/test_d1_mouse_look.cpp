// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cmath>
#include <initializer_list>
#include "core/d1_mouse_look.h"

using namespace Core::D1NativeMouse;

static bool Near(float a, float b) {
    return std::abs(a - b) <= 1e-5f * std::max(1.0f, std::abs(b));
}

int main() {
    // The game turns by rate x dt: a flick turns the same angle at every frame rate, however its
    // counts fall into frames.
    const float counts = 1200.0f;
    const float turn_30 = -counts * StickSecondsPerCount;
    for (unsigned rate : {30u, 60u, 100u, 144u, 240u, 1000u}) {
        const float dt = 1.0f / static_cast<float>(rate);
        assert(ValidLookDt(dt));
        MotionAccumulator motion;
        float yaw_turn = 0.0f, pitch_turn = 0.0f;
        const unsigned frames = rate / 10; // the flick lasts 100 ms
        for (unsigned f = 0; f < frames; ++f) {
            motion.Add(counts / frames, -counts / frames);
            const auto [dx, dy] = motion.Take();
            const LookAdd add = LookRates(dx, dy, dt, 1.0f, false);
            yaw_turn += add.yaw * dt;
            pitch_turn += add.pitch * dt;
        }
        assert(Near(yaw_turn, turn_30));
        assert(Near(pitch_turn, turn_30)); // dy was -counts: the opposite sign of yaw
    }
    // Sensitivity scales linearly; invert flips only the pitch.
    const LookAdd base = LookRates(10, 20, 1.0f / 60, 1.0f, false);
    const LookAdd fast = LookRates(10, 20, 1.0f / 60, 2.0f, true);
    assert(Near(fast.yaw, 2 * base.yaw));
    assert(Near(fast.pitch, -2 * base.pitch));
    // Counts kept while a frame time is unusable reach the next frame whole.
    MotionAccumulator motion;
    motion.Add(5, 5);
    assert(!ValidLookDt(0.0f) && !ValidLookDt(0.5f));
    motion.Add(5, -1);
    const auto [dx, dy] = motion.Take();
    assert(dx == 10.0f && dy == 4.0f);
    const auto [ex, ey] = motion.Take();
    assert(ex == 0.0f && ey == 0.0f);
    // Garbage is clamped, a fast flick is not.
    assert(
        Near(LookRates(1e9f, 0, 1.0f, 1.0f, false).yaw, -MaxCountsPerLook * StickSecondsPerCount));
    assert(Near(LookRates(4000, 0, 1.0f / 30, 1.0f, false).yaw * (1.0f / 30),
                -4000 * StickSecondsPerCount));
}
