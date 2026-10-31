# shadPS4 for D1 Reimagined

This is the shadPS4 build the [D1 Reimagined](https://github.com/p3n1cillin/d1reimagined-source)
project uses to run Destiny (PS4, CUSA00219, build 56578 / 01.33) against its own servers. It is
upstream shadPS4 **v0.19.0** (`c7e065d`, "tagged 0.19.0 release") plus a short series of commits
on the `d1` branch. Everything here is GPL-2.0-or-later, like upstream.

Nothing in this repository is game data. You need your own legally dumped copy of the game and
your own console's system modules; see "What is not here" below.

## The changes

Each change is one commit on `d1`, with the reason in its message (`git log v.0.19.0..d1`).

| Area | Commit subject | Why Destiny needs it |
|---|---|---|
| Networking | net: a real guest address, and TCP sendmsg/recvmsg on Windows | The stock IP probe yields 127.0.0.1 in a contained lab, which Destiny rejects; multi-buffer sends on TCP silently sent 0 bytes on Windows and the client's network job spun until it halted. |
| shadNet | shadnet: retry the startup probe and allow 10 s per attempt | Under boot load the 3 s probe sometimes missed the local server's handshake and disabled networking for the run. |
| CPU | cpu_patches: do not JIT-decode an unreadable guest address | About half of world loads crashed inside the instruction decoder. |
| Shaders | shader: V_CMP_CLASS_F32 with a single NaN kind | Destiny's shaders test QuietNaN alone, which hit UNREACHABLE. |
| Graphics | texture cache: Destiny's colour-grading LUT keeps all its slices | Without it, shadows are crushed and blue-tinted and lit areas blow out (the whole look of the game depends on this LUT). |
| Audio | audio: D1_AUDIO_TAP writes the game's own audio for recordings | Progress clips carry game audio only, never a microphone. |

## Building

### Windows (the reference setup)

Requirements: Visual Studio 2022 Build Tools (C++ workload and Windows SDK), CMake 3.24 or newer,
Ninja, and LLVM/clang-cl (the reference build uses LLVM 23.1.3). Clone with submodules:

```
git clone --recursive -b d1 https://github.com/p3n1cillin/d1reimagined-shadps4.git
cd d1reimagined-shadps4
d1\Build-D1ShadPS4.cmd
```

`Build-D1ShadPS4.cmd [source-dir] [build-dir]` reads `LLVM_BIN`, `VCVARS` and `NINJA_DIR` from the
environment when your tools are not in the default places, and builds Release with Discord RPC and
the updater turned off (a lab binary must not reach the network by itself). The result is
`build-d1\shadPS4.exe`.

### Linux

Follow upstream's `documents/building-linux.md`, and configure with the same switches:

```
cmake -S . -B build-d1 -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_DISCORD_RPC=OFF -DENABLE_UPDATER=OFF
cmake --build build-d1 --parallel
```

## Settings

Start shadPS4 once so it writes `user/config.json`, then change these keys (everything else stays
at upstream's defaults). `config.d1.json` in this folder lists exactly these values.

| Key | Value | Why |
|---|---|---|
| `General.shad_net_enabled` | `true` | The game's PSN-side calls go to the project's local shadNet server. |
| `General.shadnet_server` | `127.0.0.1:31313` | The project's `shadnet-server` (in d1reimagined-source). Never the public shadNet server. |
| `General.shadnet_webapi_server` | `http://127.0.0.1:31315` | Same, for the web API. |
| `General.connected_to_network` | `true` | The game signs on to the project's local servers. |
| `General.enable_upnp` | `false` | Nothing should open ports on your router. |
| `Vulkan.pipeline_cache_enabled` | `true` | Much shorter loads after the first run. |
| `GPU.window_width` / `window_height` | `1920` / `1080` | For recordings; any size works for play. |
| `GPU.internal_screen_width` / `internal_screen_height` | `1920` / `1080` | The display resolution the game is told. |

The keyboard bindings the project's input scripts expect are upstream's defaults
(`user/input_config/default.ini`): cross `kp2`, circle `kp6`, square `kp4`, triangle `kp8`,
L2 `e`, R2 `o`, left stick `w a s d`, right stick `i j k l`.

## Environment variables

All are off unless set. None of them changes behaviour that upstream games rely on.

| Variable | Effect |
|---|---|
| `D1_GUEST_IP=a.b.c.d` | The guest's own IP address. Used only if it belongs to this machine; otherwise a UDP route probe picks it (nothing is sent). |
| `D1_VOLUME_SLICES=0..3` | How a render target over the colour-grading LUT is handled. Default 3 (a fresh 2D image, the fix); 1 and 2 are earlier experiments; 0 is upstream's handling of that case. The other LUT fixes (expanding, extending the predecessor) are always on. |
| `D1_AUDIO_TAP=DIR` | While `DIR/rec.flag` exists, every audio port writes its float samples to `DIR/port<N>-<rate>-<channels>-<unix ms>.f32`. The project's `tools/ps4_setup/d1-record.py` creates the flag, captures the window and muxes the clip. |
| `D1_MOUSE_PROBE=1` | Detours the menu cursor update (56578, 0x985D00) and logs the UI root and cursor position at the first hits; changes nothing. Proves the detour framework (`InstallD1Detour`). |
| `D1_MOUSE_LOOK=1` | Also mouse look in gameplay: the PC mouse's movement is added to player 0's look rates at the 56578 look site (0x91C7DC), scaled by the frame time, and the PC cursor is re-centred in the game window while no menu cursor is active. Moves the system cursor, so opt in only. |
| `D1_MOUSE_CURSOR=1` | The menu cursor follows the host mouse while the game window has focus (Windows). `D1_MOUSE_SENS` scales it (default 1, the engine's own unscaled rate). |
| `D1_FLIP_RATE=N` | Replaces the flip rate the title requests (Destiny asks for 1: every second vblank, 30 Hz). `0` flips on every vblank, for the 60 Hz engine-clock patch. Unset keeps the title's rate. |
| `D1_DUMP_DIR=DIR` | Creating `DIR.flag` dumps every texture sampled in the next ~0.2 s (raw, linear, in draw order) plus `events.txt`. |
| `D1_TEX_SURVEY=1` | Logs each shader's distinct texture shapes and the render targets each pixel shader writes, once. |
| `D1_TRACE_IMAGE=<hex address>` | Traces the texture cache's lookups, creation, expansion and freeing at that address. |
| `D1_META_RUN=1` | Still runs a compute dispatch over memory once registered as HTILE (diagnostic). |

## How it fits together

The rest of the environment lives in
[d1reimagined-source](https://github.com/p3n1cillin/d1reimagined-source):

- `ps4/BOOTSTRAP.md`: the whole setup, from your own console dump to a first boot;
- `ps4/d1-server`: the Rust servers the game talks to (`d1-responder`, `d1-activity-host`,
  `shadnet-server`, ...);
- `tools/ps4_setup`: the Windows launch, pad, walk and recording scripts
  (`Start-D1ContainedBoot.ps1`, `d1-lab-served.ps1`, `d1-agr-run.ps1`, `d1-pad.ps1`,
  `d1-walk.py`, `d1-record.py`);
- `notes/`: what works, what does not, and how each fix was found.

## What is not here

- **The game.** Dump your own copy from a console you own (`ps4/capture-rig/CFW-DUMP-RUNBOOK.md`
  in d1reimagined-source). No game files, patches of game files, or extracted assets are ever
  committed to either repository.
- **System modules and firmware.** shadPS4 loads some system libraries from `user/sys_modules`;
  they come from your own console as well.
- **Keys of any kind.** None are needed beyond what your own dump contains, and none are accepted
  in contributions.

## Contributing

1. Fork, branch from `d1`, and make one fix per pull request. Mark fork-local code with a `D1` comment
   so it stays easy to find when rebasing onto a newer upstream. Put behaviour that could affect other
   games behind a `D1_*` variable, and add it to the table above.
2. Build it (above) and check the fix in the game: say what you saw before and after, in which scene,
   on which GPU and OS. The pull request template asks for exactly that.
3. CI builds every pull request on Windows, Linux and macOS and checks `clang-format`. A maintainer
   reviews it; merges are squashed, so the `d1` series stays one commit per fix.

Rendering, stability and performance problems that also happen on upstream shadPS4 belong
[upstream](https://github.com/shadps4-emu/shadPS4), and fixes that aren't Destiny-specific are worth
sending there too, following its [CONTRIBUTING.md](../CONTRIBUTING.md) (including its A.I. rules).
Project-wide tasks and research live in
[d1reimagined-source](https://github.com/p3n1cillin/d1reimagined-source/issues). Working with an AI
agent? Point it at [AGENTS.md](../AGENTS.md).
