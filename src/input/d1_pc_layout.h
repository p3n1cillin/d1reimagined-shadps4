// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdlib>
#include <cstring>
#include <string_view>

namespace Input::D1PcLayout {

// D1: the PC keyboard layout for Destiny (CUSA00219), after Destiny 2's PC defaults wherever D1
// has the same action (d1/PC-CONTROLS.md). Keys press the pad buttons the game already reads, so
// every action the pad has is reachable, and the game's own button rules (tap or hold, chords)
// stay as on the console. The mouse buttons, mouse look and Esc are not here: the D1 mouse layer
// (core/d1_native_mouse.cpp) handles them, aware of whether a menu cursor is up.
constexpr std::string_view FileName = "d1-pc.ini";

constexpr std::string_view Text = R"(# D1 Reimagined: the PC layout for Destiny (CUSA00219).
# Written by the emulator when D1_PC_LAYOUT=1 and this file is missing. Edit it to remap; F8 reloads
# it in game, and deleting it restores the default. See d1/PC-CONTROLS.md in the emulator's source.
# The mouse (look, fire, aim, menu clicks) and Esc are handled by the D1 mouse layer, not here.

# Movement
axis_left_y_minus = w
axis_left_y_plus = s
axis_left_x_minus = a
axis_left_x_plus = d
cross = space            # jump
l3 = lshift              # sprint
circle = lctrl           # crouch

# Combat
square = r               # reload (tap)
square = e               # interact (hold)
r1 = c                   # melee
l1 = q                   # grenade
l1 = f                   # super: L1 + R1 together, as on the pad
r1 = f
triangle = 1             # switch weapon: D1 swaps primary and special on a tap
triangle = 2
triangle = grave
triangle = mousewheelup
triangle = mousewheeldown
triangle = 3             # hold for the heavy weapon

# Emotes (the d-pad)
pad_up = up
pad_down = down
pad_left = left
pad_right = right

# Menus
options = i              # character screen (Esc also opens it in play)
touchpad_center = tab    # Ghost / Director
touchpad_center = m
cross = enter            # confirm

# Controller: any SDL gamepad keeps working beside the keyboard
triangle = triangle
cross = cross
square = square
circle = circle
l1 = l1
l2 = l2
l3 = l3
r1 = r1
r2 = r2
r3 = r3
options = options
touchpad_center = back
pad_up = pad_up
pad_down = pad_down
pad_left = pad_left
pad_right = pad_right
axis_left_x = axis_left_x
axis_left_y = axis_left_y
axis_right_x = axis_right_x
axis_right_y = axis_right_y

# Range of deadzones: 1 (almost none) to 127 (max)
analog_deadzone = leftjoystick, 5, 127
analog_deadzone = rightjoystick, 5, 127
# Hall-effect triggers rest slightly above zero, which would read as a held aim.
analog_deadzone = l2, 5, 127
analog_deadzone = r2, 5, 127

override_controller_color = false, 0, 0, 255
)";

/// D1_PC_LAYOUT=1: the PC layout above (and the D1 mouse layer) for CUSA00219.
inline bool Enabled() {
    const char* v = std::getenv("D1_PC_LAYOUT");
    return v != nullptr && *v != '\0' && std::strcmp(v, "0") != 0;
}

} // namespace Input::D1PcLayout
