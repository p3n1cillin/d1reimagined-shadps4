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
| `D1_VIDEO=0` | Disables the D1 56578 Video controls. F10 opens the overlay during gameplay when enabled and the build matches. It applies 30/60/100/144/240/uncapped live and saves 1080p/1440p/4K for the next start. The overlay owns input while open. Choices persist under `D1Video` in `user/config.json`; its writer preserves every other byte. `D1_FPS` overrides the saved rate at boot for diagnostics. Resolution uses the native surface/display allocation patches and experimental wide views described in the source project's resolution note. |
| `D1_NATIVE_VIDEO_DIR=<private-directory>` | Selects the native Settings → Display directory on 56578 when D1 Video is enabled. Without an override, installing the generated directory as `d1-native-video` under the emulator's user directory enables the controls on ordinary launches. Generate it with the source project's `d1-cui-video.py`, using the authored Helmet group for `--template-group` and `--existing-group`, `--group-title Display`, and the decoded English string inputs. This keeps the Helmet setting and adds Frame Rate and Resolution with the same persistence and restart behavior as F10. The loader compares original decoded tags before applying candidate data. Added labels are English. The separate `--graph` mode remains experimental. Keep generated files and original game inputs private. |
| `D1_NATIVE_VIDEO_TRACE=1` | With the native-settings experiment, logs a bounded set of string lookup results and invalid candidate entry addresses. Uses matched 56578 instructions and leaves lookup results unchanged; avoids attaching an exception debugger during menu loading. Off by default. |
| `D1_GUEST_IP=a.b.c.d` | The guest's own IP address. Used only if it belongs to this machine; otherwise a UDP route probe picks it (nothing is sent). |
| `D1_VOLUME_SLICES=0..3` | How a render target over the colour-grading LUT is handled. Default 3 (a fresh 2D image, the fix); 1 and 2 are earlier experiments; 0 is upstream's handling of that case. The other LUT fixes (expanding, extending the predecessor) are always on. |
| `D1_AUDIO_TAP=DIR` | While `DIR/rec.flag` exists, every audio port writes its float samples to `DIR/port<N>-<rate>-<channels>-<unix ms>.f32`. The project's `tools/ps4_setup/d1-record.py` creates the flag, captures the window and muxes the clip. |
| `D1_MOUSE_PROBE=1` | Detours the menu cursor update (56578, 0x985D00) and logs the UI root and cursor position at the first hits; changes nothing. Proves the detour framework (`InstallD1Detour`). |
| `D1_MOUSE_BUTTONS=1` | Mouse buttons merged into player 0's controls after pad selection (56578, 0x9284EF): in menus left = cross, right = circle; in gameplay left = R2 (fire), right = L2 (aim), middle = R3, Mouse4 = square, Mouse5 = circle. The pad keeps working. |
| `D1_KBM=1` | The game's own PC keyboard layer (56578, builder 0x926965): the 104 keys are written in the engine's order to the key table after the mouse state, and the layer is enabled, so the engine reads the keyboard with its own bindings. Seen in a Moon match: W walked the Guardian forward with W unmapped in the pad config (10 m in 2 s, the same as the stick); R switched to the special weapon. Keys the pad config also maps act twice. |
| `D1_KBM_MAP=R=E,1=R` | Rebinds the keyboard layer: each `HOST=ENGINE` pair makes the host key press that engine key instead of its own (key names as in `EngineKeyNames`, any case; a host key may be named more than once). The engine's own keys are a developer layout, read from its KBM builder (0x926910): Space = cross (jump), Q = circle (crouch), E = square (reload / interact), R = triangle (weapon), T / G / X / C = d-pad up / down / left / right (wave, sit and dance seen), End = L2 (aim), Ins = R2 (fire), PgDn = Options (character menu), PgUp = touchpad (Ghost menu), B = R3, WASD move, arrows look. Z, F, V and U J K N M showed nothing when tapped or held (no sprint, grenade, melee or super found yet). So `R=E,E=E` puts reload on R and keeps interact on E. |
| `D1_MOUSE_LOOK=1` | Also mouse look in gameplay: the PC mouse's movement is added to player 0's look rates at the 56578 look site (0x91C7DC), scaled by the frame time, so a hand movement turns the camera by the same angle at any frame rate. It reads raw input (SDL relative mode, Windows raw input: no pointer acceleration), switched on while no menu cursor is up and the window has focus. `D1_MOUSE_LOOK_SENS`, `D1_MOUSE_ADS_SENS` (while aiming) and `D1_MOUSE_INVERT_Y=1` tune it; `D1_MOUSE_RAW=0` keeps the earlier route that re-centres the system cursor. No reticle friction is added for the mouse ([PC-CONTROLS.md](PC-CONTROLS.md)). |
| `D1_MOUSE_CURSOR=1` | The menu cursor follows the host mouse while the game window has focus (Windows). `D1_MOUSE_SENS` scales it (default 1, the engine's own unscaled rate). |
| `D1_FLIP_RATE=N` | Replaces the flip rate the title requests (Destiny asks for 1: every second vblank, 30 Hz). `0` flips on every vblank, for the 60 Hz engine-clock patch. Unset keeps the title's rate. |
| `D1_RATE_RULES=0` | Disables the build-56578 timing detours. With the rules enabled, weapons with a whole-tick 30 Hz shot interval can distribute rounding across a burst at fractional tick rates. |
| `D1_DUMP_DIR=DIR` | Creating `DIR.flag` dumps every texture sampled in the next ~0.2 s (raw, linear, in draw order) plus `events.txt`. |
| `D1_TEX_SURVEY=1` | Logs each shader's distinct texture shapes and the render targets each pixel shader writes, once. |
| `D1_TRACE_IMAGE=<hex address>` | Traces the texture cache's lookups, creation, expansion and freeing at that address. |
| `D1_META_RUN=1` | Still runs a compute dispatch over memory once registered as HTILE (diagnostic). |
| `D1_DRAW_FAST=0` | Restores upstream's draw path. By default (on), a graphics pipeline, vertex input layout, vertex buffer or index buffer bind that repeats what the current Vulkan command buffer already has is not recorded again, the graphics pipeline lookup compares the key with the last one before hashing it, and the scheduler doesn't query the GPU timeline before a draw when no deferred operation can be due. Each is exact by the Vulkan rules (the remembered state is forgotten on every new command buffer and after the blit helper binds its own pipelines); see "Measuring the draw path". |
| `D1_GPU_WAIT_YIELD=1` | Experimental, off by default. A guest wait on the GPU command processor (WAIT_REG_MEM, a memory semaphore, a rewind, the CE/DE counters) that has spun 256 times yields the thread's time slice on each further spin, so a long wait leaves the core to the game's threads. It may also delay noticing the wait's end, so check it with `D1_DRAW_STATS` ("guest wait spins") and the frame rate before using it. |
| `D1_DRAW_STATS=1` | Logs the draw path's CPU cost once a second (see "Measuring the draw path"). Its timers add about 0.1 us to each draw, so compare runs with the same setting. |
| `D1_PC_LAYOUT=1` | The PC keyboard-and-mouse layout ([PC-CONTROLS.md](PC-CONTROLS.md)): Destiny 2's PC keys where D1 has the action, pressing the pad buttons the game reads (`user/input_config/d1-pc.ini`, written on first use, remappable, F8 reloads), plus `D1_MOUSE_CURSOR`, `D1_MOUSE_BUTTONS` and `D1_MOUSE_LOOK`, Esc (back in menus, the character screen in play) and `D1_PAD_KEEP_KEYBOARD`. |
| `D1_PAD_KEEP_KEYBOARD=1` | When player 1's controller is unplugged, its slot stays connected and driven by the keyboard, as at a start with no controller, instead of disconnecting. Default with `D1_PC_LAYOUT=1`; `0` keeps upstream's disconnect. |
| `D1_AUDIO_SYNC=1` | Audio output paced by the host device, as the console's hardware paces `sceAudioOutOutput`, instead of a wall-clock schedule: the stream holds about two game buffers (43 ms), so the clocks can't drift into a cleared backlog (a drop) or an empty queue (crackle), and the backlog limit counts the downmixed stream's own bytes (upstream compared 8-channel bytes with the stereo stream, allowing up to 341 ms of audio lag for Destiny). |
| `D1_EXCEPTION_HANDLERS=0` | Keeps `sceKernelInstallExceptionHandler` / `sceKernelRemoveExceptionHandler` unresolved for titles that import them from `libkernel_exception`. By default they are registered there (and `_sigaction` in libkernel): Destiny's fault handlers are installed, and its fatal-error path removes its SIGILL handler and ends on ud2 as on the console, rather than calling the stubs. |
| `D1_CLEAN_EXIT=0` | Default for CUSA00219 (`1` forces it for others): an unhandled guest exception, including the game's own fatal-error ud2 (logged as such), ends the process with the exception code after the log is flushed, instead of passing it to Windows error reporting. |
| `D1_HITCH_HALT=0` | The game's hitch detector (thread BLOCK_DETECT, 0x92A3D0) halts it with RELEASE_VHALT when a watched job stalls past its limit. That never happens on a console, but on a loaded PC it can. With `0` it takes its report-only branch instead and the game carries on with its own recovery (cine-live10: a 53 s stall under disk load, then the halt during the repair). `1` only logs each stall. Unset: no detour, the game halts as on the console. |

## Measuring the draw path

Destiny is draw-heavy. Bungie's renderer talks (Tatarchuk, GDC 2015 and 2017; SIGGRAPH 2013 and 2014;
Genova, GDC 2015) describe many small command buffers per view and render stage, 3,100-9,000 shader
techniques a frame, each gear piece drawn about five times, and the same state re-bound at the start
of each command buffer. The emulator turns every one of those draws into Vulkan commands on a single
thread, the GPU command processor. When its CPU cost per draw times the draws in a frame exceeds the
frame time, the frame rate falls while the host GPU waits.

`D1_DRAW_STATS=1` logs one line a second (`Lib_VideoOut`, info, like `D1_FPS`, so a filter such as
`*:Info Render_Vulkan:Error` still shows it):

```
D1 draw stats: 182000 draws/s (0 indirect), 2400 dispatches/s, 120 submits/s, 60 guest submits/s
with 3500 command buffers/s; 2.81 us a call (pipeline 0.92, resources 1.20, record 0.69), slowest
450 us; GPU thread 52% in calls; skipped binds: ...; guest wait spins 0/s (0 yields)
```

(The numbers above show the format, not a measurement.)

- **GPU thread N% in calls**: the share of wall time the command processor spends inside draws and
  dispatches. Near 100%, the draw path limits the frame rate, and frame time is about draws a frame
  times the cost of a call.
- **The phases**: *pipeline* builds the pipeline key, picks each shader's permutation and finds the
  pipeline (a first-time compile shows up as the slowest call); *resources* finds render targets,
  buffers, textures and samplers in the caches; *record* covers barriers, the rendering scope,
  vertex and index buffers, descriptors, dynamic state and the draw itself.
- **Command buffers/s** counts the guest's indirect buffers; **guest wait spins** count how often a
  guest wait (WAIT_REG_MEM, a memory semaphore, a rewind, the CE/DE counters) was found
  unsatisfied, each a spin of the GPU thread.
- **Skipped binds** and **pipeline memo** say how often `D1_DRAW_FAST` found the state already
  bound (0% with `D1_DRAW_FAST=0`).

### What D1_DRAW_FAST saves, and the A/B in the game

Per draw it can save one `vkCmdBindPipeline`, one `vkCmdSetVertexInputEXT`, a vertex and an index
buffer bind (each only when it repeats the command buffer's state), one
`vkGetSemaphoreCounterValue` (whenever no deferred operation can be due), and the hash and map
probe of a ~380-byte pipeline key on a memo hit. The bench in `tests/test_d1_draw_perf.cpp`
(`--bench`) puts the filters at about 1-12 ns each and the lookup at 68 ns against 31 ns behind the
memo, with runs of eight draws per pipeline. What the driver no longer records is worth more:
about 0.1-0.4 us a draw is the estimate. If a draw costs 2-4 us on the command processor (the stats
give the real figure), that is roughly 5-15% of its time. These are estimates from the code and the
bench, not game measurements. The frame
rate only rises where the command processor is the limit (**GPU thread** near 100%).

The native test, on the same build:

1. Same scene and setting both times: for example a fixed checkpoint of A Guardian Rises or the
   Tower plaza, standing still for 60 s after the load settles, with `D1_FPS` fixed (60) or
   uncapped (`D1_FPS=0`), and nothing heavy running on the machine.
2. Run with `D1_DRAW_STATS=1 D1_DRAW_FAST=0`, then with `D1_DRAW_STATS=1` only.
3. Compare the medians of the stats lines (**us a call**, its **record** phase, **GPU thread %**)
   with the frame rate: the emulator's FPS counter, `D1_FPS`'s "uncapped: N frames/s" lines, and
   its "ran at N ticks/s" lines (fewer of them, or a higher held rate, at the same setting).
4. Look for rendering differences between the two runs (none are expected; one would mean a
   bind the filters should not have skipped, and `D1_DRAW_FAST=0` is the workaround).

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

## Credits

Everything outside the `d1` commits is the work of the [shadPS4 Emulator Project](https://github.com/shadps4-emu/shadPS4)
and its contributors, including shadNet, its PSN replacement, which grew out of the rpcsn Project's
RPCN. This build only adds the Destiny-specific fixes listed above. The services and research it
runs against build on aknumbers' [Destiny 1 Preservation Project](https://gitlab.com/aknumbers).
Everyone whose work D1 Reimagined uses is named in
[CREDITS.md](https://github.com/p3n1cillin/d1reimagined-source/blob/main/CREDITS.md).

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
