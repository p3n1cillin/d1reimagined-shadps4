// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

union SDL_Event;

namespace Core::D1Video {

// D1 56578 only: called before the guest starts, after the ordinary patches.
void Install();
void Draw();
bool ProcessEvent(const SDL_Event& event);
bool IsOpen();
bool IsEnabled();

} // namespace Core::D1Video
