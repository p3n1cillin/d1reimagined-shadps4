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
// D1: native cycling rows use the same persistence and live rate writer as the panel.
int GetNativeOption(unsigned id);
bool SetNativeOption(unsigned id, int value);

} // namespace Core::D1Video
