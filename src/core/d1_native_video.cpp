// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// D1 56578 only. Keep owned decoded tags outside the emulator sources. Their original bytes
// must match the running client's tag before redirecting its arrays to the candidate's storage.
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "common/logging/log.h"
#include "core/cpu_patches.h"
#include "core/d1_native_video.h"
#include "core/d1_tag_override.h"
#include "core/d1_video.h"

namespace Core::D1NativeVideo {
namespace {
using Detail::Override;
using Detail::Read;
Override graph, settings;
bool active = false; // set before the guest runs; failed installation leaves every callback inert

void PS4_SYSV_ABI Graph(D1DetourRegisters* regs, u64) {
    if (active) {
        if (graph.Apply(regs->rdx)) {
            LOG_INFO(Config, "D1 native video: graph arrays redirected");
        }
    }
}
void PS4_SYSV_ABI SettingsGroup(D1DetourRegisters* regs, u64) {
    if (active) {
        if (settings.Apply(regs->r12)) {
            LOG_INFO(Config, "D1 native video: settings arrays redirected");
        }
    }
}
void PS4_SYSV_ABI SettingsRow(D1DetourRegisters* regs, u64) {
    if (active) {
        if (settings.Apply(regs->rcx)) {
            LOG_INFO(Config, "D1 native video: settings arrays redirected");
        }
    }
}
void PS4_SYSV_ABI Get(D1DetourRegisters* regs, u64) {
    const unsigned id = regs->rdi & 0xff;
    if (active && (id == 21 || id == 22)) {
        // The original out-of-range branch returns eax unchanged at this site.
        regs->rax = static_cast<u32>(D1Video::GetNativeOption(id));
    }
}
void PS4_SYSV_ABI Set(D1DetourRegisters* regs, u64) {
    const auto* writer = reinterpret_cast<const u8*>(regs->rbx);
    const unsigned id = Read<u8>(writer + 0xb4);
    if (active && (id == 21 || id == 22)) {
        D1Video::SetNativeOption(id, Read<s32>(writer + 0xb8));
        // Original ids21/22 take the default branch and never write native user-options bytes.
    }
}
} // namespace

void Install() {
    const char* dir = std::getenv("D1_NATIVE_VIDEO_DIR");
    if (!dir || !*dir || !D1Video::IsEnabled()) {
        return;
    }
    try {
        settings.LoadPair(dir, "settings", {{8, 40}, {0x18, 40}});
        graph.LoadPair(dir, "graph", {{0x48, 16}, {0xc8, 4}, {0x138, 64}});
        const bool ok =
            InstallD1Detour(0xa8be60,
                            std::array<u8, 10>{0x45, 0x31, 0xff, 0x48, 0x8b, 0x82, 0x40, 1, 0, 0},
                            Graph) &&
            InstallD1Detour(0x9aea52, std::array<u8, 5>{0x49, 0x8b, 0x44, 0x24, 0x10},
                            SettingsGroup) &&
            InstallD1Detour(0x9af328, std::array<u8, 7>{0x48, 0x8b, 0x41, 0x20, 0x48, 0x85, 0xc0},
                            SettingsRow) &&
            InstallD1Detour(0x9af53b, std::array<u8, 5>{0x83, 0xf9, 0x13, 0x77, 0x6e}, Get) &&
            InstallD1Detour(0x9b040c, std::array<u8, 7>{0x0f, 0xb6, 0x8b, 0xb4, 0, 0, 0}, Set);
        active = ok;
        LOG_INFO(Config, "D1 native video: experimental hooks {} (labels still require validation)",
                 ok ? "installed" : "incomplete");
    } catch (const std::exception& e) {
        LOG_ERROR(Config, "D1 native video disabled: {}", e.what());
    }
}
} // namespace Core::D1NativeVideo
