# LiteScenes — M0 compatibility and measurement host

Opt-in Windows host for scenes **159, 161, 163, 168, 200, 247, 248, 249,
256, 257, 258, 259, 260 and 266**. `LiteScenesNative` runs handwritten
scene-user-code ports using only `babylon_lite.h`. `LiteScenesJs` bundles the
unchanged TypeScript scene scripts against `Plugins/LiteJSBinding`; its receipts
reject engine-JavaScript inputs. Neither target includes private Core headers.
The Win32/bgfx presenter, WIC decoding, VM, instrumentation and test tools are
outside Core. Existing scene2/35/38 hosts are unchanged.

**M0 is not complete.** Golden failures and missing native contracts are not
silently treated as implemented APIs or performance passes.

## Source and assets

The workload is bblitec's frozen Lite **1.25.0** corpus, pin
`286525f8041dd9adc72b2c9962e8bff4d9aeb764`. Engine behavior follows Lite
**1.32.0**, `2e064d88ec7422af946f8ec7f089ac6519f99295`.
`trace_sources.py` compared all fourteen official 1.32 lab scripts: eleven are
unchanged; scenes159/161/163 only added the identity `wgsl` template tag/import.
The shared HPM builder used by scene200 is also byte-equivalent after line normalization.
No scene workload or shader was changed to obtain a better image.

`prepare.py` verifies audit source/resource/executable hashes. Native and JS
load **raw original** glTF/GLB, buffers, images and environment files, not
bblitec's preprocessed GLBs. A generated URI map implements the offline document
roots and exact URL-to-file resolution; no engine behavior depends on an
asset name/hash. Nothing downloads or publishes model assets during a test.
The original user-code/WGSL source is Apache-2.0; its license is retained at
`Core\LiteLayer\LICENSE.txt`.

The audit's description of scene247 as “one instanced draw” is incorrect for
the actual script/asset: it contains **two** instanced teapot batches (958 and
941 instances, 12,096 indices each) and a six-index plane. The host retains all
three draws. RenderDoc captures confirm this and 4× BGRA8/D32F attachments for
every native scene. The native importer expands scene260's strip to a six-index
triangle list, as documented by its existing C99 importer contract; do not
claim identical strip-pipeline/index traffic.

## Configure and validate

Use a **fresh** ignored directory after the upstream sync (`4685f480`), Ninja,
VS2026 and V8. The earlier `lite-scenes-m0-v1` evidence is pre-sync only.
Keep `vcvars64.bat` and the build in the same `cmd.exe` process. Supply the
existing exact-pin Tint libraries/source, RmlUI and FreeType overrides where
required by those components. Do not reuse the obsolete external
JsRuntimeHost/UrlLib/bgfx wrapper source overrides.

```powershell
& $env:ComSpec /c 'call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake -S . -B build\lite-scenes-m0-v2\build -G Ninja -C build\lite-scenes-m0-v2\initial-cache.cmake -DNAPI_JAVASCRIPT_ENGINE=V8'
& $env:ComSpec /c 'call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\lite-scenes-m0-v2\build --target LiteScenesNative LiteScenesJs LiteScenesStyleCheck --parallel 3'
ctest --test-dir build\lite-scenes-m0-v2\build -R '^LiteScenes' --output-on-failure --parallel 1
ctest --test-dir build\lite-scenes-m0-v2\build -E '^LiteScenes' --output-on-failure --parallel 1
```

The evidence's `initial-cache.cmake` is the executed machine-specific cache
recipe. Required options:

* `BABYLON_NATIVE_BUILD_LITE_LAYER`, `BABYLON_NATIVE_BUILD_LITE_SHADER_COMPILER`,
  `BABYLON_NATIVE_BUILD_LITE_ASSET_DECODER`, `BABYLON_NATIVE_PLUGIN_LITEJSBINDING`,
  `BABYLON_NATIVE_BUILD_LITE_TESTS`, `BABYLON_NATIVE_BUILD_LITE_SCENES`: `ON`.
* `BABYLON_LITE_ENABLE_GLTF`, `BABYLON_LITE_ENABLE_STYLE_CHECKS`: `ON`.
* `LITE_SCENES_CORPUS`: read-only bblitec checkout.
* `LITE_SCENES_AUDIT`: `artifacts\api-coverage-v1` directory.
* Python with the existing Pillow and NumPy test dependencies; Node/esbuild
  from LitePlayground's existing dependency installation.

The 28 serial CTest gates use the registry's exact extent, RGB MAD units
(**0..255**, not normalized), thresholds, and reference-only Euclidean
foreground mask. They reject extent mismatches, not crop images. Captures,
diffs, process logs and numeric receipts are retained even on failure. Separate
tests cover the statistics/mask/capture parser and real NAPI animation,
visibility, invalid-option, wrong-kind and disposed-handle forwarding.

## Measurement protocol and limits

```powershell
$hostDir = 'build\lite-scenes-m0-v2\build\Apps\LiteTests\LiteScenes'
node Apps\LiteTests\LiteScenes\verify-baseline.mjs E:\Github\bblitec "$hostDir\data" build\lite-scenes-m0-v2\baseline-identity.json
python Apps\LiteTests\LiteScenes\harness.py benchmark --host "$hostDir\LiteScenesNative.exe" --data "$hostDir\data" --scene 159,161,163,168,200,247,248,249,256,257,258,259,260,266 --backend sdl_gpu --pairs 5 --output E:\Github\Babylon-Lite-Native-AI\artifacts\api-coverage-batch1-v1\perf\native-sdl_gpu
```

Repeat with `LiteScenesJs.exe`, and both languages with `--backend dawn`.
The runner preserves immutable existing pairs only when executable hashes,
scene and backend match. It alternates AB/BA, starts at five pairs and expands
close (within 10%, **not** an acceptance tolerance) or inconclusive comparisons
to fifteen. Ratios and percentile-bootstrap 95% CIs use **run pairs**, never
individual frames. The native first zero-delta frame counts toward the 120
warmup frames; 2,000 following frames are measured at delta 1000/60 ms.
Profiling arrays are preallocated. Captures and asset IO/shader compilation
are forbidden in measured frames. JS GC and projection frame processing remain
enabled. No builds or GPU correctness runs overlap the serial performance run.
The host canvas retains registered listeners strongly, so ignored control
detachers do not permit premature GC/finalization of live native controls.
Earlier JS measurements from before this host-lifetime fix are preserved under
`superseded-platform-retention` and excluded. Native executables/workloads were
unchanged. Current JS pairs also record the exact scene-bundle hash.

**These are diagnostic ratios, not qualified equal-or-faster results:**

* Native timing includes event pumping through `bgfx::frame`, including
  enqueue/backpressure; bblitec's existing bracket omits event pumping.
* Native records deduplicated real bgfx GPU query IDs, render-thread elapsed
  intervals (including waits), and actual process CPU consumption. The raw
  `renderThreadCpuMs` field is bgfx CPU-wall time, not consumed thread CPU.
  Baseline GPU, latency and raw frame samples are
  unavailable; its per-run mean/P95 are rounded to .001 ms.
* `setupFromMainMs` and a first-frame GPU-completion upper bound are **not**
  process-start-to-first-present. Actual first-present/startup ratios remain
  `null`; do not derive them by subtracting loop time from process duration.
  Native shader service, file-read and image-decode spans are separate.
* bgfx/D3D11 differs from SDL_GPU/D3D12 and Dawn/D3D12 in compiler, queues and
  presentation model. SDL's shaders and repackaged assets were baked offline;
  native loads raw assets and compiles WGSL at runtime. The existing production
  Tint/SPIRV-Cross/FXC bridge remains unchanged by this batch.
* Source-requested anisotropy can be 4; actual captured native samplers are 1.
  Exact per-sampler anisotropy remains explicitly deferred by policy. Core also
  currently generates asset mips on the CPU, whereas the original source uses
  GPU render-pass mip generation. This pre-existing source-placement difference
  is not hidden by a passing screenshot.

The strict acceptance gate requires upper paired mean **and** P95 ratio CIs
≤1.0, matching work, parity, GPU and latency qualification. Missing metrics,
failed golden gates or mismatched workload are never rounded into a pass.
Use `--minimal-timing` for a separate phase-timer-overhead control; it does not
disable bgfx's own GPU queries.

## Projection scope and remaining failures

No `babylon_lite.h` additions were made. M0's 57 audit entries include 13
TypeScript-only types, nine existing scoped forwarders, one added visibility
alias, four partially covered entries and thirty entries blocked by missing
native contracts/features. The exact per-entry disposition is in
`source-review\projection-coverage.json` in the evidence directory.

Added forwarding exposes native container clip identities/metadata,
speed/loop and play/pause/stop/seek, preserving container ownership while a
group remains referenced. Direct time/playing writes, target metadata and
animation-manager/mixing support are not fabricated. Engine precision exposes
its creation value only; incompatible MSAA, nonempty WebGPU limits, DPR and
floating-origin requests fail explicitly rather than silently disappear.

Missing contracts include scene collections/image processing, mesh bounds/
picking/morph state, material names/UV transforms, bone control/XMP, generic
container mesh enumeration and copy/resolve frame-graph tasks. These require
native feature work and reviewed ABI changes, not independent JS shadow state.

The remaining golden failures are scene200's HPM-off raster differences and
the stricter foreground gates for scenes249/256/257. Actual 4× attachments and
correct draw workloads have been checked. Sampler/mip differences are observed;
they are **not** a proved attribution of every differing pixel. Thresholds,
camera, geometry and shader math have not been weakened to mask the failures.
Scene200's actual native uploaded view-projection bytes match the fresh
bblitec Float32 matrix exactly; that narrows the discrepancy downstream of that
upload, but does not prove complete shader numerical equivalence.

The source-equivalent native C++ Minecraft retained-UI projection was also
rebuilt in `build\lite-c99\rmlui-minecraft\NativeBatchM0Sync`, with the same
post-sync bgfx/bx/bimg sources and current shader compiler archives. All ten
existing suites passed (including 1,380 frames, replay, save/load, resize,
underwater, toast expiry, UI boundary and translation tests). The root build's
20 existing suites and all six legacy scene-host smokes also passed.

Executed per-scene values and all four timing lanes are published in the
batch evidence's `summary.json` and `SUMMARY.md`; this milestone must remain
open until its failed gates and native-contract blockers are resolved.

## Executed coverage (post-sync)

MAD is full/foreground in 0..255 units; P/F is the actual golden gate. Ratios
below are **diagnostic native / SDL_GPU** mean and P95, with paired 95% CIs.
Startup observations are mean native setup-from-main / shader-service ms,
not actual first-present times. Every startup ratio and every formal
performance-acceptance verdict remains **unqualified/N/A**.

| Scene | Native gate, MAD | JS gate, MAD | Native setup/shader ms | Mean ratio [95% CI] | P95 ratio [95% CI] |
|---|---|---|---:|---:|---:|
| 159 | P 0/0 | P 0/0 | 277.7/9.4 | .737 [.685,.795] | .386 [.283,.563] |
| 161 | P 0/0 | P 0/0 | 272.8/10.4 | .499 [.411,.605] | .166 [.105,.263] |
| 163 | P 0/0 | P 0/0 | 260.8/10.6 | .410 [.397,.430] | .093 [.087,.103] |
| 168 | P 0/0 | P 0/0 | 403.3/128.3 | .409 [.403,.413] | .093 [.091,.095] |
| 200 | F .311492/.453903 | F .311485/.453892 | 2803.0/2516.4 | .700 [.658,.742] | .487 [.448,.529] |
| 247 | P 0/0 | P 0/0 | 582.7/243.5 | .986 [.967,1.002] | .901 [.769,.985] |
| 248 | P .00237486/.0132468 | P .00237775/.0132635 | 1117.7/656.6 | .515 [.399,.619] | .314 [.176,.597] |
| 249 | F .0166808/.722008 | F .0163845/.709187 | 1984.4/1434.2 | .597 [.473,.806] | .307 [.163,.590] |
| 256 | F .0163411/.156960 | F .0163437/.156984 | 1141.3/84.9 | .692 [.595,.780] | .557 [.328,.772] |
| 257 | F .00772244/.0615595 | F .00772244/.0615595 | 504.9/157.3 | .637 [.620,.658] | .166 [.159,.174] |
| 258 | P .00000217/.00000293 | P .00000217/.00000293 | 358.4/69.6 | .789 [.771,.804] | .677 [.648,.707] |
| 259 | P 0/0 | P 0/0 | 346.0/69.9 | .791 [.781,.805] | .676 [.654,.702] |
| 260 | P 0/0 | P 0/0 | 341.7/68.5 | .780 [.771,.789] | .672 [.626,.723] |
| 266 | P .000533492/.000905596 | P .000536386/.000911122 | 942.1/642.8 | .778 [.748,.801] | .650 [.634,.662] |

There are **370 valid pairs / 740 serial processes**: native SDL 80, JS SDL
120, native Dawn 90 and JS Dawn 80 pairs. Native SDL scene247 is numerically
inconclusive, not “equal within a tolerance.” Its observed Core/callback span
is 0.0243 ms (0.43% of complete host), submit/backpressure is 5.634 ms, and
GPU duration is 5.563 ms; these overlapping spans must not be summed. No
justified parity-preserving Core optimization was identified for that
GPU/handoff-bound result.

JS SDL timing-only results are slower for scenes200/247/266 and inconclusive
for 259/260. All native/JS Dawn timing-only CIs meet the numerical gate.
The complete four-lane tables, actual per-run samples, profiling and unresolved
qualification blockers remain in the evidence, not hidden by these summaries.
Phase-timer overhead controls were also run: their short-scene CIs are noisy
and do not establish an isolated uninstrumented-Core result.
