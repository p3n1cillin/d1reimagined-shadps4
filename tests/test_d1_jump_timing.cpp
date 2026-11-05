// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cmath>
#include <iostream>
#include "core/d1_jump_timing.h"

int main() {
    using Core::D1FrameRate::JumpImpulseDue;
    // Every authored impulse arrives once, within one simulation tick of its PS4 boundary.
    // Test a second, including fractional boundaries at 100 and 144 Hz.
    for (unsigned rate : {30u, 60u, 100u, 120u, 144u, 240u, 1000u}) {
        int count = 0;
        for (unsigned tick = 1; tick <= rate; ++tick) {
            if (!JumpImpulseDue(tick, rate)) {
                continue;
            }
            ++count;
            const double delay = double(tick) / rate - double(count) / 30;
            assert(delay >= -1e-12 && delay < 1.0 / rate + 1e-12);
        }
        assert(count == 30);
        assert(JumpImpulseDue(0, rate)); // the native takeoff supplies the first impulse
    }
    assert(JumpImpulseDue(1, 0)); // absent clock preserves the native behavior
    std::cout << "Jump impulse boundary tests passed\n";
}
