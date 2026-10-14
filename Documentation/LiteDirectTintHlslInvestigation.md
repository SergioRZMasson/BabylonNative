# Direct Tint HLSL runtime compiler investigation

**Direct Tint HLSL reduced uncached compiler-service lifecycle time by
7.69-15.59% across all eleven measured profiles, and the matched linked probe
was 693,760 bytes smaller (12.77%).** This is a promising native D3D11 compiler
alternative, not a production replacement, GPU performance result or complete
model-loading benchmark.

The accepted loader, public C99 ABI, provisional production compiler and manual
sandbox remain unchanged. This report follows the separate
[cache/reduced-Tint investigation](LiteRuntimeShaderInvestigation.md).
No cache is used in either arm of this comparison.

## Actual route and supported scope

The current route is:

`WGSL -> Tint SPIR-V -> SPIRV-Cross HLSL -> FXC -> native reflection/link/package`

The experimental route is:

`WGSL -> Tint core IR -> Tint HLSL in FXC mode -> FXC -> native reflection/link/package`

Both use Tint `a21a4a1c7c497e6366947ccaefbab768d16f32a8`, the same system
FXC/D3DReflect and the existing native result ABI/bgfx v12 container contract.
The direct route does not execute the SPIR-V writer or SPIRV-Cross HLSL compiler;
the actual linked map confirms the corresponding code paths are absent.
It is not raw DXBC relabeled as a bgfx package.

This required more than selecting a writer:

- Preserve original logical resource/member metadata before lowering.
- Merge logical uniform blocks into one actual core-IR `b0` root and rewrite
  typed uses, including uniform pointers and whole-aggregate loads.
- Add a dependency-local vertex-input semantic hook for bgfx geometry inputs
  and instance TEXCOORD31..28, without changing WGSL locations or interstage IO.
- Reflect real texture/sampler operations and active native layouts, then retain
  stage linking, result ownership and paired release.

The proof covers the captured supported material/service subset. All 49
captured cases retain their expected success/error statuses and logical
interfaces; own accepted native outputs remain exact before and after timing.
Different correct native code and warning text are allowed between routes.
The existing contract permits signed/unsigned texture `LOAD` with or without
a backend-generated `DIMENSIONS` robustness query; that narrow difference is
recorded, not hidden by dropping arbitrary metadata.

Actual native qualification includes six GLB drag/drop loads, embedded
textures/environment/instances/UV1/multiple materials, Minecraft/RmlUI rendering,
uniform-mutation GPU readback and pointer/texture-helper requests. That is
interface/resource and relevant rendering evidence, **not full numerical or
pixel-perfect equivalence**.

A review found a higher-alignment root-placement hazard. Guarded v3 explicitly
returns **BL_UNSUPPORTED for uniform-block alignment above 16 bytes before
constructing invalid IR**. Sixteen active `@align(32/64)` cases cover direct
accesses, pointer/whole loads, service1/service2 and repeated paired releases.
Diagnostics identify the unsupported block/alignment; there is no assertion
crash, silent omission or fallback to the old compiler. Wider layout support
remains outside this prototype's qualified scope.

## Matched measurement

Windows 11 x64, Xeon W-2235, MSVC 14.51.36231, Release
`/O2 /Ob2 /MD /DNDEBUG /EHsc`, `/OPT:REF /OPT:ICF /INCREMENTAL:NO`, no LTO.
WGSL validation, IR assertions and dumping controls remain enabled. Both arms
use full-archive configurations; a minimal-direct dependency closure is unbuilt.

Both executables link the **same validation-driver object** and consume
byte-identical request streams, including labels, options and instance
declarations. Each has independently accepted exact native-result goldens.
The shared guard serializes all result metadata/diagnostics/containers, verifies
native package/stage links, performs native D3DReflect checks and releases the
result. Corrupting either arm's golden fails the comparison.

Eleven successful profiles each have twelve matched independent process rounds,
six AB and six BA: **264 performance children**. Another 96 children provide
timer-on/off and pipe/NUL controls with the full native guard always enabled;
twelve correctness-only children run outside the timing phase. All 372 exited.
The four expected errors in the 49-case corpus and sixteen alignment regressions
are never included in successful compilation timings.

The primary metric is **compile callback plus full validation/reflection/release
lifecycle**. Callback and guard spans are reported separately. Whole-process
wall time additionally includes input/golden loading, CRT/DLL setup, all declared
warmup, output/drain and shutdown. Cold means a fresh process with OS-warm
file/DLL caches, not machine-boot cold.

Warm chains and warm64 profiles measure **uncached recompilation** after an
explicit prefix on both arms, never cache hits. Only the declared warm service
metric excludes that prefix; whole-process CPU/wall/memory include it.

## Compiler-service results

Mean lifecycle totals in milliseconds:

| Captured profile | Current | Direct v3 | Change |
|---|---:|---:|---:|
| Ordered GLB chain, 20 requests across six loads | 885.481 | 755.347 | -14.70% |
| Ordered Minecraft/RmlUI chain, 15 requests | 463.013 | 397.405 | -14.17% |
| 26 distinct successful identities, first pass | 718.834 | 623.690 | -13.24% |
| GLB second chain, after 20-request warmup | 874.159 | 751.052 | -14.08% |
| Minecraft/RmlUI second chain, after 15-request warmup | 460.998 | 392.617 | -14.83% |
| PBR first request, fresh process | 61.533 | 51.940 | -15.59% |
| PBR 64 recompilations, after one warmup | 3,747.155 | 3,250.710 | -13.25% |
| Instanced first request, fresh process | 66.923 | 58.380 | -12.76% |
| Instanced 64 recompilations, after one warmup | 4,123.040 | 3,628.090 | -12.00% |
| RmlUI gradient first request, fresh process | 49.649 | 43.827 | -11.73% |
| RmlUI gradient 64 recompilations, after one warmup | 2,929.823 | 2,704.386 | -7.69% |

Paired direct-minus-current lifecycle difference 95% intervals are
[-150.187, -114.838] ms for the GLB chain,
[-71.591, -59.811] ms for Minecraft/RmlUI,
[-111.813, -72.871] ms for distinct first-pass identities, and
[-11.890, -7.902] ms for first-use PBR. All eleven lifecycle and whole-process
wall intervals favor direct in this batch.

For the GLB chain, mean callbacks are 884.021 -> 754.205 ms; full postcallback
guard/release is 1.460 -> 1.141 ms. Minecraft/RmlUI callbacks are
462.126 -> 396.722 ms and guard/release is 0.887 -> 0.683 ms.
The gain is not merely a validation-boundary change.

Whole fresh-process wall means are 904.907 -> 774.780 ms for the GLB chain,
482.416 -> 416.280 ms for Minecraft/RmlUI, and 738.058 -> 643.290 ms for the
distinct first pass. These are compiler-probe process costs, **not complete
sandbox startup or first-visible-frame latency**.

Unlike the earlier cache experiment, direct shows a measured benefit on
distinct first-use shaders. However, **do not combine percentages or compare
absolute totals against that older batch**: the old cache driver had different
validation work. No direct-plus-cache arm or combined saving was measured.

## Linked footprint and whole-process resources

| Same shared-driver executable | Bytes | MiB |
|---|---:|---:|
| Current, uncached | 5,433,344 | 5.182 |
| Direct v3, uncached | 4,739,584 | 4.520 |

Direct saves **693,760 bytes, 677.5 KiB, 12.77%**. These are actual linked
full-closure prototype endpoints, not source/header/archive sizes, a rebuilt
shipping sandbox, engine-only size or an optimized minimal-direct package.
System FXC and the shared MSVC runtime remain prerequisites.

Means of whole-child CPU and OS peak working-set counters:

| Profile | Current CPU, ms | Direct CPU, ms | Current peak WS, MiB | Direct peak WS, MiB |
|---|---:|---:|---:|---:|
| Ordered GLB chain | 882.813 | 764.323 | 16.091 | 14.342 |
| Ordered Minecraft/RmlUI | 472.656 | 411.458 | 15.688 | 14.542 |
| Distinct first pass | 721.354 | 635.417 | 16.007 | 14.837 |

CPU uses GetProcessTimes whole-child counters; their 100 ns units are not an
effective-resolution guarantee. Post-exit GetProcessMemoryInfo peak working-set
and peak-pagefile fields are retained separately. Working set includes shared
pages and all child setup/input/compiler/guard activity. These are not precise
Tint/private-heap live peaks, allocator attribution or cache-retention counters.

## Interpretation and decision boundary

Confidence intervals use 20,000 paired process-round resamples, seed 1337.
All valid runs/outliers are retained. Calls within a process are correlated,
not independent samples. Profiles/metrics are exploratory correlated questions,
not multiplicity-adjusted confirmatory guarantees. P95 describes only twelve
process aggregates, not a robust individual-shader tail or SLO.

Known viewer/build/compiler activity probes stayed clear. Shared unrelated
workstation, OS and thermal activity was not isolated; no global power,
priority, affinity or timer settings were changed. Timer/output control
intervals cross zero in this batch. They do not separately calibrate QPC,
formatting or per-call overhead, and no noisy subtraction was applied.
No phase-clock overlay exists: the saved time cannot be quantitatively assigned
to Tint, SPIRV-Cross removal, HLSL shape or FXC optimization.

**Conclusion:** direct Tint HLSL is the strongest executable translation
alternative tested so far for this bounded D3D11 service corpus. It improves
unique cold compilation as well as repeated uncached work and reduces the
matched probe footprint. It is a candidate for a separately approved opt-in
integration, **not an automatic production-default replacement**.

Before production selection, preserve/extend required layout/resource coverage,
review the dependency-local hook/ownership, and evaluate shader numerical/GPU
behavior and actual application startup/first-frame costs in the intended
scope. GLB parsing/codecs, material source composition, environment preparation,
bgfx GPU program creation, uploads and GPU FPS were not measured here.
Source-perfect images, full glTF/WGSL/backend support and exact anisotropy remain
unqualified; the minimum native loader milestone is unaffected.

## Evidence

Root:
`build\lite-c99\shader-strategy-investigation\experiments-direct-guards-v3`.
The frozen batch is `measurements-direct-v3-quiet-01`; preparation includes
`frozen-matched-arms.json`, `source-delta-manifest.json`,
`comparison-measurement-plan.json` and the alignment/native qualification
receipts. The batch retains raw stdout/stderr/process records, schedules,
before/after integrity and correctness, controls, resource counters and scripts.

The parent independently verified **61 frozen input hashes, 20 report hashes,
all 264 raw runs and 528 stdout/stderr hashes**, each run's lifecycle/callback/
guard/wall/CPU/memory aggregates, schedule balance and **154 paired bootstrap
intervals**. `parent-direct-measurement-verification.json` records that audit.
The separately attributed 96 overhead controls and native qualification
receipts are retained; no measured cache/v2 artifacts were overwritten.
