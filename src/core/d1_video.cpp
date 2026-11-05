// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// D1: a usable video panel while the native settings content is being extended. The renderer
// allocates its pools before gameplay, so resolution is deliberately a next-start setting.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <SDL3/SDL_events.h>
#include <imgui.h>

#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "common/path_util.h"
#include "core/d1_frame_rate.h"
#include "core/d1_video.h"
#include "core/d1_video_config.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Core::D1Video {
namespace {
std::atomic<bool> enabled{false}, open{false};
std::mutex settings_mutex;
int selected_rate = 30;
int selected_resolution = 0;
int running_resolution = 0;
std::string error;
constexpr std::array<int, 6> Rates = {30, 60, 100, 144, 240, 0};
constexpr std::array<const char*, 6> RateNames = {"30", "60", "100", "144", "240", "Uncapped"};
struct Resolution {
    const char* label;
    u32 width, height, pool_mb, arena_mb;
};
constexpr std::array<Resolution, 3> Resolutions = {{
    {"1920 x 1080", 1920, 1080, 450, 2900},
    {"2560 x 1440", 2560, 1440, 512, 2900},
    {"3840 x 2160 (4K)", 3840, 2160, 1152, 3600},
}};

auto ConfigPath() {
    return Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "config.json";
}

std::string ReadConfig() {
    std::ifstream in(ConfigPath(), std::ios::binary);
    if (!in) {
        throw std::runtime_error("Could not read config.json; settings were not saved.");
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool Save() {
    try {
        const auto& res = Resolutions[selected_resolution];
        const std::string original = ReadConfig();
        const std::string changed = WithVideoConfig(
            original, {{"fps", selected_rate}, {"width", res.width}, {"height", res.height}});
        auto tmp = ConfigPath();
        tmp += ".d1-video.tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out.write(changed.data(), changed.size());
            out.close();
            if (!out) {
                throw std::runtime_error("Could not save video settings.");
            }
        }
        // Avoid replacing a config another settings writer changed while we were writing.
        if (ReadConfig() != original) {
            std::filesystem::remove(tmp);
            throw std::runtime_error("Config changed; please retry saving.");
        }
#ifdef _WIN32
        if (!MoveFileExW(tmp.c_str(), ConfigPath().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Could not replace config.json.");
        }
#else
        std::filesystem::rename(tmp, ConfigPath());
#endif
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        LOG_ERROR(Config, "D1 Video: {}", error);
        return false;
    }
}

template <typename T>
void Write(u64 va, T value) {
    std::memcpy(reinterpret_cast<void*>(MemoryPatcher::g_eboot_address + va), &value, sizeof(T));
}

// The exact 56578 instructions also serve as a build guard. No offsets from 01.29 are used.
bool Matches56578() {
    constexpr std::array<u8, 11> display = {0x41, 0xC7, 0x85, 0xF8, 0x28, 0, 0, 0x80, 0x07, 0, 0};
    constexpr std::array<u8, 6> jump = {0x81, 0xCA, 0xFF, 0x03, 0, 0};
    const auto base = MemoryPatcher::g_eboot_address;
    return MemoryPatcher::g_game_serial == "CUSA00219" &&
           MemoryPatcher::g_eboot_image_size > 0x18DEC70 &&
           std::memcmp(reinterpret_cast<void*>(base + 0x7E40CF), display.data(), 7) == 0 &&
           std::memcmp(reinterpret_cast<void*>(base + 0x6F5A32), jump.data(), jump.size()) == 0;
}

void ApplyResolution(const Resolution& res) {
    Write<u16>(0x7ED1DC, res.width); // clamp immediates
    Write<u16>(0x7ED1E0, res.width);
    Write<u16>(0x7ED202, res.height);
    Write<u16>(0x7ED206, res.height);
    Write<u16>(0x18DCC58, res.width);
    Write<u16>(0x18DCC5C, res.height);
    Write<u32>(0x7E40D6, res.width); // display-buffer allocation
    Write<u32>(0x7E40E1, res.height);
    // The native surface table uses full, half, quarter, and narrow full-height targets.
    // Shadow slots 9 and 15..18 keep their own sizes.
    for (u32 slot = 0; slot < 20; ++slot) {
        if (slot == 9 || (slot >= 15 && slot <= 18)) {
            continue;
        }
        const bool narrow = slot == 3 || slot == 4;
        const bool quarter = slot == 5 || slot == 6 || slot == 14;
        const u32 width = res.width / (narrow || quarter ? 4 : slot == 7 ? 2 : 1);
        const u32 height = res.height / (quarter ? 4 : slot == 7 ? 2 : 1);
        Write<u32>(0x18DEA40 + slot * 0x1C + 0x14, width);
        Write<u32>(0x18DEA40 + slot * 0x1C + 0x18, height);
    }
    Write<u32>(0x81A8FA, (res.pool_mb << 20) + 0x10000);
    Write<u32>(0x81A921, res.pool_mb << 20);
    Write<u32>(0xB45A3, res.arena_mb << 20);
    LOG_INFO(Lib_VideoOut, "D1 Video: render {}x{}, pool {} MB, arena {} MB", res.width, res.height,
             res.pool_mb, res.arena_mb);
}
} // namespace

void Install() {
    const char* opt = std::getenv("D1_VIDEO");
    if ((opt && std::strcmp(opt, "0") == 0) || !Matches56578()) {
        return;
    }
    bool saved = false;
    try {
        const auto root = nlohmann::json::parse(ReadConfig());
        if (root.contains("D1Video")) {
            const auto& video = root.at("D1Video");
            const int fps = video.value("fps", 30);
            if (std::find(Rates.begin(), Rates.end(), fps) != Rates.end()) {
                selected_rate = fps;
            }
            const u32 w = video.value("width", 1920u), h = video.value("height", 1080u);
            for (size_t i = 0; i < Resolutions.size(); ++i) {
                if (Resolutions[i].width == w && Resolutions[i].height == h) {
                    selected_resolution = static_cast<int>(i);
                    saved = true;
                }
            }
        }
    } catch (const std::exception& e) {
        error = e.what();
    }
    // Preserve explicit client resolution patches until the player saves an in-game choice.
    if (saved) {
        ApplyResolution(Resolutions[selected_resolution]);
    }
    running_resolution = selected_resolution;
    const char* env = std::getenv("D1_FPS");
    if (env && *env) {
        const int n = std::atoi(env);
        if (n == 0 || (n >= 10 && n <= 1000)) {
            selected_rate = n;
        }
    }
    D1FrameRate::SetRate(selected_rate);
    enabled.store(true);
    LOG_INFO(Lib_VideoOut, "D1 Video: F10 opens video settings");
}

bool IsOpen() {
    return open.load(std::memory_order_relaxed);
}

bool IsEnabled() {
    return enabled.load(std::memory_order_relaxed);
}

int GetNativeOption(unsigned id) {
    std::lock_guard lock{settings_mutex};
    if (!IsEnabled()) {
        return -1;
    }
    if (id == 21) { // D1 56578's original option ids end at 20
        const auto it = std::find(Rates.begin(), Rates.end(), selected_rate);
        return it == Rates.end() ? -1 : static_cast<int>(it - Rates.begin());
    }
    return id == 22 ? selected_resolution : -1;
}

bool SetNativeOption(unsigned id, int value) {
    std::lock_guard lock{settings_mutex};
    if (!IsEnabled() || value < 0 || (id != 21 && id != 22) ||
        value >= static_cast<int>(id == 21 ? Rates.size() : Resolutions.size())) {
        return false;
    }
    const int old_rate = selected_rate, old_resolution = selected_resolution;
    if (id == 21) {
        selected_rate = Rates[value];
    } else {
        selected_resolution = value;
    }
    if (!Save()) {
        selected_rate = old_rate;
        selected_resolution = old_resolution;
        return false;
    }
    if (id == 21) {
        D1FrameRate::SetRate(selected_rate);
    }
    return true;
}

bool ProcessEvent(const SDL_Event& event) {
    if (!enabled.load()) {
        return false;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F10) {
        open.store(!open.load());
        return true;
    }
    if (event.type == SDL_EVENT_KEY_UP && event.key.key == SDLK_F10) {
        return true; // D1: this hotkey belongs to Video, including its release edge
    }
    if (IsOpen() && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        open.store(false);
        return true;
    }
    // Release events must reach the game's pad so opening the panel cannot leave a held key.
    return IsOpen() &&
           (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL ||
            event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
            event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION);
}

void Draw() {
    if (!enabled.load() || !IsOpen()) {
        return;
    }
    std::lock_guard lock{settings_mutex};
    ImGui::SetNextWindowSize(ImVec2(470, 250), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_Appearing);
    bool visible = true;
    if (ImGui::Begin("D1 Video", &visible,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetWindowFocus();
            ImGui::SetKeyboardFocusHere(); // D1: keyboard navigation starts on Frame rate
        }
        int index = 0;
        for (size_t i = 0; i < Rates.size(); ++i) {
            if (Rates[i] == selected_rate) {
                index = static_cast<int>(i);
            }
        }
        if (ImGui::Combo("Frame rate", &index, RateNames.data(), RateNames.size())) {
            const int was = selected_rate;
            selected_rate = Rates[index];
            if (Save()) {
                D1FrameRate::SetRate(selected_rate);
            } else {
                selected_rate = was;
            }
        }
        ImGui::TextDisabled("Applies now. The game follows the speed this PC can hold.");
        if (ImGui::BeginCombo("Resolution", Resolutions[selected_resolution].label)) {
            for (size_t i = 0; i < Resolutions.size(); ++i) {
                if (ImGui::Selectable(Resolutions[i].label, selected_resolution == i)) {
                    const int was = selected_resolution;
                    selected_resolution = static_cast<int>(i);
                    if (!Save()) {
                        selected_resolution = was;
                    }
                }
                if (selected_resolution == i) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled(selected_resolution == running_resolution
                                ? "Resolution applies at the next game start."
                                : "Resolution saved. Restart the game to apply.");
        if (!error.empty()) {
            ImGui::TextWrapped("%s", error.c_str());
        }
        if (ImGui::Button("Close (F10)")) {
            visible = false;
        }
    }
    ImGui::End();
    if (!visible) {
        open.store(false);
    }
}
} // namespace Core::D1Video
