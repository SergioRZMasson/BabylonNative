# LiteScenes — M0/M1 compatibility and measurement host

Opt-in Windows host for scenes **159, 161, 163, 168, 200, 247, 248, 249,
256, 257, 258, 259, 260 and 266**, plus the twelve M1 scenes below.
`LiteScenesNative` runs handwritten
scene-user-code ports using only `babylon_lite.h`. `LiteScenesJs` bundles the
unchanged TypeScript scene scripts against `Plugins/LiteJSBinding`; its receipts
reject engine-JavaScript inputs. Neither target includes private Core headers.
The Win32/bgfx presenter, WIC decoding, VM, instrumentation and test tools are
outside Core. Existing scene2/35/38 hosts are unchanged.

**M0 is not complete.** Golden failures and missing native contracts are not
silently treated as implemented APIs or performance passes.

## M1 implementation and executed coverage

All six batches have native implementation and JS forwarding: affine-preserving
parenting/cloning and matrix helpers, torus/host-decoded heightmaps, grid/lines,
colored/dynamic thin instances and GPU culling, alpha coverage, floating origin,
orthographic projection and host-transported free controls.

The approved 64/80-byte bgfx slot packing preserves the original GPU culler,
64-thread dispatch, atomic compaction and indexed-indirect draw. Ordered buffer
copies upload persistent raw storage without changing bgfx. Runtime compute
compilation/reflection is an additive shader service. Native Graphics' float
palette clear approach is enabled by an explicit host slot lease; no authored
clear color or parity threshold is altered. See `babylon_lite.h` for ownership,
atomic-error, sharing and native-host configuration contracts.

MAD values below are **full/foreground RGB 0..255**, not normalized:

| M1 scene | Native batches exercised | Native parity MAD | JS parity MAD |
|---|---|---|---|
| 16 | colored instances | PASS .00003979/.00011677 | PASS .00003979/.00011677 |
| 43 | colored instances, native callbacks | PASS .00012080/.00035791 | PASS .00012297/.00036239 |
| 162 | torus geometry | PASS 0/0 | PASS 0/0 |
| 165 | shader instance colors | PASS .00000723/.00003982 | PASS .00000723/.00003982 |
| 201 | high-precision floating origin | PASS .00027633/.00039786 | PASS .00028176/.00040567 |
| 204 | instances and floating origin | PASS .00034975/.00042827 | PASS .00035409/.00043358 |
| 213 | grid material | PASS .00013636/.00295270 | PASS .00015408/.00324797 |
| 268 | live orthographic projection | PASS .00012731/.00057836 | PASS .00012731/.00057836 |
| 270 | mirrored parenting/transforms | PASS .00009946/.00078914 | PASS .00009838/.00078053 |
| 274 | alpha-to-coverage and sample metadata | PASS 0/0 | PASS 0/0 |
| 278 | colored line topology | PASS 0/0 | PASS 0/0 |
| 279 | line updates and colored instances | PASS 0/0 | PASS 0/0 |

The final Release/V8 build passes 79/79 root CTests and 10/10 fresh native
Minecraft suites. Existing scene200 FP and 249/257 anisotropy wrappers remain;
their passing CTest status is not a new strict pixel pass. The M1 forwarding
test covers live bounds, F64 storage, clone identity, dynamic raw writes/counts,
error rollback and native free controls. CPU/GPU tests additionally check
original byte goldens, clone allocator failure/churn, actual compute output,
reset/dirty updates, index storage and floating-origin culling.

Full body/scene deltas, the exact additive ABI inventory, benchmark commands,
raw balanced pairs and separate startup observations are retained under
`E:\Github\Babylon-Lite-Native-AI\artifacts\api-m1-v1\implementation-v1`.
Performance is accepted only when both mean/P95 paired 95% CI upper bounds
are <=1.0 after 15 pairs of 120 warmup + 2000 measured frames.

**Executed steady-state result: 6/24 lanes accepted, 15 slower, 3 inconclusive.**
The three accepted scenes are **162, 278 and 279**, in both languages.
The complete 360-pair/720-process results and all confidence intervals are in
`SUMMARY.md`/`summary.json`; startup remains separate. Native43 and Native/JS274
are inconclusive, not passes. Every other nonaccepted lane is slower by at least
one required gate. Profiling all 18 nonaccepted lanes places 97.2–99.9% of their
P95-tail envelope in submit/backpressure; exact backend attribution remains open.
No threshold, source workload, compiler route or bgfx implementation was changed
to hide these failures. **M1 is not performance-qualified.**

**Scope still requiring parent review:** the original bake API is Gaussian-
splatting-only (M10), not an ordinary-mesh bake. Bound CPU-skin clones and
protected imported-topology reparenting remain explicit unsupported cases.
Source 1.25/1.32 capacity-draw padding is recorded separately rather than silently
adapting the corpus. Clone-shared geometry rejects ordinary in-place updates;
resize detaches, while thin data remains shared.

The older feasibility-stop artifacts and historical M0 sections below remain
immutable evidence, not the current M1 implementation status.

## Approved-policy continuation after7412ea7b

The five reviewed C99 inspection functions are implemented with native negative,
stale/capacity/OOM tests, strict header-consumption checks and JS forwarding tests.
Collections retain native membership/order/identity, including duplicate entries;
JS mutation through inspection arrays fails explicitly. Generic node wrappers
are promoted without replacing their identity when a container query reveals a
mesh. Returned mesh wrappers retain their container's ownership.

LiteLayer now enables bgfx's existing D3D11 staging configuration by default.
The full scene and Minecraft suites are tested against the fresh
`build\lite-scenes-m0-v4` build. Scene200 uses a controlled-evidence expected
failure wrapper; it still fails on process errors, absent metrics or an
unexpected pixel pass. Mean/P95 steady-state frame ratios alone determine
performance acceptance under the approved M0 policy. Startup and unavailable
GPU/latency remain separate reported observations.

**Known anisotropy gap (scenes 249 and 257):** the pinned bgfx API has no numeric
device-wide maximum setter. `BGFX_RESET_MAXANISOTROPY` chooses 1 or 16 on D3D11,
not the requested 4. A 16x-only capture control still fails scene 249's foreground
gate (.11610 vs .05); the 4x-only control passes (.00994). The user chose to record
this as a known gap rather than patch bgfx (2026-11-02). Both parity tests keep
their thresholds and are marked `WILL_FAIL` with the `known-gap` label, so they
start failing once parity passes and the gap must be revisited. The default
effective maximum remains 1.

Final executed tables, paired Minecraft regression and commands are retained
under `api-coverage-batch1-v1\approved-final-v4`. Earlier sections below remain
historical v2/v3 evidence.

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

The 52 serial CTest gates use the registry's exact extent, RGB MAD units
(**0..255**, not normalized), thresholds, and reference-only Euclidean
foreground mask. They reject extent mismatches, not crop images. Captures,
diffs, process logs and numeric receipts are retained even on failure. Separate
tests cover the statistics/mask/capture parser and real NAPI animation,
visibility, invalid-option, wrong-kind and disposed-handle forwarding.

## Measurement protocol and limits (initial v2 evidence)

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
  initially generated asset mips on the CPU, whereas the original source uses
  GPU render-pass mip generation. This pre-existing source-placement difference
  is not hidden by a passing screenshot.

The strict acceptance gate requires upper paired mean **and** P95 ratio CIs
≤1.0, matching work, parity, GPU and latency qualification. Missing metrics,
failed golden gates or mismatched workload are never rounded into a pass.
Use `--minimal-timing` for a separate phase-timer-overhead control; it does not
disable bgfx's own GPU queries.

## Projection scope and remaining failures (initial v2 evidence)

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

## Executed coverage (initial post-sync v2 evidence)

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

## Continuation after parent commit 5680aaed

The fresh `build\lite-scenes-m0-v3\build` Release/V8 build corrects scene256's
owned mip-generation defect. Both original pins use GPU fullscreen-triangle
linear blits; the earlier CPU averaging changed lower-mip texels despite
byte-identical base levels. Core now uses the original WGSL algorithm, mip-only
texture views, hardware sRGB conversions and regenerated mips on partial updates.
It retains no CPU mip chain. A focused GPU test checks hardware rounding,
partial updates, callback updates and view-budget rejection before mutation.
Related asset tests compare every physical mip to an independent D3D11 driver's
`GenerateMips`, not to Lite's own CPU/GPU implementation.

Necessary private embedding adaptation for review: scenes occupy the high end
of each engine's existing granted view range, leaving lower views for ordered
mip prelude jobs. Already-reserved environment decode views remain disjoint.
Outside frames, existing submitted-work retirement drains setup before reusing
these views. An insufficient view budget fails explicitly; no host-reserved view
is used. No public ABI changed.

The remaining failures have controlled replay evidence, not merely suspected
sampler/compiler differences:

* **200:** uploaded Float32 camera matrices match. Changing only captured VS
  arithmetic from FXC's Y-first accumulation to DXC's X-first fused accumulation
  gives exact **0/0** MAD. This is a backend arithmetic-order difference, not
  a documented 1.25→1.32 workload change. No scene-specific shader substitution
  or global IEEE flag is shipped.
* **249/257:** changing only authored all-linear 2D samplers from effective
  anisotropy1 to source-requested4 passes the original gates. IBL samplers are
  deliberately unchanged. Exact per-sampler integration requires separate
  policy review; no platform sampler override, global anisotropy or bgfx edit
  is shipped.
* **256:** replacing only the captured lower mips with baseline GPU-generated
  texels passes; the production GPU fix now passes native and JS CTest gates.

Scene247's balanced replay counters confirm identical teapot primitive and
pixel work, U32 indices, 64-byte/four-float4 instance strides and instance
counts958/941. Native vertex records are32 bytes versus baseline96. Native's
plane-first order matches both original source pins; baseline draws it last.
The plane has316,087 versus219,688 passing samples, but teapot pixel work is
equal. Counter timings are **replay-only**, never live benchmark samples.
There is no justification to reorder the native hierarchy to mimic baked
baseline ordering.

`map_projection_milestones.py` maps all34 previously unresolved entries.
Later features retain M1/M2/M3/M4/M5/M6/M13 ownership rather than fake JS fields.
Writable `useHighPrecisionMatrix` metadata now matches the original: matrix
storage is chosen at engine creation and is not changed by later metadata
writes. Its JS test covers subsequently created Float32 transforms.
Six genuinely M0 inspection entries need five reviewed C99 queries:
container/scene meshes, scene lights, mesh bounds and material name. These
remain explicitly blocked on contract approval; the public header is unchanged.

### Actual Present and event-inclusive baseline overlays

`PresentProbe` is a private outside-Core observer of the process's actual DXGI
`Present`/`Present1` calls. Process creation FILETIME is calibrated to QPC with
recorded uncertainty. Receipts distinguish any setup Present from the first
Present after CPU source-frame readiness. That tag does not establish exact
GPU source-frame/Present association across bgfx's queued rendering. This is
a hidden-window API-call completion measurement,
**not** display/photon time. Events use preallocated memory; output occurs only
after measured loops. Factory/swapchain callback failures are contained and
invalidate the receipt.

`prepare_baseline_probe.py` copies only PAL host files into ignored storage,
compiles those copies and relinks untouched original engine/main objects. It
never builds or edits the bblitec checkout. Absolute read-only asset/shader
directories preserve its baked workload. Both copied PALs bracket events
through submission/frame cleanup and write full-precision samples afterward.
All28 instrumented SDL_GPU/Dawn golden preflights pass.

```powershell
$v3 = 'build\lite-scenes-m0-v3'
$hosts = "$v3\build\Apps\LiteTests\LiteScenes"
python Apps\LiteTests\LiteScenes\prepare_baseline_probe.py --corpus E:\Github\bblitec --output "$v3\baseline-probe-final" --scenes 159,161,163,168,200,247,248,249,256,257,258,259,260,266
# Run each generated scene<id>\build.cmd in the same cmd.exe as vcvars64.bat.
python Apps\LiteTests\LiteScenes\formal_benchmark.py preflight --hosts "$hosts" --data "$hosts\data" --manifest "$v3\baseline-probe-final\manifest.json" --output "$v3\baseline-probe-preflight"
python Apps\LiteTests\LiteScenes\formal_benchmark.py benchmark --hosts "$hosts" --data "$hosts\data" --manifest "$v3\baseline-probe-final\manifest.json" --output "$v3\formal-perf-v2"
```

The new runner uses15 balanced pairs in every scene/language/backend lane,
120 warmup and2,000 measured frames. Each receipt records host, original
baseline, overlay, probe and JS-script hashes. Do not mix the original370
pairs, invalid-marker smoke or GPU replay counters with these measurements.
Native shader-service time is separate; baseline runtime PSO creation remains
included in startup but not separately instrumented. PresentMon was attempted
without elevation and failed ETW access checks. Live baseline GPU/latency and
exact first-correct-frame association remain missing; the runner never promotes
a numerical throughput win to complete qualification.

`observer_overhead.py` measures fifteen on/off pairs for native, JS, SDL_GPU and
Dawn in the short-frame scene159. All four complete-host mean/P95 CIs include1;
the Core/callback brackets are also reported. This characterizes overhead, not
proof that it is zero.

`prepare_memory_probe.py` supplies a reproducible **isolated** experiment with
bgfx's existing `BGFX_CONFIG_RENDERER_DIRECT3D11_USE_STAGING_BUFFER=1` option.
No vendor source or default build is changed. Changing only captured vertex/
index buffer usage from DYNAMIC/CPU-write to DEFAULT/no-CPU-access preserves
exact pixels and improves replay GPU time to0.9236 [0.9058,0.9403]; physical heap
residency was not measured. The supported-option build then passes all24
graphics tests and retains the same22/28 native/JS golden gates. Sixty matched
Native/SDL pairs turn all four default-build timing failures (200/247/256/258)
into numerical mean/P95 passes. This global backend option requires parent
review and complete-batch/Minecraft qualification before becoming the default.

See the evidence's `continuation-v3` section and per-scene JSON for executed
values. Historical v2 measurements above remain historical, not current passes.

JS SDL timing-only results are slower for scenes200/247/266 and inconclusive
for 259/260. All native/JS Dawn timing-only CIs meet the numerical gate.
The complete four-lane tables, actual per-run samples, profiling and unresolved
qualification blockers remain in the evidence, not hidden by these summaries.
Phase-timer overhead controls were also run: their short-scene CIs are noisy
and do not establish an isolated uninstrumented-Core result.


## Continuation v3 executed coverage

Startup tags are not exact GPU correct-frame association. All full acceptance remains unqualified; the staging option is isolated, not the default.

|Scene|Native MAD|JS MAD|Native/SDL startup ms|Native startup ratio [CI]|Native mean ratio [CI]|Native P95 ratio [CI]|Native verdict|JS/SDL startup ms|JS startup ratio [CI]|JS mean ratio [CI]|JS P95 ratio [CI]|JS verdict|
|---|---|---|---:|---|---|---|---|---:|---|---|---|---|
|159|P 0/0|P 0/0|305.0/637.8|0.4800 [0.4506,0.5089]|0.7627 [0.7478,0.7767]|0.2502 [0.2199,0.3099]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|306.6/600.5|0.5123 [0.4904,0.5316]|0.7337 [0.7243,0.7448]|0.2075 [0.2004,0.2154]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|
|161|P 0/0|P 0/0|290.9/586.4|0.4968 [0.4768,0.5132]|0.7449 [0.7355,0.7540]|0.2214 [0.2153,0.2276]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|308.9/597.0|0.5188 [0.4998,0.5354]|0.7141 [0.7014,0.7268]|0.1975 [0.1883,0.2058]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|
|163|P 0/0|P 0/0|290.0/609.1|0.4778 [0.4565,0.4995]|0.7585 [0.7475,0.7708]|0.2240 [0.2170,0.2329]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|306.8/572.8|0.5360 [0.5242,0.5452]|0.7633 [0.7539,0.7723]|0.2156 [0.2082,0.2237]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|
|168|P 0/0|P 0/0|447.5/639.6|0.7005 [0.6876,0.7138]|0.7230 [0.7121,0.7331]|0.2271 [0.2189,0.2358]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|497.6/671.7|0.7301 [0.6615,0.8185]|0.7657 [0.7200,0.8368]|0.3355 [0.2345,0.5089]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|
|200|F 0.3114847/0.4538918|F 0.3114923/0.4539029|2813.6/789.1|3.5718 [3.4470,3.7088]|1.1166 [1.0996,1.1335]|0.9701 [0.9532,0.9886]|slower; reject-parity|2802.3/745.5|3.7592 [3.7226,3.7974]|1.1386 [1.1273,1.1472]|1.0067 [0.9939,1.0162]|slower; reject-parity|
|247|P 0/0|P 0/0|519.4/700.1|0.7435 [0.7066,0.7787]|1.0483 [1.0371,1.0599]|1.0325 [1.0190,1.0462]|slower; slower|536.6/675.9|0.7947 [0.7700,0.8149]|1.0354 [1.0340,1.0370]|1.0164 [1.0105,1.0229]|slower; slower|
|248|P 0.002094184/0.01176431|P 0.002086227/0.01171844|940.6/733.8|1.2842 [1.2404,1.3173]|0.7573 [0.7462,0.7671]|0.2661 [0.2553,0.2783]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|963.7/729.7|1.3217 [1.2845,1.3596]|0.7339 [0.7277,0.7404]|0.2389 [0.2309,0.2472]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|
|249|F 0.01669018/0.7224153|F 0.01638527/0.7092179|1134.9/718.0|1.5845 [1.5290,1.6317]|0.7629 [0.7531,0.7730]|0.2724 [0.2650,0.2808]|equal-or-faster; reject-parity|1154.9/701.3|1.6482 [1.6108,1.6749]|0.7247 [0.7172,0.7318]|0.2350 [0.2287,0.2419]|equal-or-faster; reject-parity|
|256|P 0.0001584201/0.001260933|P 0.0001616753/0.001292369|525.1/821.8|0.6401 [0.6209,0.6546]|0.8894 [0.8796,0.8997]|1.1586 [1.1457,1.1714]|slower; slower|541.8/800.6|0.6768 [0.6708,0.6824]|0.8966 [0.8871,0.9051]|1.1813 [1.1689,1.1940]|slower; slower|
|257|F 0.007727865/0.06149744|F 0.007727865/0.06149744|499.2/712.5|0.7028 [0.6754,0.7270]|0.8067 [0.7947,0.8171]|0.2651 [0.2585,0.2728]|equal-or-faster; reject-parity|520.3/694.0|0.7508 [0.7267,0.7702]|0.7692 [0.7140,0.8098]|0.2811 [0.2407,0.3487]|equal-or-faster; reject-parity|
|258|P 2.170139e-06/2.931253e-06|P 2.170139e-06/2.931253e-06|391.4/647.5|0.6058 [0.5842,0.6247]|0.9457 [0.9395,0.9516]|1.0476 [1.0385,1.0572]|slower; slower|480.3/691.9|0.6795 [0.6165,0.7632]|0.9576 [0.8344,1.0804]|1.0572 [0.9161,1.1777]|inconclusive; inconclusive|
|259|P 0/0|P 0/0|397.1/650.5|0.6078 [0.5761,0.6546]|0.7576 [0.7227,0.7912]|0.6276 [0.6025,0.6602]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|433.0/650.9|0.6635 [0.6335,0.6995]|0.7601 [0.7303,0.7855]|0.6638 [0.6467,0.6836]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|
|260|P 0/0|P 0/0|378.7/643.4|0.5905 [0.5670,0.6116]|0.7659 [0.7546,0.7762]|0.6114 [0.5959,0.6283]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|405.9/623.2|0.6512 [0.6251,0.6796]|0.7846 [0.7723,0.8031]|0.6470 [0.6260,0.6768]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|
|266|P 0.0005342159/0.0009069778|P 0.000521195/0.0008848732|965.9/694.2|1.3947 [1.3421,1.4380]|0.7934 [0.7878,0.7995]|0.6236 [0.6110,0.6369]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|993.4/671.0|1.4806 [1.4674,1.4940]|0.7909 [0.7808,0.7983]|0.6278 [0.6175,0.6372]|equal-or-faster; unqualified-gpu-latency-first-correct-frame|

### Isolated supported-option performance fix (not the default build)

Changing only captured vertex/index buffer usage from DYNAMIC/CPU-write to DEFAULT/no-CPU-access preserves0/0 pixels and gives GPU replay ratio0.9236 [0.9058,0.9403]. Physical heap residency was not measured. No vendor source was edited.

The existing bgfx staging option was then compiled in isolation. All28 native/JS gates retain the same22 passes, and all24 graphics tests pass. Sixty additional matched Native/SDL pairs:

|Scene|Mean ratio [CI]|P95 ratio [CI]|Timing verdict|
|---|---|---|---|
|200|0.9320 [0.9257,0.9386]|0.6321 [0.6193,0.6449]|equal-or-faster|
|247|0.9134 [0.9016,0.9237]|0.9021 [0.8922,0.9121]|equal-or-faster|
|256|0.7156 [0.7011,0.7313]|0.1958 [0.1763,0.2347]|equal-or-faster|
|258|0.7838 [0.7772,0.7910]|0.6109 [0.5953,0.6290]|equal-or-faster|

This global backend build option needs parent review and complete-batch/Minecraft qualification; it is not silently enabled or substituted for the default840 pairs.

All native/JS Dawn values, raw samples, startup/shader spans and identities are in `summary.json`/`formal-perf-v2`.
