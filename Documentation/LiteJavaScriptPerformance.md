# Original JavaScript on LiteLayer with retained RmlUI

The original Minecraft JavaScript now runs through LiteJSBinding against the
handwritten C99 engine and retained RmlUI/bgfx UI. The measured JavaScript host
is within the 5% practical mean-frame-time threshold of the native C++ host,
and both beat the SDL/bblitec reference in this fresh workload. This does not
mean that JavaScript has no execution or deployment cost.

## Qualified implementation

- The original 27-file TypeScript slice is unchanged. Application-only
  bundling excludes the JavaScript engine implementation and bblitec runtime.
  The four C++ paths retain the same standard-generated application sources.
- The VM creating thread owns bgfx and all C99 calls. The engine is BORROWED;
  normal frames perform 3D, retained UI update/render and one host presentation.
  Window/message ownership remains on the OS thread.
- Native identity/type/status forwarding, parent-wrapper GC, image copying,
  stale handles, textbox/change/click transport and original callback exception
  preservation are exercised. Controls/audio, save/load, toast expiry,
  resize/DPI and combined GPU captures are qualified.
- Parent independently reran all 19 UI-ON and 13 UI-OFF integration tests,
  new-host formatting/tidy, original-source/API routing and patch audits.
  Source routing covers 122 current C99 functions, not all package exports.
- The existing clean JsRuntimeHost source override at
  `8cd142951018de07c89c23f726fb7dc9c8374599` is reused, with V8 11.9.169.4.
  This differs from root FetchContent pin
  `33e4a233dea178712352ec90a647683084bfbf1d`; the default-pin build is not
  independently qualified by this cached configuration.

The UI is a narrow native DOM/CSS transport, not a browser. Unsupported
layout/bounds, fonts, transitions, colored difference, clipboard/IME and
asynchronous event-handler behavior are disclosed in `BabylonLite.md`.
Retained UI is the default when enabled; the explicitly selected legacy GDI
path and UI-OFF builds preserve the earlier host behavior.

## Fresh matched five-way comparison

**Do not merge these figures with the earlier MSAA1 C++ batch.** The JavaScript
host retains source-default MSAA4, so all five paths were newly qualified and
measured at MSAA4. Actual combined captures and reference camera/geometry
observations were collected before timing; no application sources were edited.

Fifteen balanced rounds, five paths, 75 processes, 180 warmup + 1,200 measured
frames per process: 90,000 measured frames. SEED1337/radius6, 1280x720, fixed
`1000/60` ms simulation step, hidden real swapchains, no input/vsync/pacing/
measured captures or receipt writes. Agents/builds were idle during timing.
The workstation remains Xeon W-2235/Quadro P620; external system-load isolation
is not claimed. All runs were retained.

| Original full-UI path | Median run-average wall frame | Median per-run P95 |
|---|---:|---:|
| Standard C++ / SDL_GPU D3D12 | 2.767 ms | 3.784 ms |
| Standard C++ / stock Dawn D3D12 | 5.614 ms | 7.453 ms |
| Standard C++ / compatible bgfx WebGPU provider | 3.313 ms | 4.362 ms |
| Standard C++ / handwritten LiteLayer + RmlUI / bgfx D3D11 | **1.521 ms** | **2.290 ms** |
| Original JS / V8 + LiteJSBinding / same native Core + RmlUI | **1.544 ms** | **2.185 ms** |

These are medians of run averages/P95, not pooled frame percentiles. Paired
bootstrap uses 10,000 resamples of the 15 round pairs:

| JS host versus | Mean paired frame-time change | Bootstrap 95% interval |
|---|---:|---:|
| SDL/bblitec | -42.90% | [-45.16%, -40.51%] |
| Stock Dawn | -72.15% | [-73.18%, -71.05%] |
| Compatible provider | -53.70% | [-54.46%, -52.92%] |
| Native C++ host | +2.34% | [+0.73%, +3.96%] |

The JS/native C++ mean interval fits inside the practical +/-5% band.
JS P95 is also lower than SDL in this batch: paired -40.70%, interval
[-43.85%, -37.31%]. Against native C++ its paired P95 interval is
[-5.07%, +0.87%]: no established P95 difference. Native C++ mean versus SDL
is -44.20%, interval [-46.22%, -41.88%].

This batch has less variability than the earlier MSAA1 batch, but is still one
workstation/workload. D3D11 versus D3D12, renderer threading, OS/VM dispatch,
glyph rendering and timing/GC boundaries remain different. Shader compilation
and boot are outside steady-state measurements. Full rendering is not bit-exact:
the native JS/C++ combined frame180 capture is 83.41% RGB-exact with mean
absolute channel difference 0.2438 on the 0..255 scale. Original source identity
does not prove identical clocks, floating-point behavior or renderer pixels.
Private JS world/hash/chunk observations remain unavailable, not fabricated.

## Execution cost versus wall time

| Median native-path measurement | JS host | C++ host |
|---|---:|---:|
| Complete frame wall time | 1.544 ms | 1.521 ms |
| Whole-process execution per frame | **2.565 ms** | **1.953 ms** |
| Retained UI update/layout/host transport | 0.0468 ms | 0.0149 ms |
| Retained UI draw recording | 0.0771 ms | 0.0662 ms |
| Presenter/handoff wall span | 0.3896 ms | 0.9390 ms |
| Render-thread submit wall span | 0.2873 ms | 0.2680 ms |

JS callbacks inside the native frame average 0.5517 ms, including native setter
calls. The enclosing native frame/callback span is 0.8710 ms. These nested wall
spans are not isolated JS CPU or complete native engine CPU. Whole-process
execution can exceed elapsed time because work runs on multiple threads.
Windows execution counters are scheduler-quantized; render-thread submit is
wall time, not execution CPU. Do not sum overlapping spans.

The longer JS work reduces time waiting at the presenter: similar wall frame
times do **not** imply no VM/binding overhead. CPU-bound demos still need their
own comparisons.

All 18,000 JS measured frames pass configuration/frame/input/resource guards.
The three UI contexts retain 51 draws, 53 live geometries and 25 live textures
at the measurement boundaries. Geometry compile, texture creation and upload
counters change zero times during every measured JS window. There are 17,787
distinct real GPU queries; missing/repeated queries remain null. Median
run-average queried GPU duration is 1.359 ms.

## Binary footprint and evidence

The JS host EXE is 8,788,992 bytes, with 29,595,136 adjacent V8/ICU/support DLL
bytes: **36.61 MiB combined**. The pure C++ host remains **9.15 MiB**, including
its statically linked runtime shader compiler and no adjacent DLLs. These
exclude assets/fonts/shader files/system runtimes and are not minimized
shipping packages. The optional scripting path has a substantial runtime
footprint; it is not required for native C/C++ consumers.

Frozen local evidence is under `build/lite-c99/js-rmlui`:
`parent-source-audit.json`, `parent-msaa4-qualification.json`,
`ParentQualificationMsaa4`, `benchmark-profile-msaa4.json`,
`Matched15Msaa4/measurements.json`, `summary.json` and all 75 logs/raw receipts.
Binary/DLL fingerprints, actual shader-cache patch identity and the original
27 source hashes are preserved. The statistical runner is the investigation's
`Experiments/Mincraft/tools/benchmark_native.py`, with explicit
`js-rmlui-receipt` and C++ formats; the candidate is never silently substituted.

The next work is individual demo qualification, starting with the native cube
sample and its reviewed ArcRotate/hemispheric-light/standard-material contract.
This milestone is not complete Babylon Lite API/corpus coverage, arbitrary
runtime GLB loading or partner-owned resource import.
