// SPDX-FileCopyrightText: Copyright 2026 d1reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// D1 (CUSA00219, executable 56578): the engine's own keyboard/mouse layer, fed from the host.
// First step: D1_MOUSE_PROBE=1 detours the menu cursor update at 0x985D00 (01.29 0x984060; the span
// `mov ebx,[r15+0x210]`, followed by the elapsed-ms subtract and the call to the mouse getter 0xAD670)
// and logs the UI root and the cursor position it holds at +0x350/+0x354. It changes nothing.

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>

#include "common/logging/log.h"
#include "core/cpu_patches.h"
#include "core/d1_native_mouse.h"

namespace Core::D1NativeMouse {

namespace {

constexpr u64 CursorSite = 0x985D00;
constexpr std::array<u8, 7> CursorSpan = {0x41, 0x8B, 0x9F, 0x10, 0x02, 0x00, 0x00};
std::atomic<int> g_probe_hits{0};

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

} // namespace

void Install() {
    const char* probe = std::getenv("D1_MOUSE_PROBE");
    if (probe == nullptr || *probe == '\0' || *probe == '0') {
        return;
    }
    if (!InstallD1Detour(CursorSite, CursorSpan, ProbeCursor)) {
        LOG_ERROR(Input, "D1 mouse probe: the cursor detour was refused");
    }
}

} // namespace Core::D1NativeMouse
