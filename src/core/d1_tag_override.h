// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "common/types.h"

namespace Core::D1NativeVideo::Detail {
constexpr size_t Limit = 1024 * 1024;
struct Field {
    size_t offset, stride;
};
template <typename T>
T Read(const void* p) {
    T result;
    std::memcpy(&result, p, sizeof result);
    return result;
}
inline std::vector<u8> Load(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (!size || size > Limit) {
        throw std::runtime_error("Decoded video tag size outside limit");
    }
    std::ifstream input(path, std::ios::binary);
    std::vector<u8> bytes{std::istreambuf_iterator<char>(input), {}};
    if (bytes.size() != size || size < 8 || Read<u64>(bytes.data()) != size) {
        throw std::runtime_error("Decoded video tag length disagrees");
    }
    return bytes;
}
struct Override {
    std::vector<u8> original, candidate;
    std::vector<Field> fields;
    std::mutex mutex;

    // D1: select a complete immutable root for consumers that cache relative descriptors.
    // Mutating a resident root's relative pointer is unsafe if that header is later copied.
    u64 Select(u64 address) const {
        if (address && Read<u64>(reinterpret_cast<const void*>(address)) == original.size() &&
            std::memcmp(reinterpret_cast<const void*>(address), original.data(), original.size()) ==
                0) {
            return reinterpret_cast<u64>(candidate.data());
        }
        return address;
    }

    bool IsApplied(u64 address) const {
        const auto* root = reinterpret_cast<const u8*>(address);
        if (!address || Read<u64>(root) != original.size())
            return false;
        for (const auto& f : fields) {
            const auto* head =
                candidate.data() + f.offset + 8 + Read<s64>(candidate.data() + f.offset + 8);
            const auto rel = static_cast<s64>(reinterpret_cast<uintptr_t>(head)) -
                             static_cast<s64>(address + f.offset + 8);
            if (Read<s64>(root + f.offset + 8) != rel ||
                Read<u64>(root + f.offset) != Read<u64>(candidate.data() + f.offset))
                return false;
        }
        return true;
    }

    void LoadPair(const std::filesystem::path& dir, const char* stem, std::vector<Field> arrays) {
        original = Load(dir / (std::string{stem} + "-original.bin"));
        candidate = Load(dir / (std::string{stem} + "-candidate.bin"));
        fields = std::move(arrays);
        if (candidate.size() < original.size()) {
            throw std::runtime_error("Video candidate shrinks original tag");
        }
        // Only the self-length and declared root-array fields may differ in the original prefix.
        for (size_t i = 8; i < original.size(); ++i) {
            bool owned = false;
            for (const auto& f : fields) {
                owned |= i >= f.offset && i < f.offset + 16;
            }
            if (!owned && original[i] != candidate[i]) {
                throw std::runtime_error("Video candidate changes unrelated tag bytes");
            }
        }
        for (const auto& f : fields) {
            if (f.offset + 16 > original.size()) {
                throw std::runtime_error("Video root field outside tag");
            }
            const auto count = Read<u64>(candidate.data() + f.offset);
            const auto rel = Read<s64>(candidate.data() + f.offset + 8);
            const s64 head = static_cast<s64>(f.offset + 8) + rel;
            if (count > Limit / f.stride || head < 4 ||
                static_cast<u64>(head) + 16 > candidate.size() ||
                count * f.stride > candidate.size() - head - 16 ||
                Read<u32>(candidate.data() + head - 4) != 0x80800184 ||
                Read<u64>(candidate.data() + head) != count) {
                throw std::runtime_error("Video root array outside candidate");
            }
        }
    }

    bool Apply(u64 address) {
        auto* root = reinterpret_cast<u8*>(address);
        if (Read<u64>(root) != original.size()) {
            return false;
        }
        std::lock_guard lock{mutex};
        bool applied = true;
        for (const auto& f : fields) {
            const auto* head =
                candidate.data() + f.offset + 8 + Read<s64>(candidate.data() + f.offset + 8);
            const auto rel = static_cast<s64>(reinterpret_cast<uintptr_t>(head)) -
                             static_cast<s64>(address + f.offset + 8);
            applied &= Read<s64>(root + f.offset + 8) == rel &&
                       Read<u64>(root + f.offset) == Read<u64>(candidate.data() + f.offset);
        }
        if (applied || std::memcmp(root, original.data(), original.size()) != 0) {
            return false;
        }
        for (const auto& f : fields) {
            const auto* head =
                candidate.data() + f.offset + 8 + Read<s64>(candidate.data() + f.offset + 8);
            const auto rel = static_cast<s64>(reinterpret_cast<uintptr_t>(head)) -
                             static_cast<s64>(address + f.offset + 8);
            std::memcpy(root + f.offset + 8, &rel, sizeof rel);
            std::memcpy(root + f.offset, candidate.data() + f.offset, sizeof(u64));
        }
        return true;
    }
};
} // namespace Core::D1NativeVideo::Detail
