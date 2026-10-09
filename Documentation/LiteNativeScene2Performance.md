# Original scene2 native qualification and performance

Authority: original Babylon Lite 1.32.0 at
`2e064d88ec7422af946f8ec7f089ac6519f99295`. Native checkout baseline:
`8a3ca669d195d424aa538fdddc801a14c8d283c0`, with the reviewed additive
Directional implementation. Dependency pins are unchanged.

## Preserved workload and qualification

This is **original corpus scene2**, not the auxiliary native-cube/primitives
examples. Original TypeScript SHA256:
`4f62cb2b45f0dcc128f32d40ba357247b42738ee519929deaa839db890c6b05f`.
Byte-preserved STANDARD user C++ SHA256:
`074a0cc69325293374be2a291bee015c32cdd6343598742d9b935d8fb68a4649`.
There was one STANDARD emission; its actual SDL_GPU/D3D12 Release binary and
readbacks remain immutable.

All lanes use 1280×720, requested/default MSAA4, the complete 2,415-vertex/
13,872-index default sphere, one default untextured Standard material, one
Directional light `(0,-1,0)` with intensity1/red diffuse/green specular, and
ArcRotate `(-π/2,π/2,5)`, target0, near1/far10000 with original controls attached.
No geometry, shader quality, material, guard, control or source reduction.

Native C++ and unchanged original-JS/native-shim lanes match source camera
scalars/targets at static1/61/121 and rotate/wheel/pan12/61. All 18 selected
GPU readbacks have silhouette IoU1.0 and full RGB differences ≤1 LSB. Native
clear blue is77 versus SDL76. Zero first delta and queued-replay alignment are
preserved. Additional unselected intermediate captures remain indexed.

CPU render JSON is reconstructed state, **not independent GPU upload proof**.
PNG/BGRA files are actual GPU readbacks. Requested flags and separate DXGI/D3D12
four-sample capabilities are recorded, but effective child device/MSAA selection
is not independently exposed by this capture API and is not invented.

The 167-function C99 contract preserves all older signatures/layouts. Source
light-entry goldens, F32/HPM parent transforms, allocator failures/churn, typed
forgery/slot reuse, shared lifetime/list/version/cap16, real mixed lighting and
runtime shader corruption/release/retry tests pass. Fresh UI-on/off19/13 and
native Minecraft10 regressions, plus cube/primitives GPU/input/semantics, pass
in new scene2 build directories. The first native Minecraft idle180 verifier
lacked its local immutable reference; that failure and targeted successful
retry are retained, not silently replaced. Other harness/build failures are
also retained.

## Fresh matched protocol

18 counterbalanced rounds × SDL/native-C++/original-JS = **54 fresh processes**.
Every permutation runs three times. Release `/O2 /Ob2 /DNDEBUG`, hidden real
swapchains, vsync/pacing off, first delta0 then 1000/60ms, 180 warmup and
1,200 measured frames. Controls remain attached and idle. There is no physical/
scripted input, screenshot/readback, file output or concurrent build during
measured frames. No outlier deletion or historical timing reuse.

The parent declared a quiet gate for its own work; all own builds and correctness
processes had stopped and no matching build/demo process was active at the
gate. Machine: Xeon W-2235, NVIDIA Quadro P620, driver32.0.15.8142. No power,
ETW or global system settings were changed. External shared-workstation load
is not independently isolated.

## Executed complete-host results

Values below are medians of the 18 per-process results, not pooled frame
statistics. Paired confidence intervals bootstrap process-run ratios.

| Metric | Native C++ | Original JS/native | STANDARD SDL |
|---|---:|---:|---:|
| Average complete host, ms | 0.288266 | 0.302485 | 0.397000 |
| P95 frame, ms | 0.376750 | 0.388600 | 1.832500 |
| Process CPU/steady frame, ms | 0.266927 | 0.345052 | unavailable |
| Core + callbacks bracket, ms | 0.005646 | 0.022939 | unavailable |
| Submit/wait inner bracket, ms | 0.282269 | 0.224405 | unavailable |
| Valid unique-query GPU time, ms | 0.040140 | 0.040280 | unavailable |
| Startup to native Run entry, ms | 348.962 | 365.494 | unavailable |
| Whole process wall, ms | 826.195 | 869.193 | 1196.800 |
| Whole process CPU, ms | 812.500 | 968.750 | 1007.813 |
| Private bytes | 143,484,928 | 161,736,704 | not queried |
| Executable bytes | 5,980,160 | 8,702,976 | 694,784 |

Executable bytes are **not standalone package sizes**. Actual shared DLL paths/
hashes, including V8/ICU and SDL dependencies, are retained locally; no private
provider/DLL package is uploaded. Native C++ links no VM/SDL/generated engine.
Runtime WGSL compilation remains present; it is not replaced with offline-only
native shaders. SDL uses its original offline reference shader path.

| Paired comparison | Mean change, 95% CI | P95 change, 95% CI |
|---|---|---|
| C++ versus SDL | **−28.086% [−29.690, −26.249]** | **−79.137% [−79.874, −78.228]** |
| JS/native versus SDL | −24.854% [−26.330, −23.477] | −78.288% [−79.029, −77.393] |
| JS/native versus C++ | +4.705% [+2.001, +7.299] | +4.714% [−0.676, +10.037] |

The predefined complete-host C++ mean improvement >5% with no P95 regression
is met **for this scene2 workload and batch**. JS/native also improves both
against SDL; it retains additional VM/process cost versus C++.

## Interpretation and limits

- D3D11/bgfx versus D3D12/SDL_GPU, render-thread/queue scheduling and different
  shader compilation/lowering paths remain confounds. A shorter C99 source or
  Core bracket is not a causal proof that C-style engine algorithms are faster.
- Native submit/wait dominates its short complete loop. This implementation
  changes no bgfx handoff, query budget, queue depth or threading policy.
  Large SDL P95 in this fresh batch is measured, not trimmed or attributed to
  an unverified internal cause.
- JS complete wall includes VM dispatch/completion wait. Inner brackets overlap
  outer work and must not be added as exclusive categories. Native timing retains
  GetProcessTimes instrumentation; no unprofiled algorithm-only estimate is
  manufactured by subtraction.
- GPU query IDs are deduplicated; invalid/unavailable queries are null. SDL's
  summary supplies rounded mean/median/P95 only. Missing SDL steady process,
  GPU/wait/startup values are unavailable, not zero or inferred differences.
- Whole-process wall/CPU includes startup, warmup, measurement and shutdown.
  Native startup includes window/runtime/material/compiler/resource work; it is
  not a pure compiler measurement.
- Existing VM source override `8cd142951018de07c89c23f726fb7dc9c8374599`
  versus root pin `33e4a233dea178712352ec90a647683084bfbf1d` remains disclosed.
  V8/RmlUI/FreeType/bgfx/Tint/SPIRV-Cross pins were not upgraded.
- Previous cube C++ average regression (+12.58%) and primitives mean improvement
  with P95 regression remain **unmet combined goals**. This scene2 result does
  not repair their pending backend/tail investigation or establish a universal
  win, all-light/shadow/PBR support, or all-corpus coverage.

## Reproduction and evidence

Parent acceptance independently reran the 19 UI-ON, 13 UI-OFF and 10 native
Minecraft regression cases, strict 33-file/21-TU Core format/AST checks and
eight native C++/JS static/input cases. All 26 repeated captures have exactly
the recorded scene/control state and remain within one RGB LSB. Original-JS
167-function routing and all 162 previous signatures are preserved.
The parent recomputed the paired mean/P95 changes and checked 1,246 indexed
fingerprints. Eight current-build CTest summary files were regenerated by that
independent rerun; `parent-rerun-audit.json` records the expected/actual hashes.
All other indexed source, ABI, binary and benchmark inputs are unchanged.

All scripts, exact commands/return codes, source/body/license/config/binary
hashes, actual loaded modules, GPU/input/lighting fields and retained failures
are indexed in the ignored independent `build\lite-c99\demo-scene2` tree.
`parent-review.json` records the public contract approval.

- `implementation-build.py`: fresh UI-on/off/native Minecraft builds, ≤3 jobs.
- `qualify-native.py`, `compare-native.mjs`: original nine poses per native lane.
- `abi-audit.py`, `audit-source.py`: older ABI, byte-preserved user sources and
  thin-only bundle/PE/link boundaries.
- `implementation-receipts`: executed tests, audits and failure/retry evidence.
- `measure-scene2.py`, `summarize-scene2.py`, `MatchedScene2_18`: the actual
  54-process matched comparison and paired mean/P95 confidence intervals.
- `final-handoff.json`, `final-artifact-index.json`: persistent final receipts
  and indexed local evidence.

Measured binaries and previous qualified roots remain immutable. A later code,
backend or host experiment requires a new qualification and fresh matched batch.
