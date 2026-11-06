// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// D1 (CUSA00219, executable 56578): the game's own hitch detector, the thread BLOCK_DETECT
// (0x92A3D0). Each pass checks four watched entries of 0x38 bytes. For an entry e ([rsp+0xF0]),
// with its limit e+4 (s32) above zero and the global byte 0x27411C8 clear, it fires once the time
// since e+8 (from the clock at 0x694D0) reaches e+4 + e+0x20 while e+0x10 is zero. Then:
//   e+1 set (and e+0x30 clear): it halts. It writes "RELEASE_VHALT hitch detected: ... stalled"
//       through the fatal-error handler [0x1A231F0], which writes a crash folder and ends the
//       process (0xBE970: remove the SIGILL handler, then ud2);
//   otherwise e+0 set (and e+0x28 clear): a report only, once.
// On a console the watched main loop never stalls for long. On a PC the emulator can: under heavy
// disk load cine-live10 (5 November) stalled 53 s, lost its activity host, recovered into the
// repair state machine, and was then halted by this detector.
//   D1_HITCH_HALT=0  at 0x92A869 (mov rax,[rsp+0xF0], just before `test byte [rax+1],1`) the halt
//                    test reads a zeroed entry, so the detector takes its report-only branch and
//                    the game carries on with its own recovery. Logged once per stall.
//   D1_HITCH_HALT=1  the same detour, logging only (the game still halts).
// Unset: no detour; the game halts as on the console.

#include <array>
#include <cstdlib>
#include <cstring>

#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "core/cpu_patches.h"
#include "core/d1_stability.h"

namespace Core::D1Stability {

namespace {

constexpr u64 HitchHaltTest = 0x92A869;
constexpr std::array<u8, 8> HitchHaltTestSpan = {0x48, 0x8B, 0x84, 0x24,
                                                 0xF0, 0x00, 0x00, 0x00}; // mov rax,[rsp+0xf0]
constexpr std::array<u8, 6> HitchHaltTestNext = {0xF6, 0x40, 0x01, 0x01,  // test byte [rax+1],1
                                                 0x0F, 0x84};             // je (report path)
constexpr size_t EntrySize = 0x38;

bool g_skip_halt = false;
alignas(16) std::array<u8, EntrySize> g_quiet_entry{}; // e+1 clear: the report-only branch

template <typename T>
T Read(u64 address) {
    T v;
    std::memcpy(&v, reinterpret_cast<const void*>(address), sizeof(T));
    return v;
}

void PS4_SYSV_ABI OnHitch(D1DetourRegisters* regs, u64) {
    static u64 last_entry = 0;
    static s64 last_since = 0;
    const u64 entry = regs->rax;
    if (entry == 0 || (Read<u8>(entry + 1) & 1) == 0) {
        return; // no halt is due for this entry
    }
    const s64 since = Read<s64>(entry + 8);
    if (entry != last_entry || since != last_since) {
        last_entry = entry;
        last_since = since;
        LOG_WARNING(Core,
                    "D1 hitch detector: a watched job passed its limit ({} + {}; last update {}); "
                    "{}",
                    Read<s32>(entry + 4), Read<s64>(entry + 0x20), since,
                    g_skip_halt ? "halt skipped (D1_HITCH_HALT=0), the game carries on"
                                : "the game halts (D1_HITCH_HALT=1 logs only)");
    }
    if (g_skip_halt) {
        regs->rax = reinterpret_cast<u64>(g_quiet_entry.data());
    }
}

} // namespace

void Install() {
    const char* v = std::getenv("D1_HITCH_HALT");
    if (v == nullptr || *v == '\0' || MemoryPatcher::g_game_serial != "CUSA00219") {
        return;
    }
    g_skip_halt = std::strcmp(v, "0") == 0;
    const auto* next = reinterpret_cast<const u8*>(MemoryPatcher::g_eboot_address + HitchHaltTest +
                                                   HitchHaltTestSpan.size());
    if (std::memcmp(next, HitchHaltTestNext.data(), HitchHaltTestNext.size()) != 0 ||
        !InstallD1Detour(HitchHaltTest, HitchHaltTestSpan, OnHitch, true)) {
        LOG_ERROR(Core, "D1 hitch detector: the 56578 halt test was not found; left alone");
        return;
    }
    LOG_INFO(Core, "D1 hitch detector: {}",
             g_skip_halt ? "halts skipped, reports kept" : "halts logged");
}

} // namespace Core::D1Stability
