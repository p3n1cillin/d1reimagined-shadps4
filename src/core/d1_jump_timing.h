// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace Core::D1FrameRate {
// D1: keep the authored 30 Hz jump impulses at their original boundaries. Dividing an impulse
// among subticks delays part of it and loses height, even when its total velocity change agrees.
inline bool JumpImpulseDue(std::int32_t held, std::uint32_t rate) {
    if (rate <= 30 || held <= 0) {
        return true;
    }
    const auto tick = static_cast<std::uint64_t>(held);
    return tick * 30 / rate != (tick - 1) * 30 / rate;
}
} // namespace Core::D1FrameRate
