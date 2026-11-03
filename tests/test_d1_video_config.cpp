// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Standalone: clang++ -std=c++20 -Isrc -Iexternals/json/include tests/test_d1_video_config.cpp
#include <cassert>
#include <iostream>
#include "core/d1_video_config.h"

int main() {
    using Core::D1Video::WithVideoConfig;
    const nlohmann::json setting = {{"fps", 60}, {"width", 3840}, {"height", 2160}};
    const std::string prefix =
        "{\r\n  \"Other\" : [1,{\"escaped\":\"}\\\"\\\\\"}],\r\n  \"D1Video\" : ";
    const std::string suffix = "  ,\r\n  \"Unknown\":1.00e+2\r\n}\r\n";
    const auto changed = WithVideoConfig(prefix + "{\"fps\":30}" + suffix, setting);
    assert(changed == prefix + setting.dump() + suffix);
    assert(nlohmann::json::parse(changed)["D1Video"] == setting);
    for (const std::string source : {"{}", "{ \r\n}\r\n", "{\"Other\":{\"D1Video\":7}}\n"}) {
        const auto added = WithVideoConfig(source, setting);
        auto wanted = nlohmann::json::parse(source);
        wanted["D1Video"] = setting;
        assert(nlohmann::json::parse(added) == wanted);
        assert(WithVideoConfig(added, setting) == added);
    }
    for (const std::string invalid : {"", "{bad", "null", "[]"}) {
        bool rejected = false;
        try {
            WithVideoConfig(invalid, setting);
        } catch (...) {
            rejected = true;
        }
        assert(rejected);
    }
    std::cout << "D1 Video config: exact-byte preservation, insert, repeat, malformed: PASS\n";
}
