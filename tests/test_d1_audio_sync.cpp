// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include "core/libraries/audio/d1_audio_sync.h"

using namespace Libraries::AudioOut::D1AudioSync;

int main() {
    // Destiny's port: 1024 frames of Float_8CH_Std at 48 kHz, downmixed to a stereo device.
    const unsigned frames = 1024;
    const unsigned stereo_buffer = frames * 4 * 2;
    // The stream holds two converted buffers (about 43 ms) before an output waits.
    assert(TargetQueueBytes(stereo_buffer) == 2 * stereo_buffer);
    // The last-resort clear is four stream buffers, not upstream's 16 (it counted 8-channel
    // bytes against the stereo stream).
    assert(ClearThresholdBytes(stereo_buffer, 480, 2) == 4 * stereo_buffer);
    const unsigned upstream = 4 * (frames * 4 * 8);
    assert(upstream / stereo_buffer == 16);
    // A device buffer larger than the game's sets the clear instead.
    assert(ClearThresholdBytes(stereo_buffer, 2048, 2) == 4 * 2048 * 4 * 2);
    // The clear always leaves room above the target.
    for (unsigned device : {0u, 256u, 480u, 1024u, 4096u}) {
        assert(ClearThresholdBytes(stereo_buffer, device, 2) > TargetQueueBytes(stereo_buffer));
    }
    // The output thread polls a little faster than the buffer period.
    const unsigned long long period = 1000000000ULL * frames / 48000;
    assert(ThreadPeriodNs(period) < period && ThreadPeriodNs(period) > period * 9 / 10);
}
