// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// D1 56578 only. Keep owned decoded tags outside the emulator sources. Their original bytes
// must match the running client's tag before redirecting its arrays to the candidate's storage.
#include <array>
#include <atomic>
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
std::atomic<bool> labels_ready{false};
std::atomic<unsigned> lookup_reports{0}, use_reports{0};
std::atomic<bool> graph_selected{false};

std::pair<u64, u64> EntryRange() {
    const auto field = english.candidate.data() + 0x48;
    const auto start = reinterpret_cast<u64>(field + 8 + Read<s64>(field + 8) + 16);
    return {start, start + Read<u64>(field) * 16};
}

bool IsEntry(u64 address) {
    const auto [start, end] = EntryRange();
    return address >= start && address < end && (address - start) % 16 == 0;
}

// D1: optional bounded diagnostics avoid the GPU exception overhead of attaching a debugger.
void PS4_SYSV_ABI Lookup(D1DetourRegisters* regs, u64) {
    if (!active)
        return;
    const auto begin = reinterpret_cast<u64>(english.candidate.data());
    if (regs->rax < begin || regs->rax >= begin + english.candidate.size())
        return;
    const bool valid = IsEntry(regs->rax);
    if ((!valid || lookup_reports.load() < 8) && lookup_reports.fetch_add(1) < 32) {
        const auto [start, end] = EntryRange();
        LOG_INFO(Config,
                 "D1 native video lookup: ordinal {} entry {:#x} range {:#x}..{:#x} valid {} "
                 "payload {:#x} candidate {} native head {:#x}",
                 regs->rdx / 4, regs->rax, start, end, valid, regs->rsi,
                 regs->rsi == reinterpret_cast<u64>(english.candidate.data()), regs->rcx);
    }
}

void PS4_SYSV_ABI UseEntry(D1DetourRegisters* regs, u64) {
    if (!active)
        return;
    const auto begin = reinterpret_cast<u64>(english.candidate.data());
    const auto ptr = regs->r14;
    if (ptr >= begin && ptr < begin + english.candidate.size() && !IsEntry(ptr) &&
        use_reports.fetch_add(1) < 8) {
        const auto [start, end] = EntryRange();
        LOG_ERROR(Config, "D1 native video entry use: {:#x}, expected {:#x}..{:#x}, offset {:#x}",
                  ptr, start, end, ptr - begin);
    }
}

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
    if (labels_ready.load())
        return;
    std::lock_guard lock{label_mutex};
    const auto payload = TagRoot(english_tag), index = TagRoot(string_index_tag);
    if (!payload || !index)
        return;
    if (english.Select(payload) != payload && string_index.Select(index) != index &&
        !labels_ready.exchange(true)) {
        LOG_INFO(Config,
                 "D1 native video: original English library verified; lookup selection ready");
    }
}

bool TargetIndex(u64 address) {
    TryLabels();
    return labels_ready.load() &&
           (address == reinterpret_cast<u64>(string_index.candidate.data()) ||
            address == TagRoot(string_index_tag));
}

void PS4_SYSV_ABI SelectLookup(D1DetourRegisters* regs, u64) {
    if (active && TargetIndex(regs->rdi)) {
        // D1: this opt-in candidate supplies English only. Select both complete roots together,
        // including when the caller carries a cached language header, without mutating that cache.
        regs->rdi = reinterpret_cast<u64>(string_index.candidate.data());
        regs->rsi = reinterpret_cast<u64>(english.candidate.data());
    }
}

void PS4_SYSV_ABI SelectTextIndex(D1DetourRegisters* regs, u64) {
    if (active && TargetIndex(regs->rdi))
        regs->rdi = reinterpret_cast<u64>(string_index.candidate.data());
}

void PS4_SYSV_ABI SelectTextPayload(D1DetourRegisters* regs, u64) {
    if (active && labels_ready.load() &&
        regs->r15 == reinterpret_cast<u64>(string_index.candidate.data())) {
        regs->r11 = reinterpret_cast<u64>(english.candidate.data());
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
void PS4_SYSV_ABI SelectGraph(D1DetourRegisters* regs, u64) {
    if (!active)
        return;
    if (graph.Apply(regs->rdi)) {
        // D1: prepare before imports, component construction and property registration, retaining
        // the guest root identity used by the graph context. The binding pass alone is too late.
        if (!graph_selected.exchange(true))
            LOG_INFO(Config, "D1 native video: graph arrays ready before component construction");
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
        bool ok =
            InstallD1Detour(0xa8b2de, std::array<u8, 7>{0x48, 0x8b, 0x47, 0x10, 0x48, 0x85, 0xc0},
                            SelectGraph) &&
            InstallD1Detour(0xa8be60,
                            std::array<u8, 10>{0x45, 0x31, 0xff, 0x48, 0x8b, 0x82, 0x40, 1, 0, 0},
                            Graph) &&
            InstallD1Detour(0x9aea52, std::array<u8, 5>{0x49, 0x8b, 0x44, 0x24, 0x10},
                            SettingsGroup) &&
            InstallD1Detour(0x9af328, std::array<u8, 7>{0x48, 0x8b, 0x41, 0x20, 0x48, 0x85, 0xc0},
                            SettingsRow) &&
            InstallD1Detour(0x9af536, std::array<u8, 5>{0xb8, 0xff, 0xff, 0xff, 0xff}, Get, true) &&
            InstallD1Detour(0x9b040c, std::array<u8, 7>{0x0f, 0xb6, 0x8b, 0xb4, 0, 0, 0}, Set) &&
            InstallD1Detour(0x8d5170, std::array<u8, 5>{0x53, 0x48, 0x8b, 0x47, 0x10},
                            SelectLookup) &&
            InstallD1Detour(0x8d5220, std::array<u8, 6>{0x55, 0x48, 0x89, 0xe5, 0x41, 0x57},
                            SelectTextIndex) &&
            InstallD1Detour(0x8d534c,
                            std::array<u8, 8>{0x49, 0x8d, 0x4b, 0x50, 0x49, 0x03, 0x4b, 0x50},
                            SelectTextPayload);
        if (const char* trace = std::getenv("D1_NATIVE_VIDEO_TRACE");
            ok && trace && std::strcmp(trace, "0") != 0) {
            ok = InstallD1Detour(0x8d520d, std::array<u8, 5>{0x48, 0x8d, 0x44, 0x81, 0x10}, Lookup,
                                 true) &&
                 InstallD1Detour(0x9b6937, std::array<u8, 6>{0x49, 0x8b, 0x06, 0x45, 0x31, 0xff},
                                 UseEntry);
        }
        active = ok;
        LOG_INFO(Config, "D1 native video: experimental hooks {} (native UI requires validation)",
                 ok ? "installed" : "incomplete");
    } catch (const std::exception& e) {
        LOG_ERROR(Config, "D1 native video disabled: {}", e.what());
    }
}
} // namespace Core::D1NativeVideo
