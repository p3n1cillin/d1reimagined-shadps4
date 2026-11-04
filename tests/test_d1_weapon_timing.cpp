// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <initializer_list>
#include "core/d1_weapon_timing.h"

int main() {
    using Core::D1FrameRate::WeaponInterval;
    for (unsigned rate : {30u, 60u, 100u, 120u, 144u, 240u, 1000u}) {
        const float native = static_cast<float>(rate) * 4 / 30;
        float elapsed = 0;
        for (unsigned shot = 1; shot <= 60; ++shot) {
            elapsed += WeaponInterval(native, rate, static_cast<int>(shot));
            assert(elapsed == (shot * 4 * rate + 29) / 30);
        }
    }
    assert(WeaponInterval(100.0f * 4 / 30, 100, 0) == 100.0f * 4 / 30);
    assert(WeaponInterval(12.5f, 100, 2) == 12.5f);
    assert(WeaponInterval(0.5f, 144, 2) == 0.5f);
    assert(WeaponInterval(4.01f, 30, 2) == 4.01f);
    assert(WeaponInterval(12.5f, 0, 2) == 12.5f);
}
