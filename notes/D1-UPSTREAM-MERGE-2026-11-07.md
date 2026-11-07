<!--
SPDX-FileCopyrightText: 2026 D1 Reimagined contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

# D1 upstream integration, 7 November 2026

This is a merge of upstream `50c5c1b4e86e275b2e9df9c33606a934b037684c` into
`d1-upstream-1107`, starting at fork `95e44fe9a627a3e2f03fb5ced89e0049687e9239`.
The common ancestor is `c7e065d` (v0.19.0). All 230 upstream commits and all 46 fork
commits remain in the ancestry. Upstream is now 0.21.1 WIP. This integration is one
merge commit, as requested; it does not rebase or replace the existing D1 series.

**No local build, translation-unit compilation, test executable, emulator, server
probe or game run was performed.** agr31 was active; compile and native A/B testing
belong between lab runs. No deployed emulator, other worktree or running process
was changed. No game material or generated native Display assets are included.

## Conflict resolutions

| File | Resolution |
|---|---|
| `AGENTS.md` (add/add) | Retain the fork's contributor rules, D1 markers, environment gates, AI disclosure and prohibition on unsolicited emulator runs. Update the baseline wording. Upstream's rebase/build requirements conflict with this explicitly authorized merge without a build; its upstream contribution policy still applies to PRs sent upstream. |
| `src/video_core/amdgpu/liverpool.cpp` (4 hunks) | Use upstream's corrected CE-minus-DE comparison and globally scheduled CE task; retain `D1_GPU_WAIT_YIELD` and wait statistics inside the corrected loop. Keep `D1_TRACE_COVER` with the new one-argument `ProcessGraphics`. Count chained indirect buffers before upstream's chain replacement, and keep its nested-task destruction. Keep submit statistics with upstream's new CE and graphics submission locking. |
| `src/video_core/buffer_cache/buffer_cache.cpp` (1 hunk) | Keep `D1_META_RUN` around upstream's `GetMetaInfo` path. Retain upstream's HTILE fill and new sample-count-aware CMask expansion; no duplicate synchronization path is restored. |
| `src/video_core/renderer_vulkan/vk_rasterizer.cpp` (4 hunks) | Keep upstream's clamped index capacity, null index-buffer fallback, maintenance5 bounded binding and buffer-access tracking. Apply D1 redundant-bind filtering only after tracking resources, comparing buffer, offset, type **and size**. Retain dump/survey diagnostics with upstream's fixed image-binding array (remove the obsolete vector `clear`). Use the new depth descriptor fields and keep `has_attachment` for upstream's render-area calculation. |
| `src/video_core/texture_cache/texture_cache.cpp` (2 hunks) | Combine includes. Retain LUT predecessor selection, array expansion, 3D/2D alias modes, type-reuse handling and diagnostics. Use upstream's component-wise `Contains` for resource sufficiency, including the D1 alias search. Preserve upstream's CPU-dirty/GPU-valid-data decision in `ExpandImage`, initialization of new image data and intrusive LRU operations. |
| `src/core/libraries/network/posix_sockets.cpp` (modify/delete) | Accept deletion. The replacement HLE gathers/scatters messages and the host layer uses ordinary Winsock `send`/`recv`, covering D1's Windows TCP fix. Port diagnostics to `src/core/libraries/net/net.cpp`; do not recompile the obsolete socket implementation. |
| `src/core/libraries/np/np_handler.cpp` (modify/delete) | Accept deletion after moving D1's startup retry to `src/core/libraries/np/np_handler/np_handler.cpp`. Keep upstream's split handler and lifecycle changes. |

The range in `D1DrawPerf::IndexBufferBind` is a necessary integration adjustment:
upstream can bind the same buffer/offset/type with a different maintenance5 size.
Skipping that bind would leave the old range active. The existing draw-filter test
source now checks a changed range; it was **not compiled or run**.

The D1 format wrapper keeps changed-line checking, uses upstream's installed
`clang-format-23` and matching `git-clang-format-23`, and pins the push-comparison
baseline to `50c5c1b`. `d1/README.md` records the new baseline and previously
undocumented network and coverage diagnostics. Two imported trailing-whitespace
lines in `stack_asm.cpp` and vendored `dr_flac.h` were trimmed so the merge diff
passes `git diff --check`.

## Networking preservation and local routes

| D1 change | Current implementation / evidence |
|---|---|
| `4e746a9`: real guest address | `src/core/libraries/net/net_util.cpp`, renamed from `network/net_util.cpp`, retains validated `D1_GUEST_IP` and the packet-free UDP route probe. `network/netctl.cpp::sceNetCtlGetInfo` retains an empty address and NOT_AVAIL on failure rather than fabricated loopback success. |
| `4e746a9`: TCP multi-buffer sends and receives on Windows | Covered by upstream `net/net.cpp::sceNetSendmsgImpl` / `sceNetRecvmsgImpl`: validate iovecs, gather once, send, receive once and scatter the returned bytes. The path continues through `sceNetSendto` / `sceNetRecvfrom`, `core/net/guest_net.cpp::SocketSendTo` / `SocketRecvFrom`, and `core/net/host_net.cpp::SendTo` / `RecvFrom`, where Windows uses `send` / `recv` for a connected socket. No guest-msghdr-to-WSAMSG reinterpretation remains. |
| `4e746a9`: errors and partial results | The replacement returns typed `NetResult` errors, translated by `net/net.cpp::Return` / `SetErrno`, rather than treating positive WSA error numbers as byte counts. `SendAll`, `ReceiveAll` and `RunBlocking` cover partial transfers, nonblocking operation, timeouts and WAITALL. |
| `c446ce1`: startup retries | `np/np_handler/np_handler.cpp::Initialize` now makes up to five probe attempts, retrying only `ProbeResult::Unreachable`. `src/shadnet/server_probe.h` keeps the 10,000 ms timeout argument per attempt; `server_probe.cpp` still marks a missing ServerInfo header Unreachable. Version mismatch and malformed protocol data remain terminal. This is the existing timeout behavior, not a new strict wall-clock cap across DNS/connect/header/payload phases. |
| `f28bba7`, `1c12c6b`: socket diagnostics | `net/net.cpp::NetDiagNote`, called from the receive/send wrappers, logs receive calls/bytes/empty calls, send calls/bytes and queued bytes after 2 s of socket activity. Nested sendmsg/recvmsg paths count once. Keys are now **guest socket IDs**, and successful HLE socket close clears that ID's counters. `SocketBytesReadable` provides queue length through the new core; RAII preserves WSAGetLastError/errno. It does not invoke HLE error translation, so the already-set guest errno is unchanged. Disabled unless `D1_NET_DIAG=1`. |

`d1/config.d1.json` still selects `127.0.0.1:31313` and
`http://127.0.0.1:31315`, enables networking and disables UPnP. shadNet's configured
server parsing and web API configuration remain present. Normal IPv4 connections
in the new HLE pass the supplied destination through address translation into
`SocketConnect`; neither a loopback shadNet endpoint nor a lab-IP BAP/activity
endpoint is redirected or filtered by this merge. The guest's own address is
separate from those server destinations. No lab address is hard-coded.

This is source-level coverage, **not proof of interoperability**. The shadNet wire
version remains 1 on both branches and the login send code is unchanged; upstream
adds protocol messages and rewrites NP authentication. The installed local server
must pass the version handshake and a real login/world/activity session in the A/B.

## Other D1 features and where they live

Historical timing commits refine or replace earlier experiments. Preserve the
effective behavior at `95e44fe`, including leaving time scale alone; retaining an
obsolete intermediate behavior would undo a later D1 fix.

| Feature / originating changes | Current home |
|---|---|
| Unreadable guest-address JIT guard (`4935823`) | `core/cpu_patches.cpp::PatchesAccessViolationHandler`, alongside upstream's new static/JIT patch work. |
| Single-kind NaN classification (`01e87c2`) | `shader_recompiler/frontend/translate/vector_alu.cpp::V_CMP_CLASS_F32`; QuietNan and SignalingNan individually use `FPIsNan`. Upstream's broader NaN fixes do not replace this case. |
| Colour-grading LUT and render diagnostics (`d93d64a`) | `texture_cache/texture_cache.cpp` predecessor growth, expansion and `D1_VOLUME_SLICES` modes; `texture_cache/image.cpp` volume barriers; rasterizer dump, texture/target survey and metadata diagnostics; buffer-cache `D1_META_RUN`. |
| Game-audio recording (`0381fe4`) | `core/libraries/audio/sdl_audio_out.cpp::D1Tap`, including channel-count-aware filenames and cleanup. |
| Flip-rate override (`7753a0b`) | `core/libraries/videoout/video_out.cpp`; presenter timing callbacks remain in `videoout/driver.cpp`. |
| Pinned detours and mouse probe/cursor (`d6b56c1`, `9c3ba8b`, `29d69a4`, `706a785`) | `core/cpu_patches.{cpp,h}` detour framework; `core/d1_native_mouse.{cpp,h}` and installation from `common/memory_patcher.cpp`. |
| Mouse look/buttons, KBM and remapping (`c20ead4`, `930b845`, `978180a`, `08607c5`, `34c579f`) | `core/d1_native_mouse.cpp`, `core/d1_mouse_look.h`, SDL raw-motion hook, and `d1/README.md` / `d1/PC-CONTROLS.md`. |
| Fixed/uncapped engine clock and guard (`a62f5ff` through `ccca7a1`, timing series in appendix) | `core/d1_frame_rate.{cpp,h}` and `videoout/driver.cpp`: live settings, observed-rate guard with 30 Hz floor, stable uncapped probing, unchanged time scale. |
| Jump and fractional rifle timing (`394ba5e`, `e2680e7`, `5991799`, `fd7af2e`, consolidated by `82c9ce2`) | `core/d1_frame_rate.cpp`, `d1_jump_timing.h` and `d1_weapon_timing.h`. |
| Experimental wider/taller native views (`b194bd3`, `edca804`) | `core/d1_frame_rate.cpp` surface/view detours and `core/d1_video.cpp` resolution selection. |
| Width/coverage tracing (`6bab075`, `26fada0`, `0b3eacd`) | Texture-cache creation trace, rasterizer viewport/scissor/dispatch trace, Liverpool register-packet trace. |
| F10 and native Settings > Display (`82c9ce2`) | `core/d1_video.{cpp,h}`, `d1_video_config.h`, `d1_native_video.{cpp,h}`, `d1_tag_override.h`; SDL/ImGui/pad capture hooks and CMake sources remain. Native definition loading still compares original decoded tags and uses the configured private directory or `user/d1-native-video`. No definitions or game data are embedded by the merge. |
| Draw CPU cost and wait controls (`2af7828`) | `renderer_vulkan/d1_draw_perf.{cpp,h}`, pipeline memo, rasterizer bind filters, scheduler and Liverpool counters/yield. Upstream's empty-pending-queue fast path coexists with D1's not-yet-due timeline check. |
| PC layout and controller fallback (`95e44fe`) | `input/d1_pc_layout.h`, `input/input_handler.cpp`, `input/controller.cpp`, `core/d1_native_mouse.cpp`; raw mouse look and `D1_PAD_KEEP_KEYBOARD` retained alongside upstream's physical-pad smoothing fixes. |
| Fatal path and hitch control (`95e44fe`) | `core/d1_stability.{cpp,h}`, detour install hook, `core/signals.cpp` clean exit and `kernel/threads/exception.cpp` library registrations and `_sigaction` alias. |
| Host-device audio pacing (`95e44fe`) | `audio/d1_audio_sync.h`, `audio/sdl_audio_out.cpp` device pacing / downmixed queue accounting, and `audio/audioout.cpp` integration. |
| Fork maintenance (`05caefb`, `1a6dd17`, `7f2cda5`, `b3ff6ed`, `6fcb13b`, `37d6b4f`, `d980310`, `4546b26`) | `d1/` build/config/docs, issue forms, README badge/credits, PR template, AGENTS, REUSE and changed-line formatting wrapper. Upstream's source/dependency additions remain intact. |

## Upstream changes relevant to Destiny

These are candidates based on the changed code and commit descriptions. No
upstream commit is established here as the cause or cure of F47 (solid-black gear).

### F47: shader values, resource contents and synchronization

| Upstream commits | Why they merit the gear-rendering A/B |
|---|---|
| `c495290` (#5257), `1c2b8f9` (#5346) | NaN-aware legacy arithmetic, unordered comparisons, three-input min/max/median, DX10 clamp and reciprocal/log clamping can change material math. D1's single-kind NaN classification remains independently necessary. |
| `34a1d4d` (#5134) | V_MOVREL lowering uses SSA-derived dynamic register-array bounds. Relevant if a material's indexed register accesses were lowered incorrectly. |
| `0951d80` (#5620), `6999be1` (#5535), `b2ad718` (#5622) | Preserve F32x2 SSA/phi/undefined values, decode the second destination correctly and correct 64-bit least-significant-bit upper-word indices. Possible shader-value corruption fixes. |
| `c34ca21` (#5220), `78bedfa` (#5365), `524be7c` (#5427), `603cff4` (#5364) | EXEC scope, CFG fallthrough and phi handling affect conditional material paths and user-data values. |
| `945dbc3` (#5285), `08225c2` (#5429), `6752d8b` (#5447) | Shared-memory/subgroup barriers, singleton workgroup dimensions and buffer-cache invalidation instructions can affect compute-produced inputs. |
| `699009c` (#5444), `d3ff0e0` (#5433), `c679ce6` (#5229), `eedf851` (#5450), `4f1a70f` (#5504), `6add101` (#5599) | Avoid stale CPU-dirty image copies, preserve initial subresources, initialize added depth overlaps, detect guest overwrites at untracked image edges, reset refresh hashes and invalidate adjacent GPU-written images. Directly relevant to missing/stale sampled material data. |
| `b3d46e9` (#5368), `ee0364a` (#5453), `e792917` (#5307), `ca36c7e` (#5524) | Copy all mips, clamp mip count, support larger tile tables and compare extents per dimension. Worth checking if affected gear depends on a mip/subresource layout. |
| `c09fa46` (#5262), `43e4c47` (#5231), `995e37e` (#5046), `14ada48` (#5283) | Track vertex/index/indirect accesses, cover fragment texture reads/presentation waits, read CMask metadata and apply fast clears during FMASK decompression. Possible visibility/content ordering fixes. |
| `7721a1e` (#5260), `643c799` (#5348), `10393d2` (#5274), `0027696` (#5278), `23c4133` (#5358) | Instance/vertex IDs, step rates and indirect index capacity affect geometry inputs. These are candidates if wrong input selection makes material or gear data invalid. |
| `e6c79a5` (#5406), `f9d0748` (#5446), `1b9d159` (#5542), `7ea677e` (#5541) | Correct CE/DE synchronization, global CE task scheduling, chained indirect buffers and packet advancement. Stale constant data or commands could affect rendering or hangs. |

`b61f6c5` (#5455) adds optional fast shader math. Keep it at the existing/default
setting for the initial comparison so it does not confound a correctness result.
No occlusion-query patch from the separate F51 worktree is included.

### Performance and stability

- `8e23388` (#5219), renderer optimizations pt2: per-image locking, smaller overlap
  lookup working sets, fixed image bindings, precomputed BCn layouts and intrusive
  LRU bookkeeping. These complement the D1 per-draw filters; no measured Destiny
  speedup is claimed. `2831b68` (#5591) also avoids empty pending-operation work.
- `914e78c` (#5571) waits once for pending image downloads; `3b53747` (#5570) fixes
  LRU early stopping. `8a66ada` (#5572) adds configurable periodic submission;
  retain the existing setting for the first A/B.
- `0614ce3` (#5382) moves Windows fiber stacks out of the GPU-tracked heap into
  the thread stack allocator and frees abandoned fiber stacks. This is the
  requested Windows fiber-stack crash fix; it is upstream's SotC fix, not yet a
  demonstrated Destiny crash cure.
- `12c5978` (#5258) frees nested indirect-buffer task frames. `5674bf7` (#5462)
  fixes over-unmapping; `0c9ca52` (#5317) restores Windows protections after a VMA
  split; `ad5dfc0` (#5464) handles JIT patch addresses outside modules.
- `e5859ce` (#5569), `d1d0b58` (#5564), `db79f9d` (#5559) fix shutdown re-entry,
  backup-thread shutdown and uninitialized ELF information. D1 fatal-path handling
  is retained on top. `a5441d6` (#5635) moves guest initialization off the UI thread.

### Audio, pad and networking

- AJM: `beda45d` (#5254) fixes MP3 parser out-of-bounds access and Xing/VBRI;
  `0d5f273` (#5645) recreates the MP3 parser on reset; `d615b38` (#5430) safely
  detects/parses ATRAC9 RIFF headers; `4dc007a` (#5644) ignores AAC reset before
  initialization. These address decoder parsing/reset stability; D1 device pacing
  and tap remain separate. `bab374b` (#5499) and `396c2a0` (#5501) correct audio3d
  attribute IDs in both backends.
- Pad: `2c1961a` (#5340) and `c20cc33` (#5377) stop digital-input smoothing from
  affecting real analog input and track its origin; `8d50471` (#5347) implements
  extended pad reads; `6bb678b` (#5247) corrects touchpad height. PC layout, raw
  look and keyboard fallback remain enabled by their existing D1 controls.
- Network: `2a2f100` (#5236) replaces the socket stack; `0fe263a` (#5306) follows
  with legacy transfer corrections, `124328b` (#5192) adds kernel event queues,
  `e34a595` (#5610) integrates socket fcntl/dup/dup2, `f68923f` (#5437) reports
  receive queue length, and `98ef621` (#5638) filters redundant netctl events and
  supports callback unregister. `81be949` (#5333) splits NpHandler, while
  `7f6b94a` (#5390) rewrites NP authentication. Local compatibility must be tested.

## Validation performed and next lab pass

- Read both branch diffs and per-file D1 history; traced the rewritten TCP,
  address selection, errno, shadNet probe and web API configuration paths.
- Confirmed 47 D1-modified files untouched by upstream and deliberate integration
  edits are identical to the pre-merge fork, including native Display/control
  implementations and the remaining existing standalone D1 test sources.
- Checked project includes in D1 source additions/changes and scanned source for
  conflict markers. Applied clang-format 23.1.3 to D1 ranges in manually resolved
  sources and the adjusted bind-filter test. `git diff --check HEAD` passes for the
  complete merge diff. Comparing against upstream alone also reports the existing
  D1 test-runner blank line at EOF; it was not introduced by this integration.
  These checks do not substitute for a compiler or runtime test.
- Between runs: update required upstream submodules, build this committed branch,
  and use the same private native Display directory, config and D1 environment.
  First establish shadNet version/login, BAP and activity-host traffic; then reach
  a world and sustain a session with `D1_NET_DIAG=1`. Compare error/queue behavior.
- A/B the same gear/scene for F47, LUT grading and missing geometry; compare draw
  statistics at the same frame-rate setting, including `D1_DRAW_FAST=0` as the
  existing diagnostic control. Check 30/60/uncapped guard, jump/rifle timing,
  Display persistence/restart resolution, PC input, unplug fallback, and audio
  crackle/drift. Check the Windows fiber fix during repeated world transitions.
  Native results and build success remain pending maintainer testing.

## Complete fork commit inventory

The following appendix maps every commit in `c7e065d..95e44fe` to its current file
locations. Intermediate revisions share their final feature implementation above.
Deleted legacy network files map to their replacement and/or upstream coverage.

| Commit and change | Current files |
|---|---|
| `4e746a9` net: a real guest address, and TCP sendmsg/recvmsg on Windows | `src/core/libraries/net/net_util.cpp`, `src/core/libraries/network/netctl.cpp`, `src/core/libraries/net/net.cpp (diagnostics; upstream covers TCP gather/scatter)` |
| `c446ce1` shadnet: retry the startup probe and allow 10 s per attempt | `src/core/libraries/np/np_handler/np_handler.cpp`, `src/shadnet/server_probe.cpp`, `src/shadnet/server_probe.h` |
| `4935823` cpu_patches: do not JIT-decode an unreadable guest address | `src/core/cpu_patches.cpp` |
| `01e87c2` shader: V_CMP_CLASS_F32 with a single NaN kind | `src/shader_recompiler/frontend/translate/vector_alu.cpp` |
| `d93d64a` texture cache: Destiny's colour-grading LUT keeps all its slices | `src/video_core/buffer_cache/buffer_cache.cpp`, `src/video_core/renderer_vulkan/vk_rasterizer.cpp`, `src/video_core/texture_cache/image.cpp`, `src/video_core/texture_cache/texture_cache.cpp`, `src/video_core/texture_cache/texture_cache.h` |
| `0381fe4` audio: D1_AUDIO_TAP writes the game's own audio for recordings | `src/core/libraries/audio/sdl_audio_out.cpp` |
| `05caefb` d1: build script, settings and environment variables for D1 Reimagined | `README.md`, `REUSE.toml`, `d1/Build-D1ShadPS4.cmd`, `d1/README.md`, `d1/config.d1.json` |
| `1a6dd17` d1: point issue forms at D1 Reimagined | `.github/ISSUE_TEMPLATE/app-bug-report.yaml`, `.github/ISSUE_TEMPLATE/build-problem.yml`, `.github/ISSUE_TEMPLATE/config.yml`, `.github/ISSUE_TEMPLATE/feature-request.yaml`, `.github/ISSUE_TEMPLATE/game-bug-report.yaml`, `.github/ISSUE_TEMPLATE/rfc.yaml` |
| `7f2cda5` README: live D1 Reimagined progress badge and watch links (#1) | `README.md` |
| `b3ff6ed` d1: clang-format the lines the D1 series changed | `src/core/cpu_patches.cpp`, `src/core/libraries/audio/sdl_audio_out.cpp`, `src/core/libraries/net/net_util.cpp`, `src/core/libraries/net/net.cpp (diagnostics; upstream covers TCP gather/scatter)`, `src/video_core/renderer_vulkan/vk_rasterizer.cpp`, `src/video_core/texture_cache/texture_cache.cpp` |
| `6fcb13b` ci: clang-format checks the D1 series, not upstream's whole tree | `.github/workflows/scripts/clang-format.sh` |
| `37d6b4f` d1: contributor guide, pull request template and AGENTS.md | `.github/pull_request_template.md`, `AGENTS.md`, `d1/README.md` |
| `d980310` d1: licence headers for AGENTS.md and the pull request template | `.github/pull_request_template.md`, `AGENTS.md` |
| `7753a0b` d1: D1_FLIP_RATE overrides the title's requested flip rate | `d1/README.md`, `src/core/libraries/videoout/video_out.cpp` |
| `d6b56c1` d1: detours at pinned guest spans, and a mouse-cursor probe | `CMakeLists.txt`, `src/common/memory_patcher.cpp`, `src/core/cpu_patches.cpp`, `src/core/cpu_patches.h`, `src/core/d1_native_mouse.cpp`, `src/core/d1_native_mouse.h` |
| `9c3ba8b` d1: D1_MOUSE_CURSOR moves the menu cursor with the host mouse | `src/core/d1_native_mouse.cpp` |
| `29d69a4` d1: add the D1_MOUSE_CURSOR implementation the previous commit described | `src/core/d1_native_mouse.cpp` |
| `706a785` d1: document D1_MOUSE_PROBE, D1_MOUSE_CURSOR and D1_MOUSE_SENS | `d1/README.md` |
| `c20ead4` d1: D1_MOUSE_LOOK turns the camera with the PC mouse in gameplay | `d1/README.md`, `src/core/d1_native_mouse.cpp` |
| `930b845` d1: D1_MOUSE_BUTTONS merges mouse buttons into player 0's controls | `d1/README.md`, `src/core/d1_native_mouse.cpp` |
| `978180a` d1: D1_KBM feeds the engine's own PC keyboard layer | `d1/README.md`, `src/core/d1_native_mouse.cpp` |
| `08607c5` d1: D1_KBM_MAP rebinds the engine's keyboard layer | `d1/README.md`, `src/core/d1_native_mouse.cpp` |
| `34c579f` d1: README records the engine keys seen live (PgDn Options, PgUp touchpad, C dance) | `d1/README.md` |
| `4546b26` d1: README credits shadPS4, shadNet and aknumbers' Destiny 1 Preservation Project | `d1/README.md` |
| `f28bba7` d1: D1_NET_DIAG logs each socket's receive and send activity every 2 s | `src/core/libraries/net/net.cpp (diagnostics; upstream covers TCP gather/scatter)` |
| `1c12c6b` d1: D1_NET_DIAG keeps the socket error code its callers read | `src/core/libraries/net/net.cpp (diagnostics; upstream covers TCP gather/scatter)` |
| `a62f5ff` d1: D1_FPS holds Destiny's frame rate at a setting, guarded to real time | `CMakeLists.txt`, `src/common/memory_patcher.cpp`, `src/core/d1_frame_rate.cpp`, `src/core/d1_frame_rate.h`, `src/core/libraries/videoout/driver.cpp` |
| `bdda63a` d1: D1_FPS=0 follows the presented frame rate; the tick limit is CPU | `src/core/d1_frame_rate.cpp`, `src/core/d1_frame_rate.h`, `src/core/libraries/videoout/driver.cpp` |
| `57d5632` d1: uncapped stops flapping at the PC's limit | `src/core/d1_frame_rate.cpp` |
| `264b2e4` d1: D1_FPS writes the clock's ticks a second and the time scale | `src/core/d1_frame_rate.cpp` |
| `2f69d66` d1: D1_FPS leaves the time scale alone | `src/core/d1_frame_rate.cpp` |
| `394ba5e` d1: the jump's hold boost follows the clock's ticks a second | `src/core/d1_frame_rate.cpp` |
| `e2680e7` d1: the jump's hold count, buffer and curve sampling follow the rate | `src/core/d1_frame_rate.cpp` |
| `5991799` d1: the jump curve's 30 Hz sample is the tick the moment starts in | `src/core/d1_frame_rate.cpp` |
| `fd7af2e` d1: the airborne step's 4-tick ground window follows the rate | `src/core/d1_frame_rate.cpp` |
| `6bab075` d1: D1_TRACE_WIDTH logs new images of a given width | `src/video_core/texture_cache/texture_cache.cpp` |
| `26fada0` d1: D1_TRACE_COVER logs passes whose coverage ends at a given x | `src/video_core/renderer_vulkan/vk_rasterizer.cpp` |
| `0b3eacd` d1: D1_TRACE_COVER also logs the command words that set the coverage | `src/video_core/amdgpu/liverpool.cpp` |
| `b194bd3` d1: experimental: 2048-wide view objects follow a wider render width | `src/core/d1_frame_rate.cpp` |
| `edca804` d1: wide views: a 2048-high view follows a taller render height too | `src/core/d1_frame_rate.cpp` |
| `ea4db7a` d1: D1_FPS's guard can go down to 30 | `src/core/d1_frame_rate.cpp` |
| `943694b` d1: uncapped doesn't re-probe a rate the guard came down from | `src/core/d1_frame_rate.cpp` |
| `ccca7a1` d1: uncapped's probe limit is what the engine achieved | `src/core/d1_frame_rate.cpp` |
| `82c9ce2` d1: in-game video settings, jump and rifle timing at any rate (#6) | `CMakeLists.txt`, `d1/README.md`, `src/core/cpu_patches.cpp`, `src/core/cpu_patches.h`, `src/core/d1_frame_rate.cpp`, `src/core/d1_frame_rate.h`, `src/core/d1_jump_timing.h`, `src/core/d1_native_mouse.cpp`, `src/core/d1_native_video.cpp`, `src/core/d1_native_video.h`, `src/core/d1_tag_override.h`, `src/core/d1_video.cpp`, `src/core/d1_video.h`, `src/core/d1_video_config.h`, `src/core/d1_weapon_timing.h`, `src/core/libraries/pad/pad.cpp`, `src/imgui/renderer/imgui_core.cpp`, `src/sdl_window.cpp`, `tests/test_d1_jump_timing.cpp`, `tests/test_d1_tag_override.cpp`, `tests/test_d1_video_config.cpp`, `tests/test_d1_weapon_timing.cpp` |
| `2af7828` d1: cheaper per-draw CPU path (D1_DRAW_STATS, D1_DRAW_FAST, D1_GPU_WAIT_YIELD) (#7) | `CMakeLists.txt`, `d1/README.md`, `src/video_core/amdgpu/liverpool.cpp`, `src/video_core/renderer_vulkan/d1_draw_perf.cpp`, `src/video_core/renderer_vulkan/d1_draw_perf.h`, `src/video_core/renderer_vulkan/vk_pipeline_cache.cpp`, `src/video_core/renderer_vulkan/vk_pipeline_cache.h`, `src/video_core/renderer_vulkan/vk_rasterizer.cpp`, `src/video_core/renderer_vulkan/vk_rasterizer.h`, `src/video_core/renderer_vulkan/vk_scheduler.cpp`, `src/video_core/renderer_vulkan/vk_scheduler.h`, `tests/test_d1_draw_perf.cpp` |
| `95e44fe` d1: PC client: fatal-path stability, hitch halt knob, audio sync, PC layout with raw mouse look (#9) | `CMakeLists.txt`, `d1/PC-CONTROLS.md`, `d1/README.md`, `d1/Run-D1Tests.cmd`, `src/common/memory_patcher.cpp`, `src/core/d1_mouse_look.h`, `src/core/d1_native_mouse.cpp`, `src/core/d1_native_mouse.h`, `src/core/d1_stability.cpp`, `src/core/d1_stability.h`, `src/core/libraries/audio/audioout.cpp`, `src/core/libraries/audio/d1_audio_sync.h`, `src/core/libraries/audio/sdl_audio_out.cpp`, `src/core/libraries/kernel/threads/exception.cpp`, `src/core/signals.cpp`, `src/input/controller.cpp`, `src/input/d1_pc_layout.h`, `src/input/input_handler.cpp`, `src/sdl_window.cpp`, `tests/test_d1_audio_sync.cpp`, `tests/test_d1_mouse_look.cpp`, `tests/test_d1_pc_layout.cpp` |
