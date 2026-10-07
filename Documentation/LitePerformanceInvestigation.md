# Full Minecraft performance investigation

Date: 2026-10-07. Investigation baseline: `babylon-lite-pure-native`,
`7d9aadb02c63e9e4b08c2f4f4b6cc5122e7ed81f`.

This is a measured host/engine investigation, not an architecture rewrite.
The qualified executable and handwritten Core sources remain unchanged.
Experiments are opt-in, build-local source overlays. The investigation agent
made no commits, pushes, uploads or GitHub posts; parent review is separate.

**Root cause:** the native HUD backing rebuild costs 3.859 ms/frame, about 84%
of full native frame elapsed time. GC costs only 0.000418 ms/frame. Retaining
unchanged HUD backing reduces the complete hidden host to 1.631 ms/frame:
−64.33%, 95% CI [−64.76%, −63.81%]. After that change, bgfx handoff/backpressure
grows to about 0.966 ms/frame; the removed CPU work does not translate one-for-one
into throughput. Default behavior has not changed.

**Engine finding:** same-frame material-group aggregate reuse cuts its phase
by 91.38% and `bl_frame` by 29.78%, but whole-host improvement is only 3.73%,
inconclusive against the predefined 5% practical threshold. It does not improve
the already GPU/backpressure-limited retained-HUD host in the five-pair control.
Cached regex and GC removal do not produce a practically significant full-host
win. No claim that “C99/no-STL guarantees faster” follows from these results.

Native startup-to-`Run` entry has a fresh median of 3,856.301 ms. This is an
aggregate host/application/resource/compiler cost, not a standalone Tint
measurement; initial world-probe validation follows that timestamp. Compilation
is outside steady state. Do not classify all startup time as shader compilation.

## Scope and interpretation

The original Lite 1.32.0 Minecraft application at source pin
`2e064d88ec7422af946f8ec7f089ac6519f99295` is retained: SEED 1337, radius 6,
169 chunks, all original modules/shaders/atlas, alpha, water, mobs, simulation,
streaming, platform events, and user-language support. No cube/kernel,
smaller-radius, scene-only, or no-present substitute is used for performance
claims. The 169-chunk raw terrain oracle and post-water-prefill world are
different, intentionally:

| World state | Nonzero blocks | FNV |
|---|---:|---:|
| Raw terrain | 1,392,067 | 3315049788837698003 |
| Original water prefill | 1,417,083 | 6859480570131883071 |

Windows 11 build 26310, Xeon W-2235, Quadro P620, NVIDIA driver
32.0.15.8142; shared workstation and unchanged Lenovo Default power policy.
All formal runs are serial, Release `/O2 /Ob2 /DNDEBUG`, 1280×720, MSAA 1,
hidden windows with **real swapchains**, no physical input/vsync/pacing,
180 warmup + 1,200 measured frames, simulation delta 1000/60 ms. There are
no overlapping builds or capture/readbacks. Startup, file output and process
shutdown are outside the steady-state bracket.

The primary comparison is SDL_GPU/D3D12 versus native C99 + bgfx/D3D11.
Different graphics APIs, UI renderers, render-thread models, asynchronous GPU
query budgets and driver queues remain explicit confounds. A faster complete
candidate does **not** establish that C99 algorithms are faster than the
transpiled engine. Conversely, a slower full native host does not locate the
regression in LiteLayer.

### Earlier independently measured baseline, not new samples

The parent's 15-round, five-executable baseline had median run-average CPU:
SDL 1.948 ms; previous native-layer SDL 2.057 ms; stock Dawn 4.345 ms;
supplied WebGPU-compatible bgfx provider 2.253 ms; native bgfx/D3D11
4.550662 ms. Native versus SDL mean paired change was +130.003%,
bootstrap 95% CI [+123.792%, +134.486%].
Native versus stock Dawn was inconclusive at a 5% practical threshold.
Native main-thread core was 0.740202 ms; unallocated non-core remainder
3.809896 ms. That remainder was **not** then an attribution to HUD or GC.

This investigation does not reuse historical 2.364/3.053 ms values.
New balanced qualified-binary controls below account for workstation drift;
do not mix absolute times from separate batches into causal ratios.

## Executed results

<!-- PERF_RESULTS_BEGIN -->
**Executed:** 15 balanced primary rounds × 13 variants = 195 processes; five balanced renderer rounds × seven variants = 35 processes. All use 180/1,200 frames. Every native formal run has identical world/camera/time/mob/material and per-frame draw-count footprints.

### Fresh full-host and ablation comparisons

| Primary variant | Median run-average ms | Median run P95 ms | Scope |
|---|---:|---:|---|
| sdl-qualified | 2.547000 | 3.106000 | Qualified unchanged executable |
| sdl-off | 2.540000 | 3.099000 | Opt-in isolated diagnostic |
| sdl-phases | 2.549000 | 3.157000 | Opt-in isolated diagnostic |
| sdl-allocations | 2.539000 | 3.100000 | Allocation-counter overhead |
| native-qualified | 4.701255 | 5.192500 | Qualified unchanged executable |
| native-off | 4.582851 | 5.011800 | Opt-in isolated diagnostic |
| native-phases | 4.597146 | 5.067600 | Opt-in isolated diagnostic |
| native-allocations | 4.614719 | 5.298300 | Allocation-counter overhead |
| native-hud-off | 1.611488 | 3.367000 | **NONPARITY** diagnostic |
| native-hud-retained | 1.630967 | 3.381800 | Opt-in isolated diagnostic |
| native-regex | 4.573017 | 5.146800 | Opt-in isolated diagnostic |
| native-gc-off | 4.645151 | 5.326700 | **NONPARITY** diagnostic |
| native-grouping | 4.394007 | 4.977200 | Opt-in isolated diagnostic |

Changes are mean paired per-run changes; CIs bootstrap the 15 run pairs, not individual frames.

| Control → candidate | Change | Bootstrap 95% CI | 5% classification |
|---|---:|---:|---|
| sdl-qualified -> sdl-off | +0.15% | [-3.29%, +3.44%] | interval-within-5-percent |
| sdl-off -> sdl-phases | -0.14% | [-3.43%, +3.40%] | interval-within-5-percent |
| sdl-phases -> sdl-allocations | +0.14% | [-2.53%, +2.93%] | interval-within-5-percent |
| native-qualified -> native-off | -2.72% | [-4.23%, -1.32%] | interval-within-5-percent |
| native-off -> native-phases | -0.20% | [-1.58%, +1.16%] | interval-within-5-percent |
| native-phases -> native-allocations | +0.30% | [-1.36%, +1.74%] | interval-within-5-percent |
| native-phases -> native-hud-off | -64.73% | [-65.15%, -64.26%] | faster-beyond-5-percent |
| native-phases -> native-hud-retained | -64.33% | [-64.76%, -63.81%] | faster-beyond-5-percent |
| native-phases -> native-regex | -0.70% | [-2.28%, +0.80%] | interval-within-5-percent |
| native-phases -> native-gc-off | +1.74% | [-0.49%, +4.24%] | interval-within-5-percent |
| native-phases -> native-grouping | -3.73% | [-5.26%, -2.04%] | inconclusive-at-5-percent |
| sdl-qualified -> native-qualified | +83.24% | [+76.50%, +89.55%] | slower-beyond-5-percent |
| sdl-phases -> native-phases | +78.36% | [+73.27%, +83.36%] | slower-beyond-5-percent |
| sdl-qualified -> native-hud-retained | -36.63% | [-38.82%, -34.59%] | faster-beyond-5-percent |

Native core-bracket overhead is checked separately because HUD-heavy totals can hide instrumentation cost:

| Native variant | Median original core-bracket ms/frame |
|---|---:|
| native-qualified | 0.775807 |
| native-off | 0.737761 |
| native-phases | 0.738468 |

| Core instrumentation control | Paired change | 95% CI |
|---|---:|---:|
| native-qualified -> native-off | -5.27% | [-6.85%, -3.51%] |
| native-off -> native-phases | -0.12% | [-1.84%, +1.55%] |

### Native versus SDL phase ledger

Median of each run's mean, milliseconds/frame. Parentheses denote nested spans; do not sum them into outer totals.

| Span | Native full | SDL full |
|---|---:|---:|
| Platform timers | 0.001047 | — |
| Audio poll | 0.000043 | — |
| bl_frame (includes callbacks) | 0.706964 | — |
| (Original callbacks / camera update) | 0.174145 | 0.128728 |
| (Native bl_frame minus callbacks) | 0.532819 | — |
| (Scene setup) | 0.004156 | — |
| (Member/world visit) | 0.033816 | — |
| (Material grouping) | 0.237905 | — |
| (Sort) | 0.056313 | — |
| (Material/uniform/bgfx enqueue) | 0.199123 | — |
| bgfx::frame handoff/wait | 0.031525 | — |
| Native HUD backing | 3.858670 | — |
| (HUD setup/DIB clear) | 0.684900 | — |
| (HUD elements/text/icons/regex) | 1.541822 | — |
| (HUD alpha scan) | 0.979272 | — |
| (HUD backing memcpy) | 0.395345 | — |
| (HUD visible presentation check) | 0.000847 | — |
| (HUD DC/DIB disposal) | 0.249816 | — |
| User-language GC | 0.000418 | 0.000665 |
| SDL acquire | — | 0.019463 |
| SDL RmlUI layout/projection | — | 0.091521 |
| SDL synchronize/plan/upload | — | 0.019803 |
| SDL encode (includes UI frame recording) | — | 0.589904 |
| SDL present (includes UI and driver submit) | — | 1.695164 |
| (SDL RmlUI GPU command recording) | — | 0.782657 |
| (SDL actual driver command submission) | — | 0.909959 |
| SDL finish (includes GC/timers) | — | 0.002056 |
| Unaccounted original-total remainder | 0.000428 | 0.001425 |
| Original total | 4.597146 | 2.548861 |

### Ranked native bottlenecks

| Rank | Owner / cost | ms/frame | % of native original total | Certainty |
|---:|---|---:|---:|---|
| 1 | HUD backing rebuild | 3.858670 | 83.936% | Direct outer/subphase timing + removal/retention controls |
| 2 | Engine material grouping | 0.237905 | 5.175% | Direct phase + same-frame reuse control |
| 3 | Material/uniform/bgfx draw enqueue | 0.199123 | 4.331% | Direct aggregate; backend execution is elsewhere |
| 4 | Generated application callbacks | 0.174145 | 3.788% | Direct callback boundary; includes forwarded setters |
| 5 | Engine sorting | 0.056313 | 1.225% | Direct phase; no sort-elimination control |
| 6 | Member visitation/matrix evaluation | 0.033816 | 0.736% | Direct phase + world call/rebuild counts |
| 7 | bgfx frame handoff/backpressure | 0.031525 | 0.686% | Direct phase; changes after HUD retention |
| 8 | Platform timer dispatch | 0.001047 | 0.023% | Direct phase + callback/pending counts |
| 9 | User cycle collector | 0.000418 | 0.009% | Direct phase + GC-off null/control + graph counts |

The HUD is 99.95% of measured non-core time. It is not merely desktop presentation: the hidden-window visibility/presentation check is 0.000847 ms/frame. User GC is 0.000418 ms/frame, not the multi-millisecond remainder.

### Phase-level controlled changes

| Control → candidate / phase | Change | 95% CI |
|---|---:|---:|
| native-phases -> native-grouping: grouping | -91.38% | [-91.52%, -91.22%] |
| native-phases -> native-grouping: blFrame | -29.78% | [-31.06%, -28.39%] |
| native-phases -> native-regex: hudElements | -2.27% | [-3.78%, -0.90%] |
| native-phases -> native-regex: hud | -0.94% | [-2.46%, +0.52%] |
| native-phases -> native-hud-retained: hud | -99.47% | [-99.51%, -99.41%] |
| native-phases -> native-gc-off: gc | -93.70% | [-94.03%, -93.26%] |
| sdl-phases -> native-phases: callbacks | +34.80% | [+29.93%, +39.45%] |

### Execution, renderer waits and GPU

| Primary variant | Total QPC ms | Whole-process CPU ms/frame | Main execution ms/frame | Renderer execution ms/frame | bgfx renderer-submit wall ms | waitRender ms | waitSubmit ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| sdl-phases | 2.548861 | 2.044271 | 2.018229 | — | — | — | — |
| native-phases | 4.597146 | 5.468750 | 4.518229 | 0.429688 | 0.208400 | 0.002360 | 4.253089 |
| native-hud-off | 1.611488 | 1.757812 | 0.677083 | 0.351562 | 0.181163 | 0.984822 | 0.049184 |
| native-hud-retained | 1.630967 | 1.679688 | 0.716146 | 0.364583 | 0.178661 | 0.962394 | 0.055141 |
| native-regex | 4.573017 | 5.455729 | 4.479167 | 0.429688 | 0.208646 | 0.002370 | 4.218895 |
| native-grouping | 4.394007 | 5.364583 | 4.335938 | 0.455729 | 0.213213 | 0.002400 | 4.043387 |

Non-main CPU by endpoint thread start module/name (zero groups omitted; raw receipts retain every thread):

| Variant | Other thread/module | CPU ms/frame |
|---|---|---:|
| sdl-phases | nvwgf2umx.dll | 0.013021 |
| native-phases | nvwgf2umx.dll | 0.520833 |
| native-hud-retained | nvwgf2umx.dll | 0.664062 |

| Native variant | GPU median run average ms | Distinct frame queries | Repeated IDs | Unavailable |
|---|---:|---:|---:|---:|
| native-phases | 1.416202 | 16763 | 1237 | 0 |
| native-hud-off | 1.595049 | 17943 | 57 | 0 |
| native-hud-retained | 1.614443 | 17930 | 70 | 0 |
| native-grouping | 1.415875 | 16755 | 1245 | 0 |

### Renderer diagnostic batch (five run pairs)

| Variant | Median full-host ms/frame | Process CPU ms/frame | GPU query availability |
|---|---:|---:|---|
| native-phases | 4.592098 | 5.468750 | 5843 distinct; 0 unavailable |
| native-init-profile-off | 4.661812 | 5.520833 | 5845 distinct; 0 unavailable |
| native-retained | 1.553981 | 1.679688 | 5980 distinct; 0 unavailable |
| native-retained-init-profile-off | 1.553344 | 1.757812 | 5880 distinct; 0 unavailable |
| native-retained-queue3 | 1.198573 | 1.640625 | 5968 distinct; 0 unavailable |
| native-retained-single-thread | 1.519834 | 1.679688 | 5942 distinct; 0 unavailable |
| native-retained-grouping | 1.574239 | 1.536458 | 5979 distinct; 0 unavailable |

| Control → renderer candidate | Paired change | 95% CI |
|---|---:|---:|
| native-phases -> native-init-profile-off | +4.36% | [+0.21%, +9.24%] |
| native-retained -> native-retained-init-profile-off | +6.76% | [-3.54%, +17.82%] |
| native-retained -> native-retained-queue3 | -22.11% | [-24.49%, -19.75%] |
| native-retained -> native-retained-single-thread | +1.66% | [-3.66%, +8.88%] |
| native-retained -> native-retained-grouping | +1.40% | [-0.69%, +3.09%] |

### GC, allocation and scheduling observations

| Variant | Live nodes first → last | Collections/run | Collected nodes/run | Roots / owning edges per collection | Mean actual collection ms | Process private-byte growth |
|---|---:|---:|---:|---:|---:|---:|
| sdl-phases | 43 → 45 | 20 | 0 | 32.95 / 37.9 | 0.034860 | 20480 B |
| native-phases | 11 → 11 | 20 | 0 | 11 / 3 | 0.023330 | -765952 B |
| native-gc-off | 11 → 11 | 0 | 0 | 0 / 0 | disabled | -1.52371e+06 B |
| native-hud-retained | 11 → 11 | 20 | 0 | 11 / 3 | 0.017420 | 20480 B |

| Allocation-counting variant | C++ new calls/frame | Requested bytes/frame | C99 host allocations/frame | C99 requested bytes/frame |
|---|---:|---:|---:|---:|
| sdl-allocations | 344.466 | 11872.5 | 0.000000 | 0.000 |
| native-allocations | 229.356 | 10423.3 | 0.039167 | 39.314 |

Per-frame medians of run mean counters (full phases):

| Counter | Native | SDL |
|---|---:|---:|
| draws | 402.625000 | 402.625000 |
| renderItems | 412.562500 | 402.625000 |
| primitiveTriangles | 375971.160000 | — |
| vectorGets | 60.830000 | — |
| vectorSets | 96.521667 | — |
| uniformSets | 26.000000 | — |
| uniformSlotTests | 97.000000 | — |
| worldCalls | 463.250000 | — |
| worldRebuilds | 70.562500 | — |
| regexCompiles | 10.000000 | — |
| hudDraws | 1.000000 | — |
| coreAllocations | 0.039167 | — |
| coreBytes | 39.314167 | — |
| uploadBytes | 92682.000000 | 0.000000 |
| managedNew | 0.000000 | 0.001667 |
| pendingTimers | 0.000000 | 0.000000 |
| timerCallbacks | 0.000000 | 0.000000 |
| rmlTreeChanges | — | 0.000000 |
| rmlFullProjections | — | 0.000000 |
| rmlTextUpdates | — | 0.000000 |

`uploadBytes` in native counts actual bgfx uniform bytes enqueued, not vertex/index uploads; SDL counts its buffer upload batch. These are different categories and must not be compared as equivalent traffic.

Receipts: `build\lite-c99\perf-deep-dive\Matched15`, `Renderer5`, `GuardQualification180`, `GuardQualification1380`; derived ledger/thread/GC/GPU statistics: `derived.json`. All source/binary/config hashes and individual samples remain in those directories.
<!-- PERF_RESULTS_END -->

## Exact timing ledger

All phase arrays are QPC elapsed intervals, not thread execution time. There is
no per-frame printing, screenshot, JSON serialization, or filesystem I/O.
Outer totals retain their original bracket. Allocated vectors are reserved
before measurement; recording occurs after the original native total ends.
The residual is reported rather than redefining the total to make it smaller.
Nested intervals must not be added twice.
Table entries are medians of individual run means; independently selected
medians need not sum exactly to the median total. Residuals and HUD/non-core
fractions are calculated within each run before taking medians.

### Native

Original `Source/NativeHost.cpp:319-369`:

1. OS message pump, replay and event dispatch: before the original total.
2. `TickPlatform`, including due timer callbacks.
3. Native engine audio polling.
4. `bl_frame`, including original generated application callbacks.
5. `bgfx::frame`, including any handoff/backpressure.
6. `PresentHUD`: backing render even when the window is hidden.
7. `collect_at_frame_boundary`.
8. Original total endpoint; engine/GPU stats and sample recording follow it.

The `bl_frame` ledger subtracts the measured callback interval and separates:
view/camera setup; scene member visitation/world-matrix evaluation; material
grouping; sorting; material/uniform/texture preparation and bgfx draw enqueue.
`Core/LiteLayer/Source/Engine.cpp:996-1158` is the original owner.
`Nodes.cpp:542-635` already uses dirty/parent-version matrix evaluation.
Uniform setter timing is nested inside callbacks, not additional Core time.

The bgfx `Stats` render-thread submit interval, `waitRender`, `waitSubmit`,
triangles and asynchronous GPU frame IDs are collected separately.
`waitSubmit` is renderer **waiting for the application to produce work**,
not extra application computation. Render-submit wall time excludes some
renderer activity (notably swapchain flip); whole-thread execution time
therefore remains necessary.

### SDL

Read-only-reference `native/src/pal_sdl_gpu.cpp:1511-1682,2019-2095,4209-4417`:

1. Platform/input poll and replay: before original start.
2. Command-buffer/swapchain acquire.
3. `advance_frame`: clock, camera/application/before-render callbacks.
4. RmlUI projection/layout update.
5. Scene/plan synchronization, dirty resource processing and upload batch.
6. Encode: RmlUI frame recording plus scene draw encoding.
7. Present: RmlUI GPU command recording and actual command-buffer submission.
8. `finish_frame`, including user GC, deferred/timer callbacks and audio cleanup.
9. Original total endpoint.

The overlay separately times `render_sprite_ui_sdl_gpu_frame` and
`SDL_SubmitGPUCommandBuffer` so “present” is not falsely all driver wait.
SDL scene encode still aggregates context render recording and scene encoding;
it is **not** a pure engine algorithm span.
`pal_gpu_frame.cpp:202-240` includes GC in SDL's original total.

Useful approximately corresponding spans are callback update, GC, and complete
host elapsed time. Native draw enqueue is not comparable alone to SDL encode:
SDL pushes work to D3D12 on the calling thread; bgfx performs backend work on
another thread. The report supplies process CPU and native render-thread data
instead of drawing an engine-speed conclusion from unmatched totals.
Native idle receipts report unavailable audio output and no submitted audio
samples. The small polling cost is not a bound on audible interactive XAudio2
mixing; original audio/replay correctness tests remain separate evidence.

## Attribution and controls

### Native HUD: backing rendering, not desktop overlay presentation

Original `Source/Platform.cpp:800-922` creates/deletes a 1280×720 DIB/DC every
frame, clears 3,686,400 bytes, visits all elements, compiles ten identical icon
regular expressions, calls GDI text/icon routines, scans all 921,600 pixels
to reconstruct alpha, then copies the complete surface to `s_hud`.
Only `UpdateLayeredWindow`/desktop overlay presentation is omitted when hidden.

The measured subphases separate setup/clear, element drawing, alpha scan,
backing copy, visibility/presentation, and resource disposal. Thus the finding
is not the unsupported generalization “GDI is slow”; it identifies recurring
work and its cost in this particular adaptation.

RmlUI's original `pal_ui_rml.cpp:5804-5914` uses revision/viewport checks,
text-only synchronization, conditional tree/style projection and retained
context geometry. It still updates/records/render-submits UI each frame.
Measured tree-change/full-projection counts distinguish its retained behavior
from the native full-surface rebuild; retained layout does not mean zero GPU
UI work.

Controls:

* `LITE_PERF_HUD_OFF=1`: **NONPARITY DIAGNOSTIC**. Removes backing drawing,
  not simulation or DOM setters. This is not a shipping improvement.
* `LITE_PERF_HUD_RETAINED=1`: compare exact snapshots of the mutable inputs
  in this fixed HUD workload (`css`, text, styles and children); redraw only
  on a change. Element tags and icon resources are fixed in this
  trace, not additional dynamic snapshot inputs. This conservative experiment
  still copies/compares strings/containers and is not a proposed final
  invalidation implementation.
  It is qualified for fixed-size hidden runs. Moving/showing an interactive
  window, DPI/viewport changes, font/theme/resource invalidation and overlay
  re-presentation need separate implementation tests before shipping.
* `LITE_PERF_CACHED_REGEX=1`: retain the same compiled expression; no change to
  pattern, match semantics, icon selection, alpha or text rendering.

### Material grouping: a real engine hot path

`Engine.cpp:1111-1144` scans every scene member for every opaque draw to compute
the material's minimum explicit/default order and earliest membership index.
This is quadratic in member/draw counts despite having only eight materials.
The opt-in experiment reuses a same-material aggregate already computed in
the **current frame**. The first draw still uses the original complete scan,
including invisible/non-drawn material members.

This is not an across-frame cache and does not need speculative revision
invalidation. Transparency/depth ordering, explicit render orders, sequence,
shared-scene ownership, membership and qsort comparator remain unchanged.
The original branch is the default. The full application, ordered draw-state
hashes and exact images—not a synthetic lower-radius benchmark—qualify it.
It should be reviewed independently of HUD changes.

### GC and allocation ownership

The reused user-language runtime is outside Core. The default collector
(`native/include/bblite/js_gc.hpp:447-531`) runs at a bounded cadence: every
60 frames or after at least eight frames and 1,024 managed allocations.
Instrumentation records live nodes, roots, owning edges, traced nodes,
collections, collected nodes and managed allocations. It does **not** claim
that registry nodes equal all C++ heap allocations or all reachable bytes.

`LITE_PERF_GC_OFF=1` is a **NONPARITY DIAGNOSTIC**, bounded to the measured
process; original exit collection is retained. Track node/private-byte growth
and final teardown. Low collection cost and no collected cycles in an idle
trace do not prove collection can be removed from arbitrary gameplay,
save/load, cyclic closures, workers or future application code.

The separate allocation-counting variants replace host C++ `new/delete` with
counted standard malloc/aligned-malloc forwarding. Counters are control-thread
only and record requested bytes, **not total live heap or physical traffic**.
DLL allocations, bgfx's allocator, GDI kernel allocations and render-thread
allocations are outside this counter. The C99 host allocator separately counts
successful calls, requested bytes, frees and current/peak bytes.
Whole-process private bytes are a coarse complement, not a lifetime proof.
Allocation-counter and phase overhead have actual balanced controls.
Core-bracket overhead is also reported separately: a small percentage of a
HUD-heavy total could still be significant against the short Core bracket.
Phase measurements are not silently overhead-corrected.

### Renderer, threads and GPU

Whole-process kernel+user execution time uses `GetProcessTimes` over the common
1,200-frame measured window, excluding startup and warmup.
Main-thread `GetThreadTimes`, per-existing-thread execution/cycles/descriptions,
and start modules are collected at window endpoints. New/exited threads can
escape the endpoint intersection; process CPU still includes them.
15.625 ms accounting quantization becomes about 0.013 ms/frame over 1,200
frames. Do not confuse elapsed QPC with execution or sum overlapping wall spans.

GPU queries use actual bgfx frequency/timestamps, `-1` unavailable values and
per-run GPU-frame-ID deduplication. Repeat IDs are not additional measurements.
The initialization-profile toggle is measured on the same native path, but
**does not disable GPU frame queries** on this pin. `renderer_d3d11.cpp:1328`
sets timer-query support; `6929-6934` begins frame queries irrespective of
`init.profile`. `init.profile` participates in RenderDoc startup probing
(`847-852`), not this query gate. The diagnostic flag's original name,
`LITE_PERF_GPU_OFF`, therefore means only `init.profile=false`; renderer
results are explicitly named `init-profile-off`.
Actual GPU-query-disable overhead cannot be measured through the public
initialization flag on this backend; both controls retain real queries.
SDL's qualified
executable provides no equivalent enabled frame GPU query stream; this remains
a GPU/API comparison gap, not a justification for fabricated SDL GPU zeros.

Queue-depth-three and single-thread controls retain the hidden swapchain.
Faster enqueue can just increase work in flight/display latency. In particular,
queue-depth-three preserves ordered command-state hashes but changes the final
asynchronous screenshot; frame/capture alignment remains unresolved. Do not
claim its pixels are frame-identical or adopt its buffering from throughput
alone. No offscreen/no-present control is substituted for complete-host results.
Configured buffering is known (default D3D11 clamps zero requested backbuffers
to two; queue3 requests three and maximum latency three), but physical display
latency and actual instantaneous frames in flight are not measured. GPU query
IDs are completion identities, not a display-latency measurement.

### Timer clocks: explicit semantic confound

SDL `pal.cpp:362-400` virtualizes `performance_milliseconds` with the fixed
frame step, advanced by `FrameClock` (`pal_gpu_frame.hpp:176-189`).
Native `Platform.cpp:82-87,343-348,778-798` schedules and dispatches using a
wall `steady_clock`; its `TickPlatform` ignores the simulation-delta argument.
At 1,380 completed frames, SDL has about 22,983 ms virtual time, while native
wall time depends on startup and host throughput.

Timer-count arrays quantify actual callbacks/pending timers in each run.
The idle no-input workload has no toast timer; original `minecraft/hud.ts`
uses a 1,800 ms timeout after a toast. Consequently, speeding the host can
change how many simulated frames a wall-scheduled toast survives. This was
**not silently aligned** in this investigation. Future deterministic replay
comparisons must name both clock policies and test timing semantics explicitly.

## Guidelines for implementation agents

Each numerical acceptance criterion is workload-specific, not a universal
budget for unmatched graphics APIs.

| Owner | Mechanism supported by evidence | Invariant and required test |
|---|---|---|
| Engine | Aggregate repeated material-order work once per frame, rather than one full membership scan per draw. | Preserve invisible members, explicit/default orders, alpha/depth ordering, shared ownership and sequence. Match ordered draw hashes, exact full-game images, replay/save/load and small randomized grouping tests. Require a paired measured reduction in the grouping phase; never infer speed from complexity alone. |
| Engine | Retain dirty/parent-version matrix computation already present. | Three component setters may make one matrix dirty, not demand three eager recomputations. Test parent mutation, camera targeting, callbacks and stale handles; compare world calls/rebuild counts and full state. |
| Engine/resource | Reuse stable buffers and validated material layouts where measured. | Preserve range updates, capacities, integer packing, reflection, texture/alpha state and deferred GPU lifetime. Idle low allocation does not characterize chunk-streaming/edit spikes; require replay/streaming measurements separately. |
| Engine | Uniform string/slot lookup is a measurable but small nested cost here. | Cache only validated identity/layout/generation; preserve material-instance mapping and stale-handle rejection. Do not prioritize an unmeasured “zero lookup” rewrite ahead of the larger grouping/HUD costs. |
| Host/UI | Retain the backing surface and redraw dirty content; avoid recurring regex compilation. | Key every actually rendered input, viewport/DPI/fonts/icons/style/text/visibility, and present/move overlays independently of redraw. Require idle plus F3/selection/toast/input/save/load negative-invalidation tests and pixel equivalence. Do not remove HUD or alpha to meet a budget. |
| Integration | Name full-loop, callback, Core, backend-thread, process execution, wait and GPU spans separately. | Original total includes waits and GC. Use matched windows, no per-frame output, measured profiling overhead, deduplicated GPU IDs and sentinel availability. Passing a sub-ms enqueue budget cannot replace complete-host validation. |
| Transpiler/runtime | Preserve ownership-aware bounded cycle collection; prioritize measured churn, not blanket GC removal. | Count traced roots/edges/collections and heap growth; validate cyclic closures, last-root drops, disposal/re-registration and long bounded runs. Idle zero cycles is not global lifetime evidence. |
| Structure/build | Keep shader compiler, user containers, native platform and profiling services outside bgfx/bx-only Core. | Audit actual dependencies/objects/public C99 header and style/AST checks. No-STL/C-style is an architectural rule, not proof of speed. |
| Renderer/host | Treat threading/queue depth as latency/backpressure configuration. | Retain real presentation, record whole process and all relevant threads, GPU durations, queue/waits and capture alignment. Adopt only with a latency acceptance criterion, not lower API-thread elapsed time alone. |
| Compiler/startup | Separate runtime shader compilation from steady-state cost. | Cache only complete source/options/backend/compiler/reflection inputs with correct diagnostics and ownership. Arbitrary runtime WGSL remains required; baking/dead-stripping are distinct startup/payload experiments, not established frame wins. |

### Practical prioritization in this workload

1. **Host/UI first:** retained backing is the only tested parity-preserving
   hidden-host candidate with a large full-loop improvement. Its steady-state
   HUD comparison is about 0.019 ms/frame instead of 3.859 ms. Replace snapshot
   copying with complete dirty tracking only after invalidation tests; preserve
   GDI alpha, icons, text and desktop presentation.
2. **Then engine CPU:** same-frame grouping is about 0.020 ms instead of
   0.238 ms; target that measured work, not a wholesale API/data-structure port.
   In the retained-HUD control process execution falls from about 1.680 to
   1.536 ms/frame, but elapsed throughput does not improve. Maintain both
   computation and end-to-end acceptance criteria.
3. **Do not optimize the wrong owner:** native full process execution is
   5.469 ms/frame, not the roughly 0.738 ms API-thread core bracket.
   NVIDIA worker execution is measurable outside the bgfx render thread.
4. **Preserve null results:** cached regex changes the elements phase by only
   2.27%; full-loop CI is within ±5%. GC-off is also within ±5%. Neither can
   explain the original multi-millisecond gap.
5. **Preserve latency:** queue-three reduces elapsed time by 22.11%
   [−24.49%, −19.75%] in the renderer batch, while process CPU is essentially
   unchanged and capture alignment differs. It is a queue/latency tradeoff,
   not a 22% reduction in engine computation.

## Public teammate-port contrast

Parent-reviewed `bghgary/BabylonLiteNativePort`,
`ca5b8fdafdbc034982982210d1daa20dbe210218`, contributes useful measurement and
ownership ideas, not a comparable Minecraft speed result.
The review is `Experiments/Mincraft/pure-native/TEAM-PORT-EVALUATION.md` in
the investigation repository. This agent did not duplicate the remote audit.

Its whole-process CPU, encoder/`frame` split and render/wait stats motivated
corresponding measurements here. Its benchmark's one-frame wall warmup,
different stats warmup, unreset phase history, zero unavailable queries and
offscreen/no-present mode are **not** adopted.
Its 400 BoomBox-clone/CSM workload has neither our transpiled game/HUD/GC nor
Minecraft shaders/streaming. Claimed executable sizes/FPS are not independently
reproduced and are not comparison baselines.

Immutable geometry sharing can help repeated mob primitives, not distinct
voxel chunk geometries; updates need detach/copy-on-write and safe retirement.
Static-pass dirty gating is a general principle, but Minecraft has no shadow
pass and already has lazy matrix evaluation. These are unmeasured future
proposals here. Direct Tint→HLSL and renderer/decoder feature stripping may
reduce startup/payload, not demonstrated steady-state work. Do not copy its
text-based cbuffer bridge/fixed interface hash, change dependency pins or
weaken arbitrary WGSL/reflection/ownership contracts.

## Reproduction, isolation and receipts

Build root: `build\lite-c99\perf-deep-dive`.
Qualified baseline remains `build\lite-c99\full-native-game`; its SHA256 is
`BB6C0D5291DA643F81BCEFF1BE3B34C8C0A2F509F8C887E0A5793368E4EA361A`.

`Apps\LiteTests\NativeProjection\FullMinecraft\PerfTools` owns:

* `prepare.py`: exact-anchor source overlays with input/output SHA256 manifests.
  SDL overlay copies local PAL/user headers for an independent diagnostic
  executable; no compiler rebuild or write to the dirty bblitec checkout.
* `Perf.h`, `PerfC.h`, `Allocations.cpp`: host-only diagnostic storage,
  endpoint process/thread/heap accounting and opt-in counters.
* `run.py`: balanced rotating/reversing process runs using the parent's actual
  harness, validation and bootstrap mathematics. Each run is the statistical
  observation; 1,200 frames are **not** 1,200 independent CI observations.
* `qualify.py`: outside-benchmark image/receipt and ordered draw-state checks.
* `test_tools.py`: source/overlay hash isolation, explicit-anchor refusals,
  opt-in/default safety and parity-qualification assertions.
* `analyze.py`: derived tables and ranked diagnostics from raw receipts.

Configure the existing isolated FullMinecraft build with the same pinned
library/cache paths as `BabylonLite.md`, but a **new** build directory and:

```powershell
$env:Path = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer;' + $env:Path
$configure = @(
    'cmake -S E:\Github\BabylonNative\Apps\LiteTests\NativeProjection\FullMinecraft'
    '-B E:\Github\BabylonNative\build\lite-c99\perf-deep-dive\Validation -G Ninja'
    '-DCMAKE_BUILD_TYPE=Release -DLITE_MINECRAFT_BUILD_GAME=ON'
    '-DLITE_MINECRAFT_PERF_TOOLS=ON -DBABYLON_LITE_ENABLE_STYLE_CHECKS=ON'
    '-DBBLITEC_SOURCE_DIR=E:\Github\bblitec-native-babylon-layer'
    '-DLITE_MINECRAFT_EMITTED_DIR=E:\Github\BabylonNative\build\lite-c99\full-native-projection\Emitted'
    '-DLITE_NATIVE_LIBRARY_DIR=E:\Github\BabylonNative\build\lite-c99\host'
    '-DLITE_BGFX_SOURCE_DIR=E:\Github\BabylonNative\build\lite-c99\deps\bgfx.cmake'
    '-DLITE_TINT_LIBRARY_DIR=E:\Github\bblitec-native-babylon-layer\.cache\tint\build-tint-94ea0623e61d4ab7\dawn'
    '-DLITE_JSON_INCLUDE_DIR=E:\Github\Babylon-Lite-Transpiler-Investigation\.ai\upstream-bblitec\artifacts\vcpkg-installed\development-full\x64-windows\include'
) -join ' '
$developer = 'call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul'
& $env:ComSpec /c ($developer + ' && ' + $configure +
    ' && cmake --build E:\Github\BabylonNative\build\lite-c99\perf-deep-dive\Validation --target LiteMinecraftNative --parallel 3')
```

Those are the executed validation-build arguments. Use a **new** directory for
another build; do not rebuild frozen `Native`, `Sdl` or `full-native-game`.
Exact configure/build commands and output also remain in the build logs and
`CMakeCache.txt`/`build.ninja`. Current guarded source adds outside-benchmark
hash hooks; future timing builds must repeat profiling-overhead controls rather
than assuming the frozen timing executable's exact instruction layout.

`LiteMinecraftPerfStyleCheck` enforces the existing Allman/100-column
formatter and semantic AST rules on diagnostic Core overlays. No profiling
header or class is inserted into the public Core or linked library.
Three instrumented free-function translation units precede the existing static
library; other Core objects and qualified bgfx/compiler libraries are reused.

The SDL overlay uses cached pinned RmlUI/LabSound/SDL packages, the original
`generated\dll-default-minecraft` application, SDL_GPU, Release, visual capture
on, source-profile off, no PCH/cache/LTCG changes. Its DLLs are byte copies of
the qualified local SDL deployment. Build commands/cache/overlay hashes are
preserved under the isolated root. Neither baseline executable is overwritten.

SDL overlay preparation and executed configure arguments:

```powershell
# Historical executed arguments; choose fresh overlay/build paths to rerun.
python Apps\LiteTests\NativeProjection\FullMinecraft\PerfTools\prepare.py sdl `
    E:\Github\BabylonNative E:\Github\bblitec-native-babylon-layer `
    E:\Github\BabylonNative\build\lite-c99\perf-deep-dive\SdlOverlay
# Execute configure and build after vcvars64.bat in the same cmd.exe process.
cmake -S build\lite-c99\perf-deep-dive\SdlOverlay `
    -B build\lite-c99\perf-deep-dive\Sdl -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DBBLITE_GENERATED_DIR=E:\Github\bblitec-native-babylon-layer\generated\dll-default-minecraft `
    -DBBLITE_BACKEND=SDL_GPU -DBBLITE_AUDIO_CAPTURE=OFF `
    -DBBLITE_PCH=OFF -DBBLITE_NATIVE_CACHE=OFF `
    -DBBLITE_RMLUI_DIR=E:\Github\bblitec-native-babylon-layer\artifacts\tools\rmlui `
    -DBBLITE_LABSOUND_DIR=E:\Github\bblitec-native-babylon-layer\artifacts\tools\labsound `
    -DCMAKE_PREFIX_PATH=E:\Github\Babylon-Lite-Transpiler-Investigation\.ai\upstream-bblitec\artifacts\vcpkg-installed\development-full\x64-windows
cmake --build build\lite-c99\perf-deep-dive\Sdl --parallel 3
```

Run `run.py --help` for required paths. Formal output directories refuse reuse.
The source manifest includes original/overlay/application/tool hashes, binary
hashes, full configuration and the parent harness hash.
Every process has a log and original CPU/GPU receipt where supported; every
diagnostic process has QPC phase/count arrays and endpoint CPU/thread/heap data.

| Diagnostic environment variable | Meaning |
|---|---|
| `LITE_PERF_PHASES=1` | QPC phase/count arrays; not shipping telemetry. |
| `LITE_PERF_OUTPUT=<absolute JSON path>` | Write the additional receipt after measurement. |
| `LITE_PERF_ALLOCATIONS=1` | Control-thread C++ allocation-call/requested-byte counters. |
| `LITE_PERF_HUD_OFF=1` | **Nonparity** backing-HUD removal. |
| `LITE_PERF_HUD_RETAINED=1` | Fixed hidden-HUD exact-input snapshot retention. |
| `LITE_PERF_CACHED_REGEX=1` | Same icon expression retained once. |
| `LITE_PERF_GC_OFF=1` | **Nonparity** bounded process diagnostic; exit collection remains. |
| `LITE_PERF_GROUPING_CACHE=1` | Same-frame opaque-material aggregate reuse. |
| `LITE_PERF_QUEUE3=1` | Request three backbuffers/maximum frame latency three. |
| `LITE_PERF_SINGLE_THREAD=1` | bgfx single-thread initialization via pre-init `renderFrame`. |
| `LITE_PERF_GPU_OFF=1` | **Initialization-profile flag only; frame GPU queries remain on.** |
| `LITE_PERF_DRAW_GUARD=1` | Guarded validation build only; forbidden in benchmark mode. |

Corrected parity reproduction:

```powershell
python Apps\LiteTests\NativeProjection\FullMinecraft\PerfTools\qualify.py `
    --native-root E:\Github\BabylonNative `
    --profile E:\Github\Babylon-Lite-Transpiler-Investigation\artifacts\pure-native-comparison\baseline-profile.json `
    --binary-directory Validation --qualification-name FreshGuardQualification
```

### Qualification correction and frozen timing provenance

An initial qualification contained **empty draw-hash arrays**: the overlay
generator ran only at CMake configure, and subsequent generator edits had not
triggered regeneration. The initial image comparisons were valid, but the
empty arrays were not draw-state evidence. A tool assertion exposed this.

The generator is now a `CMAKE_CONFIGURE_DEPENDS` input, qualification refuses
anything other than one hash per requested frame, and guards explicitly reject
benchmark mode. A separate `Validation` build was created; the 230 formal
timing processes' `Native`/`Sdl` executables and overlays were **not rebuilt**.
The guarded Core sources match those frozen timing sources after removing only
hash-call statements and whitespace; this is recorded and unit-tested.

Corrected `GuardQualification180` verifies all eight native configurations with
180 **nonempty, ordered** state hashes. Regex, grouping, retained HUD, their
combination, single-thread and initialization-profile controls have exact
scene/HUD pixels. Queue3's final capture remains explicitly nonexact.
`GuardQualification1380` additionally verifies default versus retained+grouping
at every one of 1,380 frames, including measured-window mob creation, and exact
first/final scene/HUD images. The default guarded images also match the original
qualified baseline's scene/HUD images exactly. Both SDL overlay comparisons
match the qualified SDL image and complete render JSON exactly.

`validation-provenance.json` records this correction, source comparison and
binary hashes. The retained initial qualification artifacts are **not** used
as guard evidence. Frozen timing native SHA256:
`7a7bba663aca54620f5b993ef2779f8867ed37cea0b31144ddede1df3fb03d40`;
guarded validation SHA256:
`43f4ad77520ea48c2c97dd48cfa0481815e524cc380d485a722150e02a37525f`.
The primary suite's exact loaded script is archived as
`Matched15\executed-run.py`; later renderer labels accurately describe the
initialization-profile toggle.

Verification: all six isolated default CTests pass; retained+grouping passes
adapter/full/replay/save-load CTests; nine tool/provenance/grouping tests pass.
Original Core and the qualified baseline binary remain unchanged.

### Independent parent confirmation

The parent independently verified all 230 process/frame counts and frozen
binary fingerprints, reran the nine tooling checks, and executed four
retained+grouping adapter/full-game/replay/save-load CTests. A fresh
`ParentQualification180` run confirmed default, retained HUD, grouping and
their combination have identical ordered draw states and exact scene/HUD
pixels; the SDL overlay also reproduced the qualified SDL scene exactly.

Five additional balanced rounds of the qualified SDL executable and frozen
native full/retained-HUD diagnostic executable produced:

| Complete host path | Median run-average CPU frame | Median run P95 |
|---|---:|---:|
| Qualified SDL | 2.289 ms | 2.876 ms |
| Native full HUD | 4.540 ms | 4.957 ms |
| Native retained HUD | 1.875 ms | 4.928 ms |

The retained candidate reduces native total elapsed time by a mean paired
58.76%, CI [-59.08%, -58.44%], and takes 19.45% less than SDL in this separate
batch, CI [-22.29%, -17.23%]. This independently confirms the large HUD-related
average-cost reduction; do not mix these absolute numbers with the primary
batch or claim an isolated engine speedup.

**Tail latency is not solved.** The retained candidate's P95 exceeds SDL's in
both the primary and parent batches despite its lower average. A shipping
acceptance criterion must include P95/long stalls and visible presentation/
input latency, not only mean throughput. Hidden fixed-HUD parity is not proof
of general viewport/DPI/theme/resource or interactive overlay invalidation.
The parent confirmation receipts remain under `ParentConfirmation5`.

## Remaining gaps / limits

* D3D12 versus D3D11 is unresolved; no unsupported compiler/backend is forced.
* SDL GPU timestamp queries are not available in this qualified workload.
* Native per-draw enqueue and SDL scene encode remain structurally different.
  SDL encode includes RmlUI context recording; native renderer-submit stats
  exclude swapchain flip. Neither is sold as a complete pure-engine span.
* Kernel/driver execution outside the process is not attributed by endpoint
  process times. No global ETW/WPR session, power plan or another user's process
  was changed.
* Registry roots/edges are measured, not every reachable allocation's traced
  byte size. STL/DLL/bgfx/GDI allocation counters are deliberately not conflated.
* Default benchmarks are no-input idle simulation with actual mobs/spawn.
  Replay, edits/streaming and save/load have correctness tests, not a fresh
  statistically powered interactive performance ranking.
* Retained HUD is a conservative hidden-run experiment; desktop reposition/
  repaint/font invalidation requires more tests. Queue3 final capture is not
  frame-exact. No experimental switch changes shipping defaults.
* Startup compilation and install relocatability remain separate tasks.
  Existing build-absolute asset paths are not an installer validation.
