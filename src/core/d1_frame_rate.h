// SPDX-FileCopyrightText: Copyright 2026 d1reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Core::D1FrameRate {

/// Starts the frame-rate holder when D1_FPS is set (see d1_frame_rate.cpp). Call once the game's
/// patches are applied.
void Install();

/// The vblank rate the presenter should run at instead of the setting, or 0 to keep the setting.
u32 VblankOverride();

} // namespace Core::D1FrameRate
