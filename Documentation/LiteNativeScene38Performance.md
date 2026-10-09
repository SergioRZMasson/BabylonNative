# Original scene38 native qualification and performance

Authority: original Babylon Lite 1.32.0, source
`2e064d88ec7422af946f8ec7f089ac6519f99295`. Native baseline:
`bf6736826c55847ce6f69cb57d1172b176332471`, branch `babylon-lite-pure-native`.
This closes the preserved scene38 implementation; it is not a second emission,
dependency upgrade or restart.

## Preserved workload and executed qualification

Original corpus `scene38.ts` SHA256:
`e38a7be866dd6ce15b1d42c3204d6f2cf4db14f42a8e155c0fbb17d280b0e95c`.
Byte-preserved STANDARD user C++ SHA256:
`28ebc95c477aac0380cac4a1faa7a7539c306d0addd74a49027ba0b73f9a1e05`.
The single STANDARD emission, baseline executable, nine SDL GPU captures and
all 1,007 phase-one indexed files remain immutable.

All lanes retain the original 10 meshes/draws, 1,057 vertices, 4,527 indices,
11 distinct Standard materials (including the unused source material), Hemi
intensity 0.7 plus Directional diffuse `(0.9,0.9,0.9)`, and ArcRotate
`(-π/2,π/3,14)`, target zero, near 0.5/far 1000. Viewport is 1280×720 with
requested/default MSAA4. There are **no source controls or animation callbacks**.
Negative rotate/wheel/pan replays must not change camera, geometry or draw order.
The source Ribbon is back-facing with zero coverage, but remains a real submitted
mesh/draw; winding and culling are not changed for visibility or performance.

The 181-function C99 API adds exactly seven data/mesh pairs. All prior
167/97/515, 162/94/504, 160/93/498 and 122/72/389 signature/record/layout
witnesses are preserved. Strict C99 and C++17 consumption pass. Core retains
C-style free functions, POD and runtime allocation; RmlUI's private interface
exception does not extend to geometry.

Executed native and thin-JS tests classify all 99 original geometry fixtures:
98 are byte-exact and one jagged Ribbon is explicitly rejected because its
short UV data cannot fit unchanged `bl_GeometryData`. The expanded 289 cases
contain 235 supported and 54 explicit rejected-domain cases, including safe
low-count/fractional Disc/Tube, coercions, all 15 Polyhedron presets, caps,
closures, seams and Float32 overflow boundaries. All four output streams compare
exactly; eight Path3D fixtures compare pre-Float32 F64 stages exactly, including
duplicate/zero/reversal/vertical/epsilon cases. No geometry float tolerance was
needed. Per-allocation fault/retry, reentry, alignment, unchanged output,
10,000 churns per family, data-only dead stripping, GPU updates/capacity and
shared-scene submitted retirement are covered.

Unequal Ribbon rows, unsafe source frames/indices/nonfinite output and native
invalid cap enums fail explicitly. Tube `radiusFunction` remains unreached and
unsupported; JS rejects it without invocation. Other custom/instance/side/UV/
callback options are not newly implemented. This is not full procedural-package
coverage or a public Path3D API.

Both native lanes have actual GPU captures at static 1/61/121 and negative-input
12/61, with additional intermediate/confirmation captures retained. All 34
recorded native images have silhouette IoU 1 and interior RGB error ≤1 LSB
against the frozen reference. All nine SDL images are themselves byte-identical.
Native clear blue is 77 versus SDL 76; this is disclosed separately. One JS
rotate frame 12 has 20 temporal pixels differing by one LSB despite unchanged
geometry/light/material/transform state and no input effects; confirmations are
identical. Exact temporal GPU identity is therefore **not** claimed.

The unchanged STANDARD C++ frontend supplies one Directional component through
a Float32 user ABI, causing a one-ULP packed-Z difference from original TS.
The native Core entry matches the unchanged original light body for those actual
transported inputs; the JS entry is source-byte-exact. Actual C++/JS first GPU
images are identical. CPU JSON alone is not independent GPU proof.

Fresh UI-on/off CTest suites pass 19/13 cases and native Minecraft passes 10.
Forty-two prior Cube/Primitives/Scene2 GPU/input comparisons pass their
**unchanged historical** thresholds. A Cube JS frame 121 has one anomalous pixel
(maximum RGB error 27, hull IoU 0.999979); this remains disclosed, not described
as exact parity or used to relax scene38's stricter comparison.

LLVM22 checks pass on 43 formatted Core files/29 parsed translation units plus
eight style-checker tests. The eight new geometry modules also pass the existing
clang-tidy policy. Its retry ignores only unsupported CL-driver `/MP` and
`/Zc:preprocessor` tool arguments, not policy checks or native compiler flags.
The recovery build required no recompilation; preserved tests still describe
the measured binaries. Warnings-as-errors remain enabled.

## Fresh matched protocol

18 counterbalanced rounds × native C++/original JS/STANDARD SDL =
**54 fresh serial processes**, each permutation repeated three times.
Every child uses 180 warmup and 1,200 measured frames, Release builds,
real hidden swapchains, vsync/pacing off, and unchanged source workload.
There is no input, animation, capture/readback, file output, compilation or
dependency enumeration inside measured frames. All runs/outliers are retained.

The parent reported a quiet gate; all own correctness builds/tests were stopped
and no matching build/demo process was active at the explicit process check.
Shared-workstation load is not independently isolated. No global power,
ETW, queue-depth or renderer settings were changed. This uses the existing
bgfx D3D11 and SDL_GPU D3D12 backends. Separate device-capability queries are not
proof of the child-selected adapter, effective MSAA or device limits; those
unavailable child values remain null.

## Executed results

Values are medians of 18 per-process results, not pooled frame percentiles.
Confidence intervals bootstrap paired **run** ratios, not individual frames.

| Metric | Native C++ | Original JS/native | STANDARD SDL |
|---|---:|---:|---:|
| Mean complete host, ms | 0.263970 | 0.265675 | 0.341500 |
| P95 complete host, ms | 0.292150 | 0.299650 | 0.495000 |
| Process CPU/steady frame, ms | 0.247396 | 0.332031 | unavailable |
| Core + callbacks bracket, ms | 0.020381 | 0.043668 | unavailable |
| Submit/wait inner bracket, ms | 0.243836 | 0.197598 | unavailable |
| Valid unique-query GPU time, ms | 0.064675 | 0.064739 | unavailable |
| Startup to source engine Run, ms | 1126.673 | 1144.218 | unavailable |
| Whole process wall, ms | 1563.561 | 1600.273 | 1084.425 |
| Whole process CPU, ms | 1554.688 | 1703.125 | 906.250 |
| Private bytes | 145,653,760 | 163,805,184 | not queried |
| Executable bytes | 6,115,328 | 8,777,728 | 783,872 |

| Paired comparison | Mean change, 95% CI | P95 change, 95% CI |
|---|---|---|
| C++ versus SDL | **−22.603% [−23.976, −21.311]** | **−48.516% [−61.956, −35.581]** |
| JS/native versus SDL | −21.895% [−24.010, −19.581] | −47.478% [−61.017, −34.449] |
| JS/native versus C++ | +0.999% [−1.755, +4.200] | +3.770% [−3.781, +13.039] |

The combined C++ complete-host mean/P95 goal is met **for this scene38 batch**.
JS also improves both versus SDL; no established mean/P95 difference versus C++
is inferred from intervals crossing zero. SDL per-run P95 ranges from 0.334 to
2.025 ms; none of those results was trimmed or replaced.

Executable sizes are **not standalone shipping sizes**. The local JS deployment
files (EXE, six DLLs, ICU data and native-only bundle) total 49,110,827 bytes,
excluding OS/driver/redist prerequisites. The SDL EXE and four observed non-OS
DLLs total 3,645,952 bytes **before offline shader payloads and prerequisites**.
These are qualified file sets, not a redistributable package audit. Actual child
DLL paths/hashes and deployment inputs are retained; no private provider package
is copied or uploaded. C++ links no VM, SDL, generated engine or PAL.

## Interpretation and remaining limits

- Native submit/wait dominates its short complete loop. Core-only timing is
  never compared to SDL's total. Inner spans overlap outer work/GPU and are
  not added as exclusive phases.
- Native process queries/timers/sample retention remain instrumented.
  JS complete wall additionally includes VM dispatch/completion waiting.
  No uninstrumented algorithm estimate or profiling-overhead subtraction is
  manufactured; shorter native source is not causal performance evidence.
- D3D11/D3D12 scheduling/query/queue differences remain confounds. This task
  makes no renderer/tail mechanism change and claims no verified internal cause
  for SDL's tail distribution.
- Native runtime WGSL compilation remains arbitrary and outside Core.
  SDL uses frozen offline reference shaders: seven cache reuses, zero new
  compilations in preparation, not a new DXC-compilation claim. Native startup
  and whole-process CPU/wall are worse here; the steady-frame win is not an
  all-metrics win.
- GPU samples use valid, deduplicated query IDs per process; missing samples
  are null. SDL reports rounded mean/median/P95 only, with unavailable steady
  process/GPU/wait/startup fields null, not invented zeros.
- Existing V8 source override `8cd142951018de07c89c23f726fb7dc9c8374599`
  remains disclosed. RmlUI/FreeType/bgfx/Tint/SPIRV-Cross/root pins and compiler
  checkout source/historical dirty state are unchanged.
- Cube's previously published mean regression and Primitives' P95 regression
  remain **unmet combined goals**. Prior Scene2/Minecraft wins are separate
  batches, not evidence reused here or a universal-faster claim.

## Persistent evidence and reproduction

Parent acceptance independently reran 19 UI-ON, 13 UI-OFF and 10 native
Minecraft regression cases plus the 43-file/29-TU strict Core style gate.
Eight fresh C++/JS static/negative-input cases produced 26 repeated GPU images:
scene/geometry/light/material state matches the recorded qualification exactly,
and RGB differences remain within one LSB. The original-JS 181-function routing
audit passes. Before reruns, 5,274 distinct indexed source/binary/qualification/
benchmark fingerprints were verified; paired mean/P95 changes were independently
recomputed from all 54 process results.

The ignored independent `build\lite-c99\demo-scene38` tree contains:

- Immutable phase-one `handoff.json`/`artifact-index.json`, original bodies,
  licenses, canonical C++, frozen SDL binary/captures and parent approval.
- `implementation` scripts and `implementation-receipts` exact commands,
  exit codes, coverage, ABI, style, bundle/link and failure/retry evidence.
- `implementation-captures`, including actual GPU bytes, input effects,
  geometry/light/material/order state and observed module paths.
- `MatchedScene38_18`: all 54 process receipts/logs/raw native samples,
  protocol and paired-bootstrap summary. Reproduction scripts are
  `implementation\measure-scene38.py` and `implementation\summarize-scene38.py`;
  measurements refuse to overwrite this directory.
- Separate `final-handoff.json`, `final-artifact-index.json` and
  `final-source-and-dependency-index.json`, produced only after validation.

Retained failures include compiler-configuration attempts, the corrected
Float32 half-ULP boundary rejection, disclosed C++ light transport difference,
temporal one-LSB comparison failure and clang-tidy driver retry. They are not
discarded or relabeled as passing. The interrupted predecessor ended with an
HTTP unexpected-EOF infrastructure failure, not a new code/test result.

Measured binaries and earlier demo baselines remain immutable. Parent regression
reruns regenerate current-build test capture summaries; this does not replace
the frozen benchmark. A later implementation/backend experiment requires new
qualification and fresh matched measurements.
