// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <mutex>
#include <utility>

namespace Core::D1NativeMouse {

// D1: the mouse's share of player 0's look rates at the 56578 look site (d1_native_mouse.cpp).
// The game turns by rate x dt each frame, so adding counts x k / dt turns the camera by counts x k
// whatever the frame rate: the same flick turns the same angle at 30, 60 or 240.
constexpr float StickSecondsPerCount = 0.00025f; // 01.29 BaseStickSecondsPerCount
// Only a guard against garbage: a fast 8000 DPI flick over a 100 ms frame stays well inside it.
constexpr float MaxCountsPerLook = 16384.0f;

struct LookAdd {
    float yaw;
    float pitch;
};

/// A frame time the look site can use; with any other the counts wait for the next frame.
inline bool ValidLookDt(float dt) {
    return dt > 0.0001f && dt <= 0.25f;
}

/// The yaw and pitch rates to add for dx, dy counts over a frame of dt seconds.
inline LookAdd LookRates(float dx, float dy, float dt, float sensitivity, bool invert_y) {
    dx = std::clamp(dx, -MaxCountsPerLook, MaxCountsPerLook);
    dy = std::clamp(dy, -MaxCountsPerLook, MaxCountsPerLook);
    const float gain = StickSecondsPerCount * sensitivity / dt;
    return {-dx * gain, (invert_y ? -dy : dy) * gain};
}

/// Raw mouse counts from the window's event thread, taken by the game thread once a frame.
class MotionAccumulator {
public:
    void Add(float dx, float dy) {
        std::scoped_lock lock{mutex};
        x += dx;
        y += dy;
    }
    std::pair<float, float> Take() {
        std::scoped_lock lock{mutex};
        const std::pair<float, float> out{x, y};
        x = y = 0.0f;
        return out;
    }
    void Clear() {
        Take();
    }

private:
    std::mutex mutex;
    float x = 0.0f;
    float y = 0.0f;
};

} // namespace Core::D1NativeMouse
