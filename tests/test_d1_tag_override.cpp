// SPDX-FileCopyrightText: Copyright 2026 D1 Reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <chrono>
#include <iostream>
#include "core/d1_tag_override.h"

using namespace Core::D1NativeVideo::Detail;
template <typename T>
void Put(std::vector<u8>& b, size_t o, T value) {
    std::memcpy(b.data() + o, &value, sizeof value);
}
void File(const std::filesystem::path& p, const std::vector<u8>& b) {
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), b.size());
    assert(out.good());
}
int main() {
    std::vector<u8> original(72), candidate;
    Put<u64>(original, 0, original.size());
    Put<u64>(original, 8, 1);
    Put<s64>(original, 16, 32); // array head at48; payload at64
    Put<u32>(original, 44, 0x80800184);
    Put<u64>(original, 48, 1);
    Put<u32>(original, 56, 7);
    Put<u32>(original, 64, 123);
    candidate = original;
    candidate.resize(104);
    Put<u64>(candidate, 0, candidate.size());
    Put<u64>(candidate, 8, 2);
    Put<s64>(candidate, 16, 64); // appended head at80; payload at96
    Put<u32>(candidate, 76, 0x80800184);
    Put<u64>(candidate, 80, 2);
    Put<u32>(candidate, 88, 7);
    Put<u32>(candidate, 96, 123);
    Put<u32>(candidate, 100, 456);
    const auto dir = std::filesystem::temp_directory_path() /
                     ("d1-tag-override-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    const auto orig_path = dir / "test-original.bin", cand_path = dir / "test-candidate.bin";
    File(orig_path, original);
    File(cand_path, candidate);
    Override o;
    o.LoadPair(dir, "test", {{8, 4}});
    auto live = original;
    live[24] = 1;
    const auto altered = live;
    assert(!o.Apply(reinterpret_cast<uintptr_t>(live.data())));
    assert(live == altered); // mismatched native content is never changed
    live = original;
    assert(o.Apply(reinterpret_cast<uintptr_t>(live.data())));
    assert(Read<u64>(live.data() + 8) == 2);
    const auto head = reinterpret_cast<uintptr_t>(live.data()) + 16 + Read<s64>(live.data() + 16);
    assert(Read<u32>(reinterpret_cast<void*>(head + 16)) == 123);
    assert(Read<u32>(reinterpret_cast<void*>(head + 20)) == 456);
    assert(!o.Apply(reinterpret_cast<uintptr_t>(live.data()))); // idempotent
    live = original;
    assert(
        o.Apply(reinterpret_cast<uintptr_t>(live.data()))); // reloaded original at the same address
    for (int failure = 0; failure < 2; ++failure) {
        auto invalid = candidate;
        if (failure == 0) {
            invalid[24] = 1; // changed unrelated prefix
        } else {
            Put<s64>(invalid, 16, 10000); // out-of-bounds array
        }
        File(cand_path, invalid);
        bool rejected = false;
        try {
            Override bad;
            bad.LoadPair(dir, "test", {{8, 4}});
        } catch (const std::exception&) {
            rejected = true;
        }
        assert(rejected);
    }
    std::filesystem::remove(orig_path);
    std::filesystem::remove(cand_path);
    std::filesystem::remove(dir);
    std::cout << "Decoded tag redirection tests passed\n";
}
