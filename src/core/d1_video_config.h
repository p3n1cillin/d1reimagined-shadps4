// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cctype>
#include <string>
#include <nlohmann/json.hpp>

namespace Core::D1Video {

// D1: replace only our root value, retaining every other byte (including CRLF and unknown keys).
// Validate first; malformed files must never be overwritten with defaults.
inline std::string WithVideoConfig(std::string text, const nlohmann::json& video) {
    const auto root = nlohmann::json::parse(text);
    if (!root.is_object()) {
        throw std::runtime_error("config must be a JSON object");
    }
    auto skip_space = [&text](size_t& i) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
    };
    auto string_end = [&text](size_t i) {
        for (++i; i < text.size(); ++i) {
            if (text[i] == '\\') {
                ++i;
            } else if (text[i] == '"') {
                return i + 1;
            }
        }
        throw std::runtime_error("unterminated JSON string");
    };
    size_t i = text.find('{') + 1;
    skip_space(i);
    while (text[i] != '}') {
        const size_t key_start = i;
        i = string_end(i);
        const auto key = nlohmann::json::parse(text.substr(key_start, i - key_start));
        skip_space(i);
        ++i; // colon (the parser already validated the document)
        skip_space(i);
        const size_t start = i;
        int depth = 0;
        while (i < text.size()) {
            const char c = text[i];
            if (c == '"') {
                i = string_end(i);
                continue;
            }
            if (depth == 0 && (c == ',' || c == '}')) {
                break;
            }
            depth += c == '{' || c == '[';
            depth -= c == '}' || c == ']';
            ++i;
        }
        size_t end = i;
        while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
            --end;
        }
        if (key == "D1Video") {
            text.replace(start, end - start, video.dump());
            return text;
        }
        if (text[i] == ',') {
            ++i;
            skip_space(i);
        }
    }
    text.insert(i, (root.empty() ? "" : ",") + std::string("\"D1Video\":") + video.dump());
    return text;
}

} // namespace Core::D1Video
