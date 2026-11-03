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
#include <nlohmann/json.hpp>

#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "core/cpu_patches.h"
#include "core/d1_native_video.h"
#include "core/d1_tag_override.h"
#include "core/d1_video.h"

namespace Core::D1NativeVideo {
namespace {
using Detail::Override;
using Detail::Read;
Override graph, settings;
Override string_index, english;
u32 string_index_tag = 0, english_tag = 0;
std::mutex label_mutex;
bool active = false; // set before the guest runs; failed installation leaves every callback inert

u64 TagRoot(u32 tag) {
    // D1 56578's inlined tag-manager lookup (9AEA04..52). Preserve its signed slot selection
    // and unsigned address arithmetic; the language payload may not yet be resident.
    const s32 shifted = static_cast<s32>(tag) >> 13;
    const u32 slot = ((static_cast<u32>(shifted) >> 18) | 0x3ff) & static_cast<u32>(shifted);
    const auto manager =
        Read<u64>(reinterpret_cast<const void*>(MemoryPatcher::g_eboot_address + 0x1a238a0));
    if (!manager)
        return 0;
    const auto slots = Read<u64>(reinterpret_cast<const void*>(manager));
    if (!slots)
        return 0;
    const auto row = slots + slot * 64;
    const auto entries = Read<u64>(reinterpret_cast<const void*>(row + 0x10));
    const auto stride = Read<u32>(reinterpret_cast<const void*>(row + 0x30));
    if (!entries || !stride || stride > 4096)
        return 0;
    const u64 mask = static_cast<u64>(Read<s32>(reinterpret_cast<const void*>(row + 0x34)));
    const auto entry = entries + (tag & 0x1fff) * stride;
    return entry - (Read<u64>(reinterpret_cast<const void*>(entry + 8)) & mask);
}

void TryLabels() {
    std::lock_guard lock{label_mutex};
    const auto payload = TagRoot(english_tag), index = TagRoot(string_index_tag);
    if (!payload || !index)
        return;
    if (english.Apply(payload))
        LOG_INFO(Config, "D1 native video: English labels redirected");
    if (english.IsApplied(payload) && string_index.Apply(index)) {
        LOG_INFO(Config, "D1 native video: label index redirected");
    }
}

void PS4_SYSV_ABI Graph(D1DetourRegisters* regs, u64) {
    if (active) {
        TryLabels();
        if (graph.Apply(regs->rdx)) {
            LOG_INFO(Config, "D1 native video: graph arrays redirected");
        }
    }
}
void PS4_SYSV_ABI SettingsGroup(D1DetourRegisters* regs, u64) {
    if (active) {
        TryLabels();
        if (settings.Apply(regs->r12)) {
            LOG_INFO(Config, "D1 native video: settings arrays redirected");
        }
    }
}
void PS4_SYSV_ABI SettingsRow(D1DetourRegisters* regs, u64) {
    if (active) {
        TryLabels();
        if (settings.Apply(regs->rcx)) {
            LOG_INFO(Config, "D1 native video: settings arrays redirected");
        }
    }
}
void PS4_SYSV_ABI Get(D1DetourRegisters* regs, u64) {
    if (active)
        TryLabels();
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
        string_index.LoadPair(dir, "string-index", {{8, 4}});
        english.LoadPair(dir, "english", {{8, 32}, {0x38, 1}, {0x48, 16}});
        std::ifstream manifest(std::filesystem::path{dir} / "manifest.json");
        const auto metadata = nlohmann::json::parse(manifest);
        string_index_tag = metadata.at("strings_index_tag").get<u32>();
        english_tag = metadata.at("english_tag").get<u32>();
        if (metadata.at("setting_ids").at("frame_rate") != 21 ||
            metadata.at("setting_ids").at("resolution") != 22 ||
            english_tag != Read<u32>(string_index.original.data() + 0x18)) {
            throw std::runtime_error("Native video manifest disagrees with supported option ids");
        }
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
        LOG_INFO(Config, "D1 native video: experimental hooks {} (native UI requires validation)",
                 ok ? "installed" : "incomplete");
    } catch (const std::exception& e) {
        LOG_ERROR(Config, "D1 native video disabled: {}", e.what());
    }
}
} // namespace Core::D1NativeVideo
