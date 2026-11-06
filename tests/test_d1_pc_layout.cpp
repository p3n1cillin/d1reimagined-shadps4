// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// Every binding in the D1 PC layout names an output and inputs the input parser knows, and no key
// is bound to two actions except the deliberate super chord (F: L1 + R1). The numpad keeps the
// emulator default's face buttons (the lab's boot presses NumPad2 for X), and an unedited file from
// an earlier layout is recognised whatever its line endings.
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include "input/d1_pc_layout.h"
#include "input/input_handler.h"

int main() {
    using namespace Input;
#ifdef _WIN32
    _putenv_s("D1_PC_LAYOUT", "1");
#else
    setenv("D1_PC_LAYOUT", "1", 1);
#endif
    assert(D1PcLayout::Enabled());
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
    // NumPad2 presses cross with the PC layout on, as without it, and so do the other face buttons.
    assert(outputs_of_key["kp2"] == std::set<std::string>{"cross"});
    assert(outputs_of_key["kp4"] == std::set<std::string>{"square"});
    assert(outputs_of_key["kp6"] == std::set<std::string>{"circle"});
    assert(outputs_of_key["kp8"] == std::set<std::string>{"triangle"});
    // The current layout is not taken for an earlier one, and CRLF files hash as LF ones.
    std::string crlf;
    for (const char c : D1PcLayout::Text) {
        crlf += c == '\n' ? "\r\n" : std::string(1, c);
    }
    assert(D1PcLayout::Hash(crlf) == D1PcLayout::Hash(D1PcLayout::Text));
    assert(!D1PcLayout::IsEarlierDefault(D1PcLayout::Text));
    assert(!D1PcLayout::IsEarlierDefault(crlf + "# edited\n"));
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
