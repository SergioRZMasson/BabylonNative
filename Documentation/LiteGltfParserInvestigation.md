# Native glTF parser investigation

**cgltf is the smallest standalone parser tested and wins on small/schema-heavy
inputs. Current TinyGLTF is competitive on large binary GLBs; neither is a
drop-in replacement for our exact native raw-provider contract.** Keep the
working native loader/sandbox moving while evaluating a separate parser adapter,
rather than importing Gary's prototype engine or delaying the viewer.

## Prototype reuse and parser dependencies

Gary's [prototype loader at the inspected commit](https://github.com/bghgary/BabylonLiteNativePort/blob/ca5b8fdafdbc034982982210d1daa20dbe210218/Source/GltfLoader.cpp)
actively uses cgltf. It combines mesh definitions, selects the first material,
centers/rescales geometry, and does not retain authored node hierarchy or
`EXT_mesh_gpu_instancing`. It also skips some unsupported primitives/images.
That is useful integration evidence, but not an arbitrary-model loader to copy
wholesale. The prototype was inspected, not independently built. No explicit
compatible license grant for its loader body was found; independently
MIT-licensed cgltf is a separate option. No prototype loader body was copied.

| Candidate | Pinned revision | JSON and image boundary |
|---|---|---|
| cgltf 1.15 | `360db1a95480fe102ae9c69b27c5d101167ff5ba` | Embedded JSMN tokenizer and glTF schema extraction; no generic JSON DOM or image codec |
| TinyGLTF 3.0.1 | `0d9ce1a9561688c6c6e7471430d15a50a0b15f1a` | C11, custom **full JSON value tree**; tested native path retains image references without decoding |
| Legacy TinyGLTF 2.9.7 | `488a70a3df62a4df1a736e9e56fb8836580c4888` | C++/nlohmann DOM by default; parser-only tests disable stb decode/write |
| Native provider baseline | Frozen raw-service v2 snapshot, nlohmann 3.12.0 | JSON DOM plus native raw descriptors; not the subsequently extended production v3 |

The hypothesis that both alternatives avoid a full JSON implementation is only
partly correct: cgltf avoids a generic DOM but still tokenizes the whole JSON
document; current TinyGLTF builds a full custom DOM. Do not evaluate current
TinyGLTF using the legacy v2 dependency stack. Preserve upstream license and
numeric-conversion notices when adopting a candidate.

## Controlled measurement

Windows x64, Xeon W-2235, MSVC 19.51 Release `/O2 /Ob2 /MD /Gy /Gw`, linker
`/OPT:REF /OPT:ICF`, no LTO. Twelve counterbalanced process rounds cover four
real GLBs and three synthetic graph sizes, four parser configurations, two
ownership modes and three stage definitions: **2,016 formal child processes**,
168 calibration children and 1,488,960 fresh timed parse/free cycles.
Implementation builds/tests/rendering were explicitly paused. External
shared-workstation load was not independently isolated.

The table below is the mean of twelve independent process means for the
owned-input schema/copy, common conversion/validation and cleanup stage.
These are **microseconds per cycle**, not complete model load or rendering time.
Input file/network IO, image decoding, engine construction, GPU upload and shader
compilation are excluded. All runs/outliers were retained.

| GLB | cgltf | TinyGLTF 3 | Native nlohmann-v2 snapshot |
|---|---:|---:|---:|
| Box, 1,664 bytes | **13.94** | 17.98 | 40.80 |
| SimpleInstancing, 7,356 bytes | **18.83** | 27.04 | 54.52 |
| Avocado, 8,110,040 bytes | 2,297.84 | **2,269.70** | 2,846.75 |
| ToyCar, 5,422,412 bytes | 3,293.15 | **3,171.23** | 3,735.09 |

Mean cycle-time reductions versus the frozen native provider, with paired
process-round bootstrap 95% intervals:

| GLB | cgltf reduction | TinyGLTF 3 reduction |
|---|---:|---:|
| Box | 65.83% [63.66%, 68.56%] | 55.94% [52.01%, 60.16%] |
| SimpleInstancing | 65.46% [62.54%, 67.46%] | 50.40% [48.24%, 52.04%] |
| Avocado | 19.28% [16.85%, 21.46%] | 20.27% [18.76%, 21.44%] |
| ToyCar | 11.83% [9.15%, 14.76%] | 15.10% [12.87%, 17.69%] |

cgltf is 22.45% faster than Tiny3 on Box and 30.35% on SimpleInstancing.
Avocado's head-to-head interval crosses zero; ToyCar favors Tiny3 modestly.
The synthetic 16,384-node graph has about 657 KB JSON but only 44 binary bytes:
owned-stage means are cgltf 15.49 ms, Tiny3 25.00 ms and baseline 44.76 ms.
This supplements real assets, not a universal workload claim.

Large binary payload copying matters: Avocado has only 1,524 bytes of JSON.
Borrowed native-schema times of 16.44/21.46 microseconds for cgltf/Tiny3 do not
mean eight megabytes of images were decoded or scanned at that speed.
Owned native-schema modes take about 2.24/2.23 ms. Current-provider and legacy
Tiny2 behavior must not be mislabeled zero-copy merely because borrowed mode
was requested.

Reported P95s are percentiles of iteration-amortized batch means, aggregated
across independent children, **not individual-load P95 latency**. Confidence
intervals use 10,000 resamples of matched process rounds, not independent
resampling of iterations.

## Footprint and memory

Same minimal driver/compiler/link flags, with 28,672-byte empty executable:

| Linked probe | Executable bytes | Marginal bytes versus empty driver |
|---|---:|---:|
| cgltf | **95,232** | 66,560 |
| TinyGLTF 3 | 118,784 | 90,112 |
| Native nlohmann-v2 provider | 148,992 | 120,320 |
| Legacy TinyGLTF 2, parser-only | 405,504 | 376,832 |

These are linked parser probes, not header sizes, engine sizes or final shipping
packages. Keeping our environment-manifest JSON parser changes the result:
cgltf plus the experimentally stripped env-only JSON path links to **192,000
bytes**, Tiny3 plus env-only JSON to **214,528 bytes**, versus the existing
combined provider's 148,992 bytes. A standalone parser saving is therefore
**not an automatic application saving**.

Median whole-child peak working sets in the owned comparison:

| Input | cgltf | TinyGLTF 3 | Native baseline |
|---|---:|---:|---:|
| Avocado | 21.02 MiB | 21.06 MiB | 21.07 MiB |
| Synthetic 16,384-node graph | 16.09 MiB | 20.38 MiB | 20.66 MiB |

These OS counters include input allocation, warmup, model/DOM/CRT work and
allocator retention. Exact provider-only allocation counts and peak live bytes
were not measured; they are not inferred from a parser's arena.

## Correctness and integration decision

The initial 112-case qualification has 54/56 exact positive semantic comparisons.
Stock cgltf uses Float32 schema fields and differs on ToyCar and a deliberate
double-precision fixture. Both current alternatives lose optional-field
presence needed by our raw contract. Forty-three of 48 negative combinations
reject malformed data: Tiny3/legacy Tiny2 accept the tested wrong-length
translation/color arrays, while cgltf accepts a fractional accessor count after
integer conversion. Additional raw-source checks are required before adoption.

Timing stock parsers and common validated stages is not proof of complete
raw-service v3, glTF conformance or identical original precision. A production
adapter must preserve presence/double values, validate tuples/counts/ranges and
unsupported features, map checked raw indices/POD, and retain result bytes until
the engine copies/decodes them with exactly-once cleanup.

**Recommendation:** complete common-model embedded-texture support and the
visible native sandbox using the current provider. Treat independently licensed
cgltf as a small, fast candidate, and current Tiny3 as a competitive alternative
with tested double-valued fields. Evaluate a fully faithful adapter plus the
retained `.env` dependency before selecting a replacement. No production parser
swap, backend change or image-quality shortcut is authorized by this benchmark.
Shader translation remains a separate next phase.

Persistent local evidence is `build/lite-c99/gltf-parser-investigation`:
`final-handoff.json`, `public-results-summary.json`, `timing-results.json`,
`prototype-reuse.json`, `dependency-matrix.json`, `parser-design.json`, harness/
qualification/build records and `bench-profiles/controlled-run-20261013-194303`.
The parent independently verified all 71 frozen inputs, all 2,016 raw record
hashes and recomputed means, and all sixteen real-asset table cells.
