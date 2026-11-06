// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace Libraries::AudioOut::D1AudioSync {

// D1: D1_AUDIO_SYNC=1 paces audio output by the host device, as sceAudioOutOutput is paced by the
// console's audio hardware, instead of by a wall-clock schedule. Two independent clocks drift
// apart: upstream's queue either grows until the whole backlog is cleared (an audible drop), or
// runs dry (crackle). Its limit is counted in the game's 8-channel bytes but compared with the
// downmixed stereo stream, so with Destiny's Float_8CH_Std port on a stereo device the audio could
// lag the picture by up to 16 buffers (341 ms) before the clear. With the device pacing it, the
// queue holds about two game buffers (43 ms at 1024 frames, 48 kHz), and the backlog limit is
// counted in the stream's own bytes.
inline bool Enabled() {
    const char* v = std::getenv("D1_AUDIO_SYNC");
    return v != nullptr && *v != '\0' && std::strcmp(v, "0") != 0;
}

/// Bytes the host stream may hold before an output waits: two of the game's buffers, converted.
inline std::uint32_t TargetQueueBytes(std::uint32_t stream_buffer_bytes) {
    return 2 * stream_buffer_bytes;
}

/// The backlog that is cleared as a last resort, in the stream's bytes: four of the larger of the
/// game's converted buffer and the device's buffer.
inline std::uint32_t ClearThresholdBytes(std::uint32_t stream_buffer_bytes,
                                         std::uint32_t device_frames,
                                         std::uint32_t stream_channels) {
    const std::uint32_t device_bytes = device_frames * 4u * stream_channels; // f32 samples
    return 4 * std::max(stream_buffer_bytes, device_bytes);
}

/// The output thread's period: a little shorter than one buffer, so the device's queue, not the
/// thread's timer, sets the pace.
inline std::uint64_t ThreadPeriodNs(std::uint64_t buffer_ns) {
    return buffer_ns * 15 / 16;
}

} // namespace Libraries::AudioOut::D1AudioSync
