// SPDX-FileCopyrightText: Copyright 2026 d1reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Core::D1NativeMouse {

/// Installs the D1 mouse detours that the environment asks for (D1_MOUSE_PROBE). Call once the
/// game's patches are applied, before the guest runs.
void Install();

/// The window's mouse motion (SDL xrel/yrel). Kept for mouse look while the D1 layer has asked for
/// relative mode, where SDL reports raw counts. Call from the window's event loop.
void OnHostMouseMotion(float xrel, float yrel);

} // namespace Core::D1NativeMouse
