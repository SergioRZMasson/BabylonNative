# Native flat Ground/primitives qualification

This task qualifies the unchanged **bblitec-owned auxiliary** `examples/primitives.ts`,
SHA-256 `af87a595fdb4af02bf9bd98f86dc2081f038946899c8fbe1ed907aa6b17b26c4`.
It is not an original Babylon Lite corpus demo or all-primitives coverage.
Core algorithm authority remains original Lite 1.32.0 TypeScript at
`2e064d88ec7422af946f8ec7f089ac6519f99295`.

## Functional scope

The approved addition is `bl_GroundOptions`, `bl_createFlatGroundData` and
`bl_createGround`, bringing the only public C99 header to 162 functions.
The helper retains its original module-exported name, not an invented root
`createGroundData` alias. All 160 prior signatures and 93 record layouts/498
binary witnesses remain unchanged; the original 122-function, 72-record/389-witness
contract also remains unchanged.

Ground uses handwritten free functions/POD and the runtime allocator. It preserves
the original independently allocated mutable typed arrays, double-to-Float32
arithmetic, winding/upward normals and two-pass UV scaling/signed zero. Null/zero
options retain defaults. Selected nonfinite values, nonintegral/sub-one subdivision
counts, Float32 overflow and unsafe sizes are explicit native rejections, not
clamps. Finite zero/negative dimensions/scales remain representable.
Heightmap min/max options are ignored by the original flat factory; no heightmap
implementation is implied.

The unchanged STANDARD user C++ translation unit, SHA-256
`831f7a2323bc12a70ed998d0b357685fbdecb8bab0099d8d600721b3be8fa5eb`, runs through
a new external **multimesh** transport. The cube's one-mesh adapter is not reused.
Original JS imports are remapped only to the thin native shim. Both lanes retain
box 24 vertices/36 indices, Ground 4/6, independent materials/geometry, one
hemispheric light, ArcRotate camera and attached idle controls.

Actual GPU qualification compared both native lanes at static frames 1/61/121
and rotate/wheel/pan frames 12/61 against frozen STANDARD SDL captures. All 18
comparisons have silhouette IoU 1.0. Static images are byte-identical; input-pose
maximum RGB differences are one LSB. Camera/control scalars match executed original
TypeScript exactly. Actual first-zero-delta sequences, queued replay alignment
and real Win32 capture/release are checked separately from timing.

Fresh independent builds passed 19 UI-ON, 13 UI-OFF and all 10 native Minecraft
regressions. Native Minecraft's first idle180 verifier lacked its immutable SDL
oracle; staging the unchanged oracle and rerunning that case resolved the
infrastructure failure. Six Ground CPU cases cover seven original byte goldens,
independent backing, selected-field guards, signed zeros, every cold allocation
failure, churn, foreign tokens, wrong-thread/reentry and misaligned allocation.
GPU tests preserve copied-data, independent-buffer, GPU-only/complete update and
bounds behavior. Cube C++/JS GPU and live-control/material semantics also pass.
LLVM 22 format/compiler-AST checks, strict C99 binary ABI, data-only linker maps
and native-only JS/source/routing audits pass. Earlier measured binaries and
receipts remain untouched.

Parent acceptance independently reran the 19 UI-ON, 13 UI-OFF and 10 native
Minecraft regression cases, strict Core LLVM22 format/AST checks, original-JS
162-function routing audit and all eight C++/JS static/input cases. The repeated
captures retain exactly the recorded scene/control state: static pixels match
exactly, input images remain within the existing one-LSB tolerance. A strict
byte comparison initially detected five one-LSB pixels at rotate12; scene state
was identical, and the published input tolerance was not increased.
The parent verified 798 distinct indexed file fingerprints and all 160 prior
signatures, and independently recomputed the final paired average/P95 changes.

## Fresh matched comparison

Fifteen serial balanced rounds (45 fresh processes), 180 warmup and 1,200 measured
frames/process, fixed delta 1000/60 ms, 1280×720 and original/default MSAA4.
No input replay, screenshots/readback, pacing, concurrent builds, workload/guard
reduction or outlier deletion occurs during measured frames. Camera controls
remain attached, as authored. Statistical units are paired run means/P95s, not
independent frame samples.

| Lane | Median run-average ms | Median run P95 ms | Executable bytes |
|---|---:|---:|---:|
| C++ / C99 / bgfx-D3D11 | 0.403307 | 2.3262 | 5,985,792 |
| Original JS / C99 / bgfx-D3D11 | 0.407813 | 2.4175 | 8,691,200 |
| STANDARD SDL_GPU / D3D12 | 0.443000 | 2.1090 | 704,512 |

Executable sizes are **not whole deployment sizes**; actual runtime DLL paths,
hashes and link graphs are indexed independently.

Paired C++ average change versus SDL is **−10.03%**, 95% bootstrap interval
**[−11.69%, −8.60%]**. JS average change is −9.58% [−11.08%, −8.19%].
However, paired C++ P95 change is **+10.92%** **[+8.55%, +13.07%]**;
JS P95 change is +15.60% [+13.25%, +17.82%].
Thus average complete-host throughput improves, but **the combined average/P95
performance goal is not met**. This is not a universal native-faster claim.

C++ median observed Core/user bracket is 0.00730 ms versus 0.39611 ms
submission/handoff/wait, with approximately 0.15334 ms real GPU execution.
The earlier retained batch's tail-frame means are about 2.988 ms total,
0.00868 ms Core/user and 2.979 ms
submission/wait. This locates the observed tail span; it does not prove a specific
driver/queue/presentation cause or justify bypassing fences or changing source
quality. No speculative renderer experiment is adopted. The previously published
tiny-cube backend-overhead goal remains separately unmet.

## Interpretation and receipts

D3D11/D3D12 backends, threading/queue behavior and runtime-versus-offline shader
startup remain confounds. Native C++ startup median is 388.31 ms and JS 410.49 ms;
SDL's standard summary does not expose an equivalent startup/GPU/wait/steady
process-CPU breakdown. Missing values are null, not measured zeros.
Whole-process wall/CPU receipts include startup, warmup, measured frames and
shutdown; they are not steady CPU/frame measurements.

Native Core brackets include the first GetProcessTimes query and full wall excludes
the final query/sample storage. JS complete wall includes VM dispatch/completion
wait; nested Core/submit categories overlap and must not be added as exclusive
costs. Valid GPU query IDs are deduplicated. SDL summary times are rounded to
0.001 ms. The shared workstation/power policy is unchanged.

Reproduction, source/binary/configuration/DLL fingerprints, original-source
goldens, actual GPU images, tests/ABI/link graphs, raw 45-process measurements,
confidence intervals, failure/retry receipts and scope limits are retained in the
ignored `build/lite-c99/demo-primitives/` tree. See `final-handoff.json`,
`MatchedPrimitives15Final/summary.json`, `receipts/native-pixel-comparison.json` and
`receipts/tail-span-attribution.json`. No Minecraft/cube timing is reused.
An earlier same-binary 45-process batch, retained in `MatchedPrimitives15`, found
−10.44% average change but +9.57% P95 change. The final batch followed the expanded
mesh allocation-failure checks and final rebuilt test suites. Both batches retain
all processes/outliers; the conclusion is not selected by discarding the earlier
P95 regression.
