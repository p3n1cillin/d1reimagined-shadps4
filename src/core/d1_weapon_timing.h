// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cmath>
#include <cstdint>

namespace Core::D1FrameRate {
// D1: weapons with an integral PS4 interval can round each burst boundary up once.
// The difference between two boundaries is the interval since the previous shot.
inline float WeaponInterval(float interval, std::uint32_t rate, std::int32_t shots) {
    if (rate <= 30 || rate % 30 == 0 || shots <= 0 || !(interval > 1 && interval < 0x1p24f)) {
        return interval;
    }
    const double at_30 = static_cast<double>(interval) * 30 / rate;
    const auto ticks_at_30 = static_cast<std::uint64_t>(at_30 + 0.5);
    if (ticks_at_30 == 0 || std::abs(at_30 - static_cast<double>(ticks_at_30)) > 1e-5) {
        // The authored interval falls between PS4 ticks. Preserve the native rule for it.
        return interval;
    }
    const auto scaled = ticks_at_30 * rate;
    const auto fraction = scaled % 30;
    const auto n = static_cast<std::uint64_t>(shots);
    const auto current_boundary = (n * fraction + 29) / 30;
    const auto previous_boundary = ((n - 1) * fraction + 29) / 30;
    return static_cast<float>(scaled / 30 + current_boundary - previous_boundary);
}
} // namespace Core::D1FrameRate
