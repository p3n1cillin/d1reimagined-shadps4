// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// Every binding in the D1 PC layout names an output and inputs the input parser knows, and no key
// is bound to two actions except the deliberate super chord (F: L1 + R1).
#include <algorithm>
#include <cassert>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include "input/d1_pc_layout.h"
#include "input/input_handler.h"

int main() {
    using namespace Input;
    std::istringstream text{std::string(D1PcLayout::Text)};
    std::string line;
    std::map<std::string, std::set<std::string>> outputs_of_key;
    int bindings = 0;
    while (std::getline(text, line)) {
        line.erase(std::remove_if(line.begin(), line.end(),
                                  [](unsigned char c) { return std::isspace(c); }),
                   line.end());
        line = line.substr(0, line.find('#'));
        if (line.empty()) {
            continue;
        }
        const auto eq = line.find('=');
        assert(eq != std::string::npos);
        const std::string output = line.substr(0, eq);
        const std::string input = line.substr(eq + 1);
        if (output == "analog_deadzone" || output == "override_controller_color") {
            continue;
        }
        const bool button = string_to_cbutton_map.contains(output);
        const bool axis = string_to_axis_map.contains(output);
        assert(button || axis);
        assert(input.find(',') == std::string::npos); // single keys only
        const bool key = string_to_keyboard_key_map.contains(input);
        const bool pad =
            string_to_cbutton_map.contains(input) || string_to_axis_map.contains(input);
        assert(key || pad);
        if (key) {
            outputs_of_key[input].insert(output);
        }
        ++bindings;
    }
    assert(bindings > 40);
    for (const auto& [key, outputs] : outputs_of_key) {
        assert(outputs.size() == 1 || (key == "f" && outputs == std::set<std::string>{"l1", "r1"}));
    }
    // Mouse buttons and Esc belong to the D1 mouse layer, not the pad config.
    for (const char* reserved : {"leftbutton", "rightbutton", "middlebutton", "sidebuttonback",
                                 "sidebuttonforward", "escape"}) {
        assert(!outputs_of_key.contains(reserved));
    }
    // The emulator's own hotkeys (global.ini) stay free.
    for (const char* hotkey : {"f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12",
                               "delete", "kpplus", "kpminus"}) {
        assert(!outputs_of_key.contains(hotkey));
    }
}
