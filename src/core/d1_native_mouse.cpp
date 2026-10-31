// SPDX-FileCopyrightText: Copyright 2026 d1reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// D1 (CUSA00219, executable 56578): the engine's own keyboard/mouse layer, fed from the host.
// The menu cursor update at 0x985D00 (01.29 0x984060; the span `mov ebx,[r15+0x210]`, followed by the
// elapsed-ms subtract and the call to the mouse getter 0xAD670) runs once a frame while a cursor
// exists, with the UI root in r15 and the cursor position as floats at +0x350/+0x354.
//   D1_MOUSE_PROBE=1   log the UI root and cursor position at the first hits; change nothing.
//   D1_MOUSE_CURSOR=1  move the cursor by the host mouse's movement since the last frame, while the
//                      game window has focus (Windows; D1_MOUSE_SENS scales it, default 1: the
//                      engine adds mouse counts to the cursor unscaled).
//   D1_MOUSE_LOOK=1    also mouse look in gameplay (implies the cursor): at the look site 0x91C7DC
//                      (01.29 0x91AB3C, span `mov [rbp-0x320],rbx`), when r13 is player 0's input
//                      record (base+0x273EFE0), the mouse movement is added to the record's look rates
//                      (+0x2CC yaw, +0x2D4 pitch) scaled by the frame's dt at [rbp-0x2DC]. This is the
//                      01.29 implementation's fallback route. While no menu cursor is active the PC
//                      cursor is re-centred in the game window, so it moves the system cursor:
//                      opt in only.
//   D1_MOUSE_BUTTONS=1 mouse buttons merged into player 0's controls after pad selection (0x9284EF,
//                      01.29 0x92684F): in menus LMB = cross, RMB = circle; in gameplay LMB = R2
//                      (fire), RMB = L2 (aim), middle = R3 (melee), Mouse4 = square, Mouse5 = circle.

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "core/cpu_patches.h"
#include "core/d1_native_mouse.h"

namespace Core::D1NativeMouse {

namespace {

constexpr u64 CursorSite = 0x985D00;
constexpr std::array<u8, 7> CursorSpan = {0x41, 0x8B, 0x9F, 0x10, 0x02, 0x00, 0x00};
std::atomic<int> g_probe_hits{0};
float g_sensitivity = 1.0f;

constexpr u64 LookSite = 0x91C7DC;
constexpr std::array<u8, 7> LookSpan = {0x48, 0x89, 0x9D, 0xE0, 0xFC, 0xFF, 0xFF};
constexpr u64 PlayerZeroInputRecord = 0x273EFE0; // per-player array 0x273ED40 + 0x2A0
constexpr u64 DeltaTimeRbpOffset = 0x2DC;
constexpr u64 LookYawRate = 0x2CC;
constexpr u64 LookPitchRate = 0x2D4;
constexpr float StickSecondsPerCount = 0.00025f; // 01.29 BaseStickSecondsPerCount
constexpr float MaxCountsPerFrame = 400.0f;
std::atomic<u64> g_cursor_tick{0};

constexpr u64 ActionsSite = 0x9284EF;
constexpr std::array<u8, 5> ActionsSpan = {0xE8, 0x4C, 0x4C, 0x78, 0xFF}; // call 0xAD140
constexpr size_t ActionStride = 20;                                        // 21 controls

struct Key {
    u16 held_ms{};
    u8 frames{};
};

bool EnvOn(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' && *v != '0';
}

void PS4_SYSV_ABI ProbeCursor(D1DetourRegisters* regs, u64 site) {
    const int n = g_probe_hits.fetch_add(1);
    if (n >= 3) {
        return;
    }
    float xy[2]{};
    std::memcpy(xy, reinterpret_cast<const void*>(regs->r15 + 0x350), sizeof(xy));
    LOG_INFO(Input, "D1 mouse probe: cursor site {:#x} hit {}: UI root {:#x}, cursor ({}, {})", site,
             n + 1, regs->r15, xy[0], xy[1]);
}

#ifdef _WIN32
bool GameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

void PS4_SYSV_ABI MoveCursor(D1DetourRegisters* regs, u64) {
    g_cursor_tick.store(GetTickCount64(), std::memory_order_release);
    static POINT last{};
    static bool have_last = false;
    POINT now{};
    if (!GetCursorPos(&now)) {
        return;
    }
    if (have_last && GameHasFocus()) {
        const float dx = static_cast<float>(now.x - last.x) * g_sensitivity;
        const float dy = static_cast<float>(now.y - last.y) * g_sensitivity;
        if (dx != 0.0f || dy != 0.0f) {
            float xy[2];
            std::memcpy(xy, reinterpret_cast<const void*>(regs->r15 + 0x350), sizeof(xy));
            xy[0] += dx;
            xy[1] += dy;
            std::memcpy(reinterpret_cast<void*>(regs->r15 + 0x350), xy, sizeof(xy));
        }
    }
    last = now;
    have_last = true;
}
bool CursorActive() {
    const u64 tick = g_cursor_tick.load(std::memory_order_acquire);
    return tick != 0 && GetTickCount64() - tick < 100;
}

float LoadFloat(u64 address) {
    float v;
    std::memcpy(&v, reinterpret_cast<const void*>(address), sizeof(v));
    return v;
}

void StoreFloat(u64 address, float v) {
    std::memcpy(reinterpret_cast<void*>(address), &v, sizeof(v));
}

void PS4_SYSV_ABI MouseLook(D1DetourRegisters* regs, u64) {
    static bool have_centre = false;
    if (regs->r13 != MemoryPatcher::g_eboot_address + PlayerZeroInputRecord) {
        return;
    }
    if (CursorActive() || !GameHasFocus()) {
        have_centre = false; // a menu or another window: the next gameplay frame starts clean
        return;
    }
    RECT rect{};
    HWND window = GetForegroundWindow();
    if (!GetClientRect(window, &rect)) {
        return;
    }
    POINT centre{(rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2};
    ClientToScreen(window, &centre);
    POINT now{};
    GetCursorPos(&now);
    SetCursorPos(centre.x, centre.y);
    if (!have_centre) {
        have_centre = true;
        return;
    }
    const float dt = LoadFloat(regs->rbp - DeltaTimeRbpOffset);
    if (!(dt > 0.0001f && dt <= 0.25f)) {
        return;
    }
    const float dx = std::clamp(static_cast<float>(now.x - centre.x), -MaxCountsPerFrame, MaxCountsPerFrame);
    const float dy = std::clamp(static_cast<float>(now.y - centre.y), -MaxCountsPerFrame, MaxCountsPerFrame);
    if (dx == 0.0f && dy == 0.0f) {
        return; // an idle mouse leaves the stick's look alone
    }
    const float gain = StickSecondsPerCount * g_sensitivity / dt;
    const float yaw = LoadFloat(regs->r13 + LookYawRate) - dx * gain;
    const float pitch = LoadFloat(regs->r13 + LookPitchRate) + dy * gain;
    if (std::isfinite(yaw) && std::isfinite(pitch)) {
        StoreFloat(regs->r13 + LookYawRate, yaw);
        StoreFloat(regs->r13 + LookPitchRate, pitch);
    }
}
/// The mouse buttons now (LMB, RMB, MMB, X1, X2), as held time and frame count.
std::array<Key, 5> SampleButtons() {
    static std::array<u64, 5> down_since{};
    static std::array<u8, 5> frames{};
    static constexpr std::array<int, 5> vk{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    std::array<Key, 5> out{};
    const bool focus = GameHasFocus();
    const u64 now = GetTickCount64();
    for (size_t i = 0; i < vk.size(); ++i) {
        const bool down = focus && (GetAsyncKeyState(vk[i]) & 0x8000) != 0;
        if (!down) {
            down_since[i] = 0;
            frames[i] = 0;
            continue;
        }
        if (down_since[i] == 0) {
            down_since[i] = now;
        }
        frames[i] = static_cast<u8>(std::min(255, frames[i] + 1));
        out[i] = {static_cast<u16>(std::min<u64>(0xFFFF, now - down_since[i])), frames[i]};
    }
    return out;
}

/// 01.29's ActionMerge: merge the mouse buttons into the selected pad's controls, keeping the pad.
void PS4_SYSV_ABI MergeButtons(D1DetourRegisters*, u64) {
    static std::array<Key, 21> previous{};
    auto* record = reinterpret_cast<u8*>(MemoryPatcher::g_eboot_address + PlayerZeroInputRecord);
    const bool menu = CursorActive();
    const auto buttons = SampleButtons();
    std::array<Key, 21> mouse{};
    mouse[menu ? 10 : 1] = buttons[0]; // LMB: confirm / fire
    mouse[menu ? 11 : 0] = buttons[1]; // RMB: back / aim
    if (!menu) {
        mouse[9] = buttons[2];  // MMB: right stick click (melee by default)
        mouse[12] = buttons[3]; // Mouse4: square (reload / interact)
        mouse[11] = buttons[4]; // Mouse5: circle (crouch / slide)
    }
    for (const unsigned i : {0u, 1u, 9u, 10u, 11u, 12u}) {
        u8* action = record + i * ActionStride;
        u16 held;
        float value;
        std::memcpy(&held, action, 2);
        std::memcpy(&value, action + 8, 4);
        const bool down = action[2] != 0 || mouse[i].frames != 0;
        Key merged{};
        merged.frames = down ? static_cast<u8>(std::min(255, previous[i].frames + 1)) : 0;
        merged.held_ms = down ? std::max(held, mouse[i].held_ms) : 0;
        action[3] = previous[i].frames;
        action[2] = merged.frames;
        std::memcpy(action, &merged.held_ms, 2);
        if (!down) {
            value = 0.0f;
        }
        if (mouse[i].frames != 0) {
            value = std::max(value, 1.0f);
        }
        std::memcpy(action + 8, &value, 4);
        previous[i] = merged;
    }
}
#endif

} // namespace

void Install() {
    if (const char* s = std::getenv("D1_MOUSE_SENS"); s != nullptr && *s != '\0') {
        g_sensitivity = static_cast<float>(std::atof(s));
    }
    if (EnvOn("D1_MOUSE_CURSOR") || EnvOn("D1_MOUSE_LOOK") || EnvOn("D1_MOUSE_BUTTONS")) {
#ifdef _WIN32
        if (InstallD1Detour(CursorSite, CursorSpan, MoveCursor)) {
            LOG_INFO(Input, "D1 mouse: the menu cursor follows the host mouse (sensitivity {})",
                     g_sensitivity);
        } else {
            LOG_ERROR(Input, "D1 mouse: the cursor detour was refused");
        }
        if (EnvOn("D1_MOUSE_BUTTONS")) {
            if (InstallD1Detour(ActionsSite, ActionsSpan, MergeButtons)) {
                LOG_INFO(Input, "D1 mouse: buttons merged into player 0's controls");
            } else {
                LOG_ERROR(Input, "D1 mouse: the buttons detour was refused");
            }
        }
        if (EnvOn("D1_MOUSE_LOOK")) {
            if (InstallD1Detour(LookSite, LookSpan, MouseLook)) {
                LOG_INFO(Input, "D1 mouse: mouse look in gameplay (player 0, look-rate route)");
            } else {
                LOG_ERROR(Input, "D1 mouse: the look detour was refused");
            }
        }
#else
        LOG_ERROR(Input, "D1 mouse: D1_MOUSE_CURSOR is Windows-only for now");
#endif
        return;
    }
    if (EnvOn("D1_MOUSE_PROBE") && !InstallD1Detour(CursorSite, CursorSpan, ProbeCursor)) {
        LOG_ERROR(Input, "D1 mouse probe: the cursor detour was refused");
    }
}

} // namespace Core::D1NativeMouse
