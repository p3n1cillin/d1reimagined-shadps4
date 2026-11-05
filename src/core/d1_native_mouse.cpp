// SPDX-FileCopyrightText: Copyright 2026 d1reimagined contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// D1 (CUSA00219, executable 56578): the engine's own keyboard/mouse layer, fed from the host.
// The menu cursor update at 0x985D00 (01.29 0x984060; the span `mov ebx,[r15+0x210]`, followed by
// the elapsed-ms subtract and the call to the mouse getter 0xAD670) runs once a frame while a
// cursor exists, with the UI root in r15 and the cursor position as floats at +0x350/+0x354.
//   D1_MOUSE_PROBE=1   log the UI root and cursor position at the first hits; change nothing.
//   D1_MOUSE_CURSOR=1  move the cursor by the host mouse's movement since the last frame, while the
//                      game window has focus (Windows; D1_MOUSE_SENS scales it, default 1: the
//                      engine adds mouse counts to the cursor unscaled).
//   D1_MOUSE_LOOK=1    also mouse look in gameplay (implies the cursor): at the look site 0x91C7DC
//                      (01.29 0x91AB3C, span `mov [rbp-0x320],rbx`), when r13 is player 0's input
//                      record (base+0x273EFE0), the mouse movement is added to the record's look
//                      rates
//                      (+0x2CC yaw, +0x2D4 pitch) scaled by the frame's dt at [rbp-0x2DC]. This is
//                      the 01.29 implementation's fallback route. The movement is the raw input
//                      (SDL relative mode, Windows raw input: no pointer acceleration, no cursor
//                      re-centring), switched on while no menu cursor is up and the game window
//                      has focus, so the turn per count is the same at every frame rate.
//                      D1_MOUSE_RAW=0 keeps the earlier route that re-centres the system cursor.
//   D1_MOUSE_LOOK_SENS  look sensitivity (default D1_MOUSE_SENS, else 1).
//   D1_MOUSE_ADS_SENS   multiplies it while aim (L2, control 0) is held (default 1).
//   D1_MOUSE_INVERT_Y=1 inverts the mouse's pitch (the game's own invert setting acts on the
//                      stick only).
//                      No reticle friction or slowdown is added to the mouse, as on Destiny 2's PC
//                      mouse; the weapons' own bullet magnetism is the game's and applies as on
//                      the console. For the console's full stick aim assist, aim with a pad.
//   D1_MOUSE_BUTTONS=1 mouse buttons merged into player 0's controls after pad selection (0x9284EF,
//                      01.29 0x92684F): in menus LMB = cross, RMB = circle; in gameplay LMB = R2
//                      (fire), RMB = L2 (aim), middle = R3 (melee), Mouse4 = square, Mouse5 =
//                      circle.
//   D1_KBM=1           the engine's own PC keyboard layer: at the KBM builder entry 0x926965 (01.29
//                      0x924CC5, right after 0xAD120 clears the blocks) the 104 keys are written,
//                      in the engine's order, to the key table that follows the mouse state at
//                      0x1AD4860, and the mouse-enable byte 0x1AD40D3 is set, so the engine reads
//                      the keyboard with its own bindings. Mouse counts stay zero (look goes
//                      through D1_MOUSE_LOOK).
//   D1_KBM_MAP=R=E,1=R  rebinds the layer: each HOST=ENGINE pair makes the host key press that
//   engine
//                      key instead of its own (names as in EngineKeyNames, any case). The engine's
//                      keys are a developer layout (builder 0x926910): Space = cross, Q = circle, E
//                      = square (reload / interact), R = triangle (weapon), T / G = d-pad up /
//                      down, End = L2, Ins = R2, PgDn / PgUp = controls 6 / 7, Z = 8, B = R3, WASD
//                      move, arrows look.
//   D1_PC_LAYOUT=1     the PC layout (input/d1_pc_layout.h, d1/PC-CONTROLS.md): the keyboard
//                      through the pad config, plus the cursor, buttons and raw look above, and
//                      Esc: circle (back) while a menu cursor is up, else Options (the character
//                      screen).

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "core/cpu_patches.h"
#include "core/d1_mouse_look.h"
#include "core/d1_native_mouse.h"
#include "core/d1_video.h"
#include "input/d1_pc_layout.h"

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
constexpr float MaxCountsPerFrame = 400.0f; // the re-centring route only
std::atomic<u64> g_cursor_tick{0};
bool g_raw = true;
bool g_menu_esc = false;
float g_look_sensitivity = 1.0f;
float g_ads_sensitivity = 1.0f;
bool g_invert_y = false;
MotionAccumulator g_motion;
std::atomic<int> g_relative{-1}; // the relative mode last asked for: -1 never, 0 off, 1 on

constexpr u64 ActionsSite = 0x9284EF;
constexpr std::array<u8, 5> ActionsSpan = {0xE8, 0x4C, 0x4C, 0x78, 0xFF}; // call 0xAD140
constexpr size_t ActionStride = 20;                                       // 21 controls

struct Key {
    u16 held_ms{};
    u8 frames{};
};

constexpr u64 KbmSite = 0x926965;
constexpr std::array<u8, 7> KbmSpan = {0x48, 0x8D, 0x05, 0xD4,
                                       0x83, 0xE1, 0x01}; // lea rax,[0x273ED40]
constexpr u64 MouseState =
    0x1AD4860; // 24-byte mouse block, then 104 x {u16 held_ms, u8 frames, u8 flags}
constexpr u64 MouseEnableByte = 0x1AD40D3;
// The engine's key order (Xbox 56578's table; 01.29's queries agree: WASD = 0x20/0x2D/0x2E/0x2F,
// arrows 0x4D..0x50, space 0x48), as Windows virtual keys.
constexpr std::array<u16, 104> EngineKeys = {
    VK_ESCAPE,   VK_F1,      VK_F2,        VK_F3,         VK_F4,      VK_F5,       VK_F6,
    VK_F7,       VK_F8,      VK_F9,        VK_F10,        VK_F11,     VK_F12,      VK_F13,
    VK_SCROLL,   VK_F15,     VK_OEM_3,     '1',           '2',        '3',         '4',
    '5',         '6',        '7',          '8',           '9',        '0',         VK_OEM_MINUS,
    VK_OEM_PLUS, VK_BACK,    VK_TAB,       'Q',           'W',        'E',         'R',
    'T',         'Y',        'U',          'I',           'O',        'P',         VK_OEM_4,
    VK_OEM_6,    VK_OEM_5,   VK_CAPITAL,   'A',           'S',        'D',         'F',
    'G',         'H',        'J',          'K',           'L',        VK_OEM_1,    VK_OEM_7,
    VK_RETURN,   VK_LSHIFT,  'Z',          'X',           'C',        'V',         'B',
    'N',         'M',        VK_OEM_COMMA, VK_OEM_PERIOD, VK_OEM_2,   VK_RSHIFT,   VK_LCONTROL,
    VK_LWIN,     VK_LMENU,   VK_SPACE,     VK_RMENU,      VK_RWIN,    VK_APPS,     VK_RCONTROL,
    VK_UP,       VK_DOWN,    VK_LEFT,      VK_RIGHT,      VK_INSERT,  VK_HOME,     VK_PRIOR,
    VK_DELETE,   VK_END,     VK_NEXT,      VK_NUMLOCK,    VK_DIVIDE,  VK_MULTIPLY, VK_NUMPAD0,
    VK_NUMPAD1,  VK_NUMPAD2, VK_NUMPAD3,   VK_NUMPAD4,    VK_NUMPAD5, VK_NUMPAD6,  VK_NUMPAD7,
    VK_NUMPAD8,  VK_NUMPAD9, VK_SUBTRACT,  VK_ADD,        VK_RETURN,  VK_DECIMAL};
constexpr std::array<const char*, 104> EngineKeyNames = {
    "Esc",  "F1",    "F2",     "F3",       "F4",   "F5",   "F6",    "F7",   "F8",   "F9",   "F10",
    "F11",  "F12",   "F13",    "ScrLk",    "F15",  "`",    "1",     "2",    "3",    "4",    "5",
    "6",    "7",     "8",      "9",        "0",    "-",    "=",     "Back", "Tab",  "Q",    "W",
    "E",    "R",     "T",      "Y",        "U",    "I",    "O",     "P",    "[",    "]",    "\\",
    "Caps", "A",     "S",      "D",        "F",    "G",    "H",     "J",    "K",    "L",    ";",
    "'",    "Enter", "LShift", "Z",        "X",    "C",    "V",     "B",    "N",    "M",    ",",
    ".",    "/",     "RShift", "LCtrl",    "LWin", "LAlt", "Space", "RAlt", "RWin", "Apps", "RCtrl",
    "Up",   "Down",  "Left",   "Right",    "Ins",  "Home", "PgUp",  "Del",  "End",  "PgDn", "NumLk",
    "Num/", "Num*",  "Num0",   "Num1",     "Num2", "Num3", "Num4",  "Num5", "Num6", "Num7", "Num8",
    "Num9", "Num-",  "Num+",   "NumEnter", "Num."};
// The host keys that press each engine key: itself, unless D1_KBM_MAP moves it.
std::array<std::vector<u16>, 104> g_key_sources;

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
    LOG_INFO(Input, "D1 mouse probe: cursor site {:#x} hit {}: UI root {:#x}, cursor ({}, {})",
             site, n + 1, regs->r15, xy[0], xy[1]);
}

#ifdef _WIN32
bool GameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId() && !D1Video::IsOpen(); // D1: overlay owns input
}

void SDLCALL ApplyRelativeMode(void*) {
    // On the main thread. The window is remembered from when it had focus, to switch back off.
    static SDL_Window* window = nullptr;
    if (SDL_Window* focus = SDL_GetKeyboardFocus(); focus != nullptr) {
        window = focus;
    }
    if (window != nullptr) {
        SDL_SetWindowRelativeMouseMode(window, g_relative.load() == 1);
    }
}

/// Asks the window for relative (raw) mouse mode on or off; true when that changed the request.
bool SetRelativeMode(bool on) {
    if (g_relative.exchange(on ? 1 : 0) == (on ? 1 : 0)) {
        return false;
    }
    SDL_RunOnMainThread(ApplyRelativeMode, nullptr, false);
    return true;
}

void PS4_SYSV_ABI MoveCursor(D1DetourRegisters* regs, u64) {
    g_cursor_tick.store(GetTickCount64(), std::memory_order_release);
    static POINT last{};
    static bool have_last = false;
    if (g_raw && SetRelativeMode(false)) {
        have_last = false; // leaving relative mode may move the system cursor
    }
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
    const float dx =
        std::clamp(static_cast<float>(now.x - centre.x), -MaxCountsPerFrame, MaxCountsPerFrame);
    const float dy =
        std::clamp(static_cast<float>(now.y - centre.y), -MaxCountsPerFrame, MaxCountsPerFrame);
    if (dx == 0.0f && dy == 0.0f) {
        return; // an idle mouse leaves the stick's look alone
    }
    const float gain = StickSecondsPerCount * g_look_sensitivity / dt;
    const float yaw = LoadFloat(regs->r13 + LookYawRate) - dx * gain;
    const float pitch = LoadFloat(regs->r13 + LookPitchRate) + dy * gain;
    if (std::isfinite(yaw) && std::isfinite(pitch)) {
        StoreFloat(regs->r13 + LookYawRate, yaw);
        StoreFloat(regs->r13 + LookPitchRate, pitch);
    }
}

/// Mouse look from raw input: the counts since the last gameplay frame, in relative mode.
void PS4_SYSV_ABI MouseLookRaw(D1DetourRegisters* regs, u64) {
    if (regs->r13 != MemoryPatcher::g_eboot_address + PlayerZeroInputRecord) {
        return;
    }
    const bool play = !CursorActive() && GameHasFocus();
    SetRelativeMode(play);
    if (!play) {
        g_motion.Clear(); // a menu or another window: no turn saved up for later
        return;
    }
    const float dt = LoadFloat(regs->rbp - DeltaTimeRbpOffset);
    if (!ValidLookDt(dt)) {
        return; // the counts wait for the next frame
    }
    const auto [dx, dy] = g_motion.Take();
    if (dx == 0.0f && dy == 0.0f) {
        return; // an idle mouse leaves the stick's look alone
    }
    const auto* aim = reinterpret_cast<const u8*>(regs->r13); // control 0: L2, aim
    const float sensitivity = g_look_sensitivity * (aim[2] != 0 ? g_ads_sensitivity : 1.0f);
    const LookAdd add = LookRates(dx, dy, dt, sensitivity, g_invert_y);
    const float yaw = LoadFloat(regs->r13 + LookYawRate) + add.yaw;
    const float pitch = LoadFloat(regs->r13 + LookPitchRate) + add.pitch;
    if (std::isfinite(yaw) && std::isfinite(pitch)) {
        StoreFloat(regs->r13 + LookYawRate, yaw);
        StoreFloat(regs->r13 + LookPitchRate, pitch);
    }
}
/// The mouse buttons now (LMB, RMB, MMB, X1, X2), as held time and frame count.
std::array<Key, 5> SampleButtons() {
    static std::array<u64, 5> down_since{};
    static std::array<u8, 5> frames{};
    static constexpr std::array<int, 5> vk{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1,
                                           VK_XBUTTON2};
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

/// Esc as held time and frame count; a press that began outside the game (closing the Video
/// panel, another window) is ignored until it is released.
Key SampleEscape() {
    static u64 down_since = 0;
    static u8 frames = 0;
    static bool blocked = false;
    const bool focus = GameHasFocus();
    const bool down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (!down) {
        blocked = false;
    } else if (!focus) {
        blocked = true;
    }
    if (!down || blocked) {
        down_since = 0;
        frames = 0;
        return {};
    }
    const u64 now = GetTickCount64();
    if (down_since == 0) {
        down_since = now;
    }
    frames = static_cast<u8>(std::min(255, frames + 1));
    return {static_cast<u16>(std::min<u64>(0xFFFF, now - down_since)), frames};
}

Key Either(Key a, Key b) {
    return {std::max(a.held_ms, b.held_ms), std::max(a.frames, b.frames)};
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
    if (g_menu_esc) {
        const unsigned esc = menu ? 11u : 6u; // Esc: circle (back) in menus, else Options
        mouse[esc] = Either(mouse[esc], SampleEscape());
    }
    static constexpr std::array<unsigned, 7> merged_controls{0, 1, 9, 10, 11, 12, 6};
    for (const unsigned i : std::span(merged_controls).first(g_menu_esc ? 7 : 6)) {
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
/// The engine's KBM layer: write the 104 keys after the builder cleared them, and enable the layer.
void PS4_SYSV_ABI InjectKeys(D1DetourRegisters*, u64) {
    static std::array<u64, 104> down_since{};
    static std::array<u8, 104> frames{};
    const u64 base = MemoryPatcher::g_eboot_address;
    const bool focus = GameHasFocus();
    const u64 now = GetTickCount64();
    auto* keys = reinterpret_cast<u8*>(base + MouseState + 24);
    for (size_t i = 0; i < EngineKeys.size(); ++i) {
        bool down = false;
        for (const u16 vk : g_key_sources[i]) {
            down = down || (focus && (GetAsyncKeyState(vk) & 0x8000) != 0);
        }
        if (!down) {
            down_since[i] = 0;
            frames[i] = 0;
        } else {
            if (down_since[i] == 0) {
                down_since[i] = now;
            }
            frames[i] = static_cast<u8>(std::min(255, frames[i] + 1));
        }
        const u16 held = down ? static_cast<u16>(std::min<u64>(0xFFFF, now - down_since[i])) : 0;
        std::memcpy(keys + i * 4, &held, 2);
        keys[i * 4 + 2] = frames[i];
    }
    *reinterpret_cast<u8*>(base + MouseEnableByte) = focus ? 1 : 0;
}

int KeyIndex(const std::string& name) {
    for (size_t i = 0; i < EngineKeyNames.size(); ++i) {
        const std::string candidate = EngineKeyNames[i];
        if (candidate.size() == name.size() &&
            std::equal(candidate.begin(), candidate.end(), name.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                       std::tolower(static_cast<unsigned char>(b));
            })) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// Each engine key starts pressed by its own host key; D1_KBM_MAP's HOST=ENGINE pairs move a host
/// key.
void LoadKeyMap() {
    for (size_t i = 0; i < EngineKeys.size(); ++i) {
        g_key_sources[i] = {EngineKeys[i]};
    }
    const char* map = std::getenv("D1_KBM_MAP");
    if (map == nullptr || *map == '\0') {
        return;
    }
    std::vector<std::pair<int, int>> pairs;
    std::string text = map;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = std::min(text.find(',', start), text.size());
        const std::string item = text.substr(start, end - start);
        // The engine name may itself be "=", so split at the first '=' after the host name.
        const size_t eq = item.find('=', 1);
        const int host = eq == std::string::npos ? -1 : KeyIndex(item.substr(0, eq));
        const int engine = eq == std::string::npos ? -1 : KeyIndex(item.substr(eq + 1));
        if (host < 0 || engine < 0) {
            LOG_ERROR(Input, "D1 KBM: ignoring map entry '{}' (HOST=ENGINE key names)", item);
        } else {
            pairs.emplace_back(host, engine);
        }
        start = end + 1;
    }
    for (const auto& [host, engine] : pairs) {
        for (auto& sources : g_key_sources) {
            std::erase(sources, EngineKeys[host]);
        }
    }
    for (const auto& [host, engine] : pairs) {
        g_key_sources[engine].push_back(EngineKeys[host]);
        LOG_INFO(Input, "D1 KBM: host {} presses engine {}", EngineKeyNames[host],
                 EngineKeyNames[engine]);
    }
}
#endif

} // namespace

void OnHostMouseMotion(float xrel, float yrel) {
#ifdef _WIN32
    if (g_relative.load(std::memory_order_relaxed) == 1) {
        g_motion.Add(xrel, yrel); // raw counts: SDL's relative mode
    }
#endif
}

namespace {
float EnvFloat(const char* name, float fallback) {
    const char* s = std::getenv(name);
    if (s == nullptr || *s == '\0') {
        return fallback;
    }
    const float v = static_cast<float>(std::atof(s));
    return std::isfinite(v) && v > 0.0f ? v : fallback;
}
} // namespace

void Install() {
    g_sensitivity = EnvFloat("D1_MOUSE_SENS", 1.0f);
    g_look_sensitivity = EnvFloat("D1_MOUSE_LOOK_SENS", g_sensitivity);
    g_ads_sensitivity = EnvFloat("D1_MOUSE_ADS_SENS", 1.0f);
    g_invert_y = EnvOn("D1_MOUSE_INVERT_Y");
    if (const char* raw = std::getenv("D1_MOUSE_RAW"); raw && std::strcmp(raw, "0") == 0) {
        g_raw = false;
    }
    const bool pc_layout = Input::D1PcLayout::Enabled();
    g_menu_esc = pc_layout;
    if (EnvOn("D1_KBM")) {
#ifdef _WIN32
        LoadKeyMap();
        if (InstallD1Detour(KbmSite, KbmSpan, InjectKeys)) {
            LOG_INFO(Input, "D1 KBM: the engine's own keyboard layer is fed and enabled");
        } else {
            LOG_ERROR(Input, "D1 KBM: the builder detour was refused");
        }
#endif
    }
    const bool buttons = pc_layout || EnvOn("D1_MOUSE_BUTTONS");
    const bool look = pc_layout || EnvOn("D1_MOUSE_LOOK");
    if (pc_layout || EnvOn("D1_MOUSE_CURSOR") || look || buttons) {
#ifdef _WIN32
        if (InstallD1Detour(CursorSite, CursorSpan, MoveCursor)) {
            LOG_INFO(Input, "D1 mouse: the menu cursor follows the host mouse (sensitivity {})",
                     g_sensitivity);
        } else {
            LOG_ERROR(Input, "D1 mouse: the cursor detour was refused");
        }
        if (buttons) {
            if (InstallD1Detour(ActionsSite, ActionsSpan, MergeButtons)) {
                LOG_INFO(Input, "D1 mouse: buttons merged into player 0's controls{}",
                         g_menu_esc ? " (and Esc)" : "");
            } else {
                LOG_ERROR(Input, "D1 mouse: the buttons detour was refused");
            }
        }
        if (look) {
            if (InstallD1Detour(LookSite, LookSpan, g_raw ? MouseLookRaw : MouseLook)) {
                LOG_INFO(Input,
                         "D1 mouse: mouse look in gameplay (player 0, look-rate route, {}; "
                         "sensitivity {}, aiming x{}{})",
                         g_raw ? "raw input" : "cursor re-centring", g_look_sensitivity,
                         g_ads_sensitivity, g_invert_y ? ", inverted" : "");
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
