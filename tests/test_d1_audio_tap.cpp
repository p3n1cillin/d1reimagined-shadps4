// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include "core/libraries/audio/d1_audio_tap.h"

using namespace Libraries::AudioOut::D1AudioTap;

int main() {
    const unsigned rate = 48000;
    const unsigned block = 256;
    const unsigned long long ns = 1'000'000'000ULL;
    const auto ns_of = [&](unsigned long long frames) { return frames * ns / rate; };

    // An exact-pace stream pads nothing: each block ends at "now".
    {
        unsigned long long written = 0;
        for (unsigned i = 1; i <= 1000; ++i) {
            assert(D1TapPadFrames(ns_of(i * block), rate, written, block) == 0);
            written += block;
        }
    }
    // Jitter under 100 ms (a block that arrives 90 ms late) pads nothing.
    {
        unsigned long long written = 10 * block;
        const auto late = ns_of(11 * block) + 90'000'000ULL;
        assert(D1TapPadFrames(late, rate, written, block) == 0);
    }
    // A 5 s stall is padded, and afterwards the file holds the elapsed frames (within rounding).
    {
        unsigned long long written = 100 * block;
        const auto elapsed = ns_of(101 * block) + 5 * ns;
        const auto pad = D1TapPadFrames(elapsed, rate, written, block);
        assert(pad >= 5ULL * rate - 1 && pad <= 5ULL * rate + 1);
        written += pad + block;
        const auto elapsed_frames = elapsed / ns * rate + (elapsed % ns) * rate / ns;
        assert(written == elapsed_frames);
        // The next block, on pace again, pads nothing.
        assert(D1TapPadFrames(elapsed + ns_of(block), rate, written, block) == 0);
    }
    // Bursts (faster than real time) never pad and never go negative.
    {
        unsigned long long written = 0;
        for (unsigned i = 0; i < 100; ++i) {
            assert(D1TapPadFrames(ns_of(i * block / 4), rate, written, block) == 0);
            written += block;
        }
        assert(D1TapPadFrames(0, rate, written, block) == 0);
    }
    // No sample rate, no padding.
    assert(D1TapPadFrames(5 * ns, 0, 0, block) == 0);
    // Ten hours of nanoseconds does not overflow.
    {
        const unsigned long long ten_hours = 10ULL * 3600 * ns;
        const auto pad = D1TapPadFrames(ten_hours, rate, 0, block);
        assert(pad == 10ULL * 3600 * rate - block);
        assert(D1TapPadFrames(ten_hours, rate, pad + block, block) == 0);
    }
    // The threshold is 100 ms.
    assert(kPadThresholdFrames(rate) == 4800);
}
