// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "common/types.h"

// ============================================================================
// Windows static guest red-zone protection
// ============================================================================

enum class WindowsGuestRedZoneProtectionMode : u32 {
    Disabled,
    StaticPatching,
};

namespace Core::WindowsGuestRedZoneProtection {

void SetActiveMode(WindowsGuestRedZoneProtectionMode mode) noexcept;
WindowsGuestRedZoneProtectionMode GetActiveMode() noexcept;
bool IsStaticPatchingEnabled() noexcept;

} // namespace Core::WindowsGuestRedZoneProtection

// ============================================================================
// End Windows static guest red-zone protection
// ============================================================================

namespace Core {

// Windows static guest red-zone protection
struct RedZonePatchResult {
    u64 function_count{};
    u64 instruction_count{};
    u64 red_zone_function_count{};
    u64 memory_instruction_count{};
    u64 short_memory_instruction_count{};
    u64 patched_memory_instruction_count{};
    u64 stack_dependent_memory_instruction_count{};
    u64 control_flow_memory_instruction_count{};
    u64 unrelocatable_memory_instruction_count{};
    u64 indirect_red_zone_function_count{};
    u64 cpu_patch_instruction_count{};
    u64 patched_cpu_patch_instruction_count{};
    u64 unsupported_cpu_patch_instruction_count{};
};

/// Registers a module for patching, providing an area to generate trampoline code.
void RegisterPatchModule(void* module_ptr, u64 module_size, void* trampoline_area_ptr,
                         u64 trampoline_area_size);

/// Applies CPU patches that need to be done before beginning executions.
void PrePatchInstructions(u64 segment_addr, u64 segment_size);

// D1: detours. A host callback runs at a pinned guest instruction span with the guest's registers,
// then the span's own instructions run (relocated) and execution continues after it.

/// The guest's general registers and flags at the detour, in the trampoline's push order. A
/// callback may change them; they are restored from here.
struct D1DetourRegisters {
    u64 rflags, r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rbp, rbx, rdx, rcx, rax;
};
using D1DetourCallback = void(PS4_SYSV_ABI*)(D1DetourRegisters* regs, u64 site_vaddr);

/// Installs a detour at the main executable's virtual address `site_vaddr` (as in the patch files,
/// without the 0x400000 bias). Refuses, returning false, unless the bytes there equal `span`, the
/// instructions covering at least a near jump decode and relocate, and the module has trampoline
/// space. Call before the guest runs the site (e.g. right after the game's patches are applied).
bool InstallD1Detour(u64 site_vaddr, std::span<const u8> span, D1DetourCallback callback);

// Windows static guest red-zone protection
/// Keeps Windows exception dispatch outside live guest red zones at faultable memory accesses.
RedZonePatchResult PatchRedZoneMemoryInstructions(u64 segment_addr, u64 segment_size,
                                                  std::span<const uintptr_t> function_starts);

} // namespace Core
