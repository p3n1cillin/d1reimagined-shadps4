// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Core::D1Stability {

/// Installs the opt-in stability detours (D1_HITCH_HALT, see d1_stability.cpp). Call once the
/// game's patches are applied.
void Install();

} // namespace Core::D1Stability
