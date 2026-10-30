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

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/logging/log.h"
#include "core/cpu_patches.h"
#include "core/d1_native_mouse.h"

namespace Core::D1NativeMouse {

namespace {

constexpr u64 CursorSite = 0x985D00;
constexpr std::array<u8, 7> CursorSpan = {0x41, 0x8B, 0x9F, 0x10, 0x02, 0x00, 0x00};
std::atomic<int> g_probe_hits{0};
float g_sensitivity = 1.0f;

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
#endif

} // namespace

void Install() {
    if (const char* s = std::getenv("D1_MOUSE_SENS"); s != nullptr && *s != '\0') {
        g_sensitivity = static_cast<float>(std::atof(s));
    }
    if (EnvOn("D1_MOUSE_CURSOR")) {
#ifdef _WIN32
        if (InstallD1Detour(CursorSite, CursorSpan, MoveCursor)) {
            LOG_INFO(Input, "D1 mouse: the menu cursor follows the host mouse (sensitivity {})",
                     g_sensitivity);
        } else {
            LOG_ERROR(Input, "D1 mouse: the cursor detour was refused");
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
