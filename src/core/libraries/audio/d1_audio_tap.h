// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdlib>
#include <cstring>

#include "common/types.h"

namespace Libraries::AudioOut::D1AudioTap {

// D1: the audio tap file is lined up with the video by its start time alone, so it has to hold one
// frame per frame of wall-clock time. The game sends nothing while it loads; without padding the
// file is shorter than the recording and the audio runs early by the length of every gap.

/// D1_AUDIO_TAP_PAD=0 turns the padding off; it is on whenever the tap is.
inline bool PadEnabled() {
    const char* v = std::getenv("D1_AUDIO_TAP_PAD");
    return v == nullptr || *v == '\0' || std::strcmp(v, "0") != 0;
}

/// A gap shorter than this (100 ms) is jitter and is left alone.
constexpr u64 kPadThresholdFrames(u32 sample_rate) {
    return sample_rate / 10;
}

/// Frames of silence to write BEFORE the next block so the file's length tracks wall-clock time.
///   elapsed_ns     : steady-clock time since the tap file was opened (>= 0)
///   sample_rate    : frames per second of the tap file
///   frames_written : frames already in the file (samples + earlier padding)
///   block_frames   : frames in the block about to be written
/// The block is taken to END at "now": the file should hold elapsed*rate frames after it is
/// written. The whole deficit is padded once it exceeds the threshold. Never negative.
constexpr u64 D1TapPadFrames(u64 elapsed_ns, u32 sample_rate, u64 frames_written,
                             u32 block_frames) {
    if (sample_rate == 0) {
        return 0;
    }
    constexpr u64 ns_per_s = 1'000'000'000;
    const u64 elapsed_frames =
        elapsed_ns / ns_per_s * sample_rate + (elapsed_ns % ns_per_s) * sample_rate / ns_per_s;
    const u64 have = frames_written + block_frames;
    if (elapsed_frames <= have) {
        return 0;
    }
    const u64 deficit = elapsed_frames - have;
    return deficit > kPadThresholdFrames(sample_rate) ? deficit : 0;
}

} // namespace Libraries::AudioOut::D1AudioTap
