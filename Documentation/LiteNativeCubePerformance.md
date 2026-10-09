# Auxiliary native cube: qualified functionality, performance target not met

The auxiliary bblitec `native-cube` sample now runs through the handwritten
LiteLayer from both identical standard-generated C++ and original JavaScript.
It qualifies the new camera/control/light/untextured Standard-material slice,
but **does not meet the performance goal against SDL/bblitec**.

This is a separate small, single-draw workload, not Minecraft or the registered
Babylon Lite `demo-cube`. Its unchanged user source rotates one lit box; it
does not attach camera controls. Control behavior has separate original-source
oracle and native/JS fixtures.

## Original application and rendering

The original auxiliary TS SHA256 is
`a1e901a9a8893016349d6e83176ee02899875b8ba1cadfbdfed3e41da277365e`.
One STANDARD emission produces `main.cpp`, SHA256
`5a773df02e410ba9831abc279d4bd915e68d05a4a02939e9619d5e72bf057bb7`.
The native C++ target compiles those bytes with external ABI headers and an
entry-name compiler definition. No application header was emitted. The JS
bundle changes only the engine import to the native shim; no JS engine or
generated native engine/PAL implementation is included.

Actual GPU captures at frames 1/61/121 match camera, material/light/geometry and
rotation state. C++ silhouette intersection-over-union is 1.0 at all poses,
with no interior color differences over two 8-bit levels. Native clear-blue
rounding is 77 versus SDL's 76; backend MSAA rounding and float C++ versus
double JS accumulation are not claimed bit-exact. The first update has zero
delta, followed by fixed `1000/60` ms steps.

The reviewed API now has 160 functions. All original 122 signatures and 389
binary size/offset witnesses across 72 original records remain unchanged.
Parent independently reran 19 UI-ON, 13 UI-OFF and 10 native Minecraft regression
cases, strict LLVM22 formatting/AST checks and new cube C++/JS rendering and
JS semantic fixtures. Original Minecraft measurements/binaries remain frozen.

## Fresh matched result

Fifteen balanced rounds, 45 processes, 180 warmup + 1,200 measured frames each.
1280x720, MSAA4, original camera/light/material/rotation, hidden real swapchains,
no measured input/capture/file output or concurrent builds. No outliers removed.

| Cube path | Median run-average wall frame | Median per-run P95 |
|---|---:|---:|
| Standard bblitec / SDL_GPU D3D12 | **0.3030 ms** | **0.3810 ms** |
| Native C++ / LiteLayer / bgfx D3D11 | 0.3353 ms | 0.5743 ms |
| Original JS / V8 / same native Core | 0.3598 ms | 0.6448 ms |

Native C++ versus SDL has mean paired change **+12.58%**, bootstrap 95% interval
**[+7.80%, +19.52%]**: slower beyond the 5% target. JS versus SDL is +16.94%,
interval [+13.31%, +20.16%]. JS versus native C++ is +4.71%, interval
[-0.96%, +8.97%]: no established practical equivalence or speedup.
These use round pairs, not ratios of table medians or pooled frames.

The median-mean absolute C++ gap is about **0.032 ms**. Small absolute cost
does not excuse the failed relative target or worse P95. This result must not
be combined with the larger Minecraft batch or presented as a universal
native speedup.

## Bottleneck evidence and rejected experiment

Native C++ Core/user work is approximately **0.00622 ms/frame**; native
submission/handoff/wait is **0.32908 ms/frame**. Real GPU queries average
approximately **0.03003 ms**. The dominant observed span is backend handoff/
backpressure, not the camera/material algorithms. SDL's summary does not expose
equivalent process/GPU phases, so this does not isolate a backend-independent
cause or prove an engine speedup.

A separate six-round alternating experiment used bgfx's supported same-API-thread
renderer handoff with unchanged workload/shaders and reproduced the GPU poses.
It did not reliably improve C++ and worsened JS. **It was not adopted**, and
does not replace the original 45-process result. No verified general fix for
the fixed-cost gap is established yet.

## Scope and reproducibility

Core supports ArcRotate data/original pointer/touch inertia, live options,
raw/helper limit semantics, hemispheric light and untextured Standard shading.
Unsupported keyboard, Standard color geometry, textures/PBR/other lights/
shadows/filters/framegraph/material views/viewport/orthographic behavior fails
explicitly. This is not full package or corpus coverage.

Sources are in `Apps/LiteTests/NativeCube`; configure with the qualified
`LITE_CUBE_STANDARD_DIR` and `LITE_CUBE_BBLITEC_DIR` values plus native test,
shader-service and JS-binding targets. Use a new build tree.
Ignored evidence is `build/lite-c99/demo-native-cube/final-handoff.json`,
`MatchedCube15/summary.json`, per-run receipts/commands, actual loaded-module
inspection, original/emitted source snapshots and `HostHandoffExperiment`.
The SDL reference resolves its actual adjacent dependencies from the existing
vcpkg bin path; its 707,584-byte EXE alone is not a deployment-size measurement.
The parent verified 541 indexed source/binary/config/evidence fingerprints and
recomputed the paired result before accepting functionality.
