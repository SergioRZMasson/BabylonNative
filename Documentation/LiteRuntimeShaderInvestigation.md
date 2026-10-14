# Native runtime shader strategy investigation

**Reusing successful compiler results reduces repeated shader work in the
captured model-loading and Minecraft/UI request chains. A smaller same-pin Tint
configuration reduces the linked probe by about 297 KiB, but does not establish
a consistent compilation-speed improvement.** These are isolated experiments,
not production changes or measurements of complete model loading/rendering.

The accepted native loader and manual sandbox remain at
`88063cddba3a5953d33172f3e87a7cf1f5a69783`, with the same provisional compiler.
No cache, dependency configuration, shader language or renderer has been selected
for production by this investigation.

## Current pipeline and comparison scope

The outside-Core service currently uses:

`WGSL -> Tint SPIR-V -> SPIRV-Cross HLSL -> system FXC -> D3DReflect/link -> bgfx containers`

This includes active uniform/texture/instance reflection, original logical
uniform offsets, consolidation into the bgfx D3D11 `b0` storage, stage linking,
owned metadata and compatible v12 shader containers. Raw DXBC alone is not a
replacement for the service. Its production-qualified backend is D3D11/FXC;
other backends are not established by these results.

The captured corpus has **49 complete requests**, including six actual GLB
loads, environment shaders, Minecraft, RmlUI and service tests. There are 45
successful requests and four expected errors. All four experimental arms
reproduce the full captured results before and after measurement, including
metadata, diagnostics and native container bytes. This is the supported corpus,
not all WGSL, glTF materials or graphics backends.

The four arms use the same MSVC 14.51 toolset, Release `/O2 /Ob2 /DNDEBUG`,
shared CRT `/MD`, `/OPT:REF /OPT:ICF /INCREMENTAL:NO` and no LTO:

| Arm | Compiler configuration | Result cache |
|---|---|---|
| Full/current | Same-pin full Tint control, rebuilt with the matched toolset | None |
| Full/cache | Same full control | Success-only, bounded, immutable result entries |
| Reduced/current | WGSL reader/inspector and SPIR-V writer closure | None |
| Reduced/cache | Same reduced closure | Same cache |

The reduced closure disables the unused HLSL/MSL writers, SPIR-V reader and
WGSL writer. Validation, IR assertions and IR dumping controls remain enabled.
The Tint revision is `a21a4a1c7c497e6366947ccaefbab768d16f32a8`; SPIRV-Cross is
`7c8705749fc1e234119c20695f82179b30d90914`. None of these arms is a direct
Tint-to-HLSL compiler. Older MSVC 14.44 Tint archives are excluded from the
matched dependency-configuration comparison.

The cache retains successful translation outputs, not GPU handles or programs.
It deep-copies containers, metadata arrays and strings into immutable entries,
and gives each invocation its own mutable result/release token. It checks full
key bytes after hashing and includes source, entries, stage, ABI/backend,
instance declarations, labels and compiler/options/layout fingerprints.
Failures remain uncached. Held results survive entry eviction and cache clear.

## Measurement method

Windows 11 x64, Xeon W-2235, six cores/twelve logical processors. The batch uses
12 independent four-arm process rounds for each of 11 successful-request
profiles: **528 performance children**. Each arm occupies each position three
times; each pair is ordered AB six times and BA six times. Another 192 processes
provide timed/untimed and pipe/NUL controls, with 20 correctness-only invocations.
All 740 children exited. Known viewer/compiler/build activity probes stayed
clear; unrelated shared-workstation load and thermal effects were not isolated.
No power, priority, affinity or system-cache settings were changed.

The primary metric sums **compile callback plus postcompile validation/release**
for the declared measured requests. This matters because a cache miss releases
the underlying provider inside its callback; comparing callbacks alone would
put that work on different sides of the timer. Whole-process wall time also
includes setup, input reads, declared warmup, stdout formatting/draining and exit.
It does not represent a running sandbox's model-load latency.

"Cold" means a fresh process with an empty result cache, **not machine-boot-cold
DLL/file caches**. Warm profiles retain an explicit first request/chain as
warmup. That prefix is excluded only from the reported warm service metric,
and remains included in whole-process measurements.

Intervals resample matched process rounds, 20,000 times with seed 1337.
Individual shader calls inside one process are not independent samples.
All valid runs and outliers are retained. Six pair comparisons share the same
cells and are exploratory, not independent, multiplicity-adjusted experiments.
Any reported P95 describes only twelve process aggregates, not a reliable
individual-request tail or latency SLO.

## Results: repeated requests versus new shaders

Full-configuration mean compiler-lifecycle milliseconds:

| Captured workload | No cache | With cache | Mean change | Paired difference 95% CI, ms |
|---|---:|---:|---:|---:|
| Ordered GLB chain, 20 requests, initially empty cache | 880.654 | 312.661 | -64.50% | [-574.602, -561.794] |
| Ordered Minecraft/RmlUI chain, 15 requests, initially empty cache | 461.104 | 358.051 | -22.35% | [-109.558, -96.382] |
| 26 distinct successful identities, initially empty cache | 714.199 | 720.515 | +0.88% | [-2.840, +15.374] |
| First PBR request in a fresh process | 60.781 | 60.739 | -0.07% | [-1.242, +0.986] |
| First instanced request in a fresh process | 66.462 | 67.071 | +0.92% | [-0.617, +2.243] |
| First RmlUI gradient request in a fresh process | 49.226 | 50.203 | +1.98% | [-0.748, +2.912] |

A negative difference favors the cache. The ordered GLB chain spans six loads
and includes repeated complete request identities; it is **not a 64.5% reduction
in the first load of every model**. The Minecraft/RmlUI chain includes two UI
context initializations. Unique/first-use rows do not establish a speedup or a
zero-overhead cache: their intervals include both gains and regressions.

The reduced closure produces similar chain behavior: GLB 875.872 -> 315.073 ms
and Minecraft/RmlUI 463.718 -> 359.388 ms with cache.

Whole fresh-process wall means for the full control are 899.196 -> 331.679 ms
for the GLB chain and 479.822 -> 376.309 ms for Minecraft/RmlUI. Those are probe
process costs, not complete application startup or first-visible-frame times.

### Warm reloads

After replaying the same chain as warmup, the second GLB chain takes
883.356 ms uncached versus **0.182 ms instrumented cached lifecycle total**
for all 20 requests. The second Minecraft/RmlUI chain takes 467.826 versus
0.124 ms for 15 requests. A 64-request PBR repetition after one warmup takes
3,709.329 versus 0.709 ms.

These observations support reuse rather than repeated translation. They are
**not faster cold compilation**. Sub-millisecond hit timings include result
cloning, validation/release and timer overhead; they are not independently
calibrated pure lookup/clone latency. Timed/untimed controls toggle QPC reads
and extra number formatting together, and pipe/NUL controls change transport.
Their noisy process deltas cannot be causally separated or subtracted per hit.
The initial compilation/warmup cost still exists.

### Reduced compiler closure

Compared uncached against the matched full control, the reduced closure's GLB
chain mean changes by -0.54%, with a paired difference interval
[-13.724, +3.568] ms. Minecraft/RmlUI changes by +0.57%, interval
[-2.676, +7.782] ms. There is **no consistent timing win** across profiles.
Keep the adverse result: the first RmlUI gradient request is about 4.67% slower
with the reduced closure, interval [+0.723, +4.665] ms.

No phase-clock overlay was built. These totals cannot identify how much time
Tint, SPIRV-Cross or FXC individually consumes.

## Linked footprint and memory

Actual minimal-driver executable lengths, with the matched compiler/link flags:

| Arm | EXE bytes |
|---|---:|
| Full/current | 5,423,616 |
| Full/cache | 5,439,488 |
| Reduced/current | 5,119,488 |
| Reduced/cache | 5,134,336 |

The uncached reduced closure saves **304,128 bytes (297 KiB, 5.61%)**; the cached
pair saves 305,152 bytes (298 KiB). The experimental wrapper adds 15,872 bytes
to the full probe or 14,848 bytes to the reduced probe. These are linked probe
endpoints, not archive/header sizes, a rebuilt sandbox size, engine-only size
or complete deployment footprint. System D3DCompiler and the shared MSVC runtime
remain prerequisites.

The complete 49-request ownership test retains 295,486 charged entry bytes
until explicit clear. With all results held, the observed owned-result slab
peak across two passes is 352,378 bytes. Slab charges and retained entry charges
return to zero after release/clear. They are conservative wrapper accounting,
**not whole-compiler, allocator or OS peak memory**.

Separate per-process OS high-water working-set and peak-pagefile counters are
retained in raw results. Working set includes shared pages; these counters
cannot be relabeled precise Tint/private-heap usage or added to cache charges
as independent allocations. No live heap/ETW attribution was performed.

## Discovered limitation and next strategy

The initial preflight failed strict replay **before any performance cases**:
successful FXC warning diagnostics contained absolute filenames derived from
the working directory. The completed batch restored the original qualified
working directory uniformly, preserved all guards, and used a new output
directory with the frozen binaries/profiles. The failed receipt remains.

The measured cache is therefore qualified for that **fixed compiler/diagnostic
context**, not arbitrary working-directory mutation while a cache is alive.
Its explicit request/options fingerprint does not automatically capture the
host's working directory. Before adoption, include the effective diagnostic
and system-compiler context in invalidation, or separately review consistent
diagnostic naming. Do not strip warnings or paths to hide the mismatch.
Fixing this gap is follow-up work in a separate, unmeasured cache revision;
its timings must not be substituted for these frozen measurements.

The next candidate is **direct Tint WGSL -> HLSL -> FXC**. Source review found
that it is not a writer substitution: it needs genuine IR consolidation of
multiple logical UBOs into `b0`, preserved logical metadata and correct access
lowering, plus a compiler-local semantic hook for bgfx geometry and reserved
instance TEXCOORD31..28 inputs. Texture/sampler reflection, active native
layouts, stage linking and compatible containers remain required. Direct
output need not be byte-identical, but must establish interface/resource
correctness and relevant native rendering before timing. This route was
**not executed in the measured batch**.

The reviewed Naga HLSL path is also not a drop-in replacement: IO semantics,
separate cbuffers and sampler/resource conventions differ. No Rust toolchain,
Naga dependency, renderer migration or dependency upgrade was installed.

**Conclusion:** retain the current working sandbox while evaluating a
context-correct output cache and direct Tint HLSL separately. The current
evidence supports avoiding duplicate compiler work and a modest linked
footprint reduction. It does not select a new translator, prove complete GLB
first-frame gains, reduce first-use unique-shader work, or measure GPU FPS.
Source composition, parsing/codecs, environment preparation, bgfx shader/program
creation, resource upload and first visible frame remain outside this benchmark.

## Evidence

Local root:
`build\lite-c99\shader-strategy-investigation\experiments-current`.
The completed batch is `measurements-20261014-quiet-02`; retain its predecessor
`measurements-20261014-quiet-01` as the failed preflight.

The batch includes `measurement-handoff.json`, `reviewed-findings.json`,
`paired-performance-statistics.json`, `performance-run-aggregates.json`,
`timer-output-overhead-control-statistics.json`, `footprint-and-memory.json`,
frozen schedules, before/after integrity and correctness receipts, all raw
stdout/stderr/process records and the effective runner/analysis scripts.
Sibling `matched-arms.json`, `measurement-profiles.json` and source manifests
identify the exact experimental inputs.

The parent independently verified 42 frozen driver/map/profile/owned-source
hashes and 18 report hashes, all 528 raw performance results and 1,056 raw
stdout/stderr hashes, each run's lifecycle/wall aggregates, schedule balance
and 264 paired bootstrap intervals. Its separate
`parent-measurement-verification.json` records that audit; compiler-copy and
overhead-control qualification remain separately attributed to the agent.
