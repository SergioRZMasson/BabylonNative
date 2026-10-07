# Babylon Lite native component

`LiteLayer` is a handwritten C-style implementation of the Minecraft-reached
Babylon Lite 1.32.0 API, based on original TypeScript at
`2e064d88ec7422af946f8ec7f089ac6519f99295`. Its only public header is
`Core/LiteLayer/Include/babylon_lite.h`; its direct link dependencies are `bgfx`
and `bx`. It contains no JavaScript runtime, NAPI, NativeEngine, SDL, Dawn/WebGPU
renderer, generated Babylon engine or bblitec runtime.

The original Babylon Lite source is Apache-2.0 licensed. Its license is retained
in `Core/LiteLayer/LICENSE.txt` for this native port and alongside the copied
demo sources; the surrounding Babylon Native repository retains its own license.

The header is strict C99/C++17. Private implementation compilation requires
C++20 because the pinned bx public headers require it. Native users link
`LiteLayer` (`Babylon::LiteLayer` alias); bgfx's existing transitive dependencies
are carried by the static target.

## Components

| Target | Responsibility |
|---|---|
| `LiteLayer` | Native transforms, geometry, scenes, materials, rendering/resource lifetime and audio routing. |
| `LiteShaderCompiler` | Separate synchronous C99 runtime WGSL compilation service. |
| `LiteJSBinding` | Thin argument/status/identity/property/callback marshalling to C99. |
| `LiteNativeTests` | Official-source geometry/audio goldens, mutable nodes, allocator churn and stale identities. |
| `LiteNativeGraphicsTests` | Test-owned Win32/bgfx host, native rendering and actual GPU readback. |
| `LiteShaderCompilerTests` | Dynamic WGSL, diagnostics, linked stages, reflection and compiler-result ownership. |
| `LitePlayground` | Visible Win32/AppRuntime host for the original full Minecraft demo, native platform services and binding fixtures. |
| `LiteApplicationBundles` | Application-only bundling and source/import provenance; no Babylon engine implementation. |
| `LiteNativeProjectedTests` | Optional real bblitec-transpiled test applications projected onto C99; no generated engine/runtime objects. |
| `LitePlatformAudioTests` | Separate real XAudio2 host primitive/routing tests; no human-audibility assertion. |
| `LiteMinecraftNative` | Complete bblitec-transpiled original Minecraft C++ application with C99-backed handles and a separate Win32 platform host; no JS VM or legacy engine/PAL objects. |

The runtime compiler uses the full public Tint reader/SPIR-V writer at
`a21a4a1c7c497e6366947ccaefbab768d16f32a8`, repository-pinned SPIRV-Cross and
runtime system FXC on D3D11. It reflects original WGSL members and performs
compiler-level UBO consolidation into bgfx's b0 buffer, producing real v12
shader containers. Uniform names distinguish incompatible native array counts.
Invalid WGSL returns actual diagnostics. It does not substitute canned shaders,
deploy precompiled DXIL or link any compiler dependency into `LiteLayer`.
Other compiler backends are currently unsupported.
Its private, full SPIRV-Cross target uses a distinct namespace; enabling it does
not change NativeEngine's existing WEBMIN configuration or compiler symbols.

Hosts provide an allocator, window/target metadata and optional shader, audio
and I/O services. Engine creation requires
`isExternalBgfxInitialized(hostUserData)` for either ownership mode. It reports
only independently host-initialized bgfx, never Lite-owned initialization.
The host serializes lifecycle operations. Borrowed hosts grant exclusive view
ranges and provide a real submitted-work synchronization callback.

## Building

### Contributor style checks

LiteLayer has a scoped C-style profile: Allman braces, four-space indentation,
100-column lines, no one-line constructs or packed declarations, and mandatory
braces for every control-flow body. Use Clang/ClangFormat 22 consistently.
Contributors enable `-DBABYLON_LITE_ENABLE_STYLE_CHECKS=ON`; this makes
`LiteLayerStyleCheck` a required library-build dependency. It checks compiler-AST
declarations, fields, control flow and macros in addition to formatter output.
Consumers need not install the formatter or enable this option.

The native library has a standalone entry point, which does not configure the
Babylon Native JavaScript/NativeEngine dependency graph:

```powershell
cmake -S Core\LiteLayer -B build\lite-c99\standalone
cmake --build build\lite-c99\standalone --config Release --target LiteLayer --parallel 3
```

The root build exposes opt-in integration targets:

```powershell
cmake -S . -B build\lite-c99\integration `
    -DBABYLON_NATIVE_BUILD_APPS=OFF -DBABYLON_NATIVE_INSTALL=OFF `
    -DBABYLON_NATIVE_EMBEDDING=OFF `
    -DBABYLON_NATIVE_BUILD_LITE_LAYER=ON `
    -DBABYLON_NATIVE_BUILD_LITE_TESTS=ON `
    -DBABYLON_NATIVE_BUILD_LITE_SHADER_COMPILER=ON `
    -DBABYLON_NATIVE_BUILD_LITE_SHADER_TESTS=ON `
    -DNAPI_JAVASCRIPT_ENGINE=V8 `
    -DBABYLON_NATIVE_PLUGIN_LITEJSBINDING=ON `
    -DBABYLON_NATIVE_BUILD_LITE_PLAYGROUND=ON
cmake --build build\lite-c99\integration --config Release `
    --target LiteNativeTests LiteNativeGraphicsTests LiteShaderCompilerTests LitePlayground --parallel 3
ctest --test-dir build\lite-c99\integration -C Release -R Lite --output-on-failure
```

Use the Visual Studio developer environment in the same command process when
using Ninja. Source-cache overrides must match the declared dependency pins:
bgfx.cmake `b53236f4d6dede3252d32b9c11921d0007d705d1` (bgfx
`cb0c6d0c6133d123989aaae10cbd35fa139cb648`) and SPIRV-Cross
`7c8705749fc1e234119c20695f82179b30d90914`. Older bgfx caches use a different
public API and v11 containers and cannot validate this integration.
`BABYLON_LITE_TINT_LIBRARY_DIR` optionally imports Release Tint compiler
libraries, with `FETCHCONTENT_SOURCE_DIR_LITETINT` at the declared source pin.

## Evidence and remaining scope

`NativeTests/Fixtures/OfficialLite/inventory.json` records eight independent
official-package geometry fixtures and nine Float32 audio curves, including
the original 703-vertex/3888-index Minecraft sky. These are test data, not engine
implementation. Audio tests use an explicitly offline, non-audible host double.

`LitePlayground contract` tests live native property mutation, identity, typed
arrays and Promise/error shapes. `callback-throw` verifies that a JavaScript
exception is retained without unwinding through a void C callback, then restored
after the C frame and used to reject the first-frame Promise.
`minecraft-shaders` exercises original opaque/cutout/blend, mob, sky, highlight
and cloud shader factories. This is a shader subset, not full Minecraft.

The component tests include eleven native CPU tests covering all 17
official-source fixtures and 1024-cycle
geometry/node allocator-generation churn; six native graphics tests with real
red-to-green readback, visibility/last-owner retirement, callback ordering,
shared-scene membership and repeated OWNED/multiple BORROWED lifecycle; seven
compiler tests; JavaScript property/identity/uniform/Promise and callback-error
tests; and the original Minecraft shader-family fixture (seven submitted draws).
The standalone native-only `LiteLayer` build also passes. These results are
component evidence, not complete Minecraft/corpus coverage.

The complete unmodified radius-6 Minecraft demo and all 24 Minecraft modules
are retained in `Apps/LiteTests/LitePlayground/JavaScript/Demos`, with source
provenance and license. The pinned esbuild application-only build substitutes
native imports and refuses engine/runtime implementation inputs. Its generated
`dist/coverage.json` records actual imports and source hashes without claiming
full export coverage. The uncalled decoder helper imports are explicitly
unsupported.

### Full original Minecraft host

`LitePlayground minecraft` runs the unmodified original SEED 1337/radius-6
application and all 24 modules in a visible, fixed-size 1280×720 Win32 window.
The separate platform host provides raw mouse/pointer lock, keyboard/buttons/
wheel input, DOM HUD presentation, WIC PNG decode, nearest-neighbor canvas
atlas composition, original Kenney CC0 assets and native save/open dialogs.
Asset hashes and the original license are under `Assets/minecraft/voxelpack`.
No engine TypeScript is bundled or executed.

```powershell
.\build\lite-c99\integration\Apps\LiteTests\LitePlayground\LitePlayground.exe minecraft
.\build\lite-c99\integration\Apps\LiteTests\LitePlayground\LitePlayground.exe minecraft `
    --frames=180 --capture-root=build\lite-c99\captures\full
.\build\lite-c99\integration\Apps\LiteTests\LitePlayground\LitePlayground.exe minecraft `
    --frames=240 --replay --capture-root=build\lite-c99\captures\replay
```

The permanent `LiteMinecraftFull180`, `LiteMinecraftReplay240` and
`LiteMinecraftNativeInputSaveLoad` CTests run the real workload, capture real
bgfx GPU frames and the Win32 HUD backing surface, reject empty/unchanged
captures, and require zero asset failures and clean native teardown. The third
test posts events to the actual host window and selects a test-owned path in
the real native save/open dialogs: original game code saves and reloads the
seed, player transform, time and block edits. This is deterministic host-event
automation, not evidence of a human mouse-input session. `--replay` injects
the separate deterministic browser-event tape; it does not replace gameplay.
CTest capture directories contain PNGs and `summary.json`. These three visible
tests are serialized; `BABYLON_LITE_ENABLE_MINECRAFT_TESTS=OFF` disables them
for component-only builds without a desktop.

The fully enabled exact-pin Release integration passes all twelve CTests,
including the three full-game tests, separate real-device audio test and optional
projected application tests. The complete bblitec corpus remains unfinished. The separate full Minecraft C++
application projection is described below. Passing the scoped Minecraft workload
does not establish coverage of all 1814 package exports.

### Audio and binding boundaries

The Win32 application supplies a separate XAudio2 output graph service.
Original `minecraft/audio.ts` still synthesizes the wind, LFO, footsteps and
edit sounds; NAPI only forwards native context/node/buffer/parameter calls.
Replay evidence includes nonzero samples submitted to the actual device
stream. This is not a human audibility test; headless/offline runs never claim
audible output. Missing output devices remain an explicit unsupported host
capability, which the original demo can handle.
The initial output host supports mono buffers, sine/triangle oscillators,
gain/LFO modulation and lowpass filtering; other channel layouts and negative
buffer playback rates return UNSUPPORTED rather than pretending WebAudio-wide
parity. Audio-clock retry polling is forwarded to the native engine.

The binding never detaches native children to retire a garbage-collected
parent wrapper. BUSY retirements are retried; unexpected disposal failures are
preserved and surfaced. A forced-GC regression verifies retained native parent
identity and world transforms.

Uniform getters use stable JS-owned Float32 copies, refreshed by binding
setters and subsequent getters. A previously retained copy does **not**
continuously observe foreign C++ writes, and writing that copy is not a native
uniform setter. It remains memory-safe after material disposal rather than
exposing unpinned borrowed Core storage. This boundary is not full cross-client
live-view parity.

Generated engine implementation/runtime must never be linked into `LiteLayer`.

### Handwritten module boundaries

The guideline-driven implementation separates allocator/typed-generation registry
(`Runtime`), data-only transforms/cameras (`Nodes`), independent primitive arrays
(`GeometryData`) and material declarations/live Float32 values (`ShaderMaterial`)
from GPU mesh storage (`Geometry`), textures (`Texture`) and runtime shader
reflection/packing/submission (`ShaderPipeline`). Audio uses its own POD header
and injected primitives. Cleanup callbacks are registered by the creating feature;
the runtime does not statically dispatch into every feature.

`RenderOrder` rebuilds a scene-local material-ID hash table once per rendered frame,
after callbacks. All live mesh members contribute opaque minimum order/earliest
sequence, including invisible and empty geometry. Duplicate members retain their
draw occurrences. Transparent depth-first sorting is unchanged. Scratch capacities
grow geometrically, commit only after both allocations succeed, and are reused;
no across-frame group cache or experimental switch is required.

`LiteDataOnlyTests` exercises runtime/nodes/primitive arrays/material values without
an engine, window or shader service. Its Release link map contains only those four
LiteLayer translation units and bx support, not bgfx rendering or compiler/VM
objects. Native randomized ordering, scratch-failure/10,000-frame allocation,
live GPU callback-swap and index-tail clearing tests cover the rewritten paths.
In-capacity geometry shrink clears removed indices even with empty explicit ranges,
matching the original source's automatic degenerate-triangle tail.
Malformed compiler-reflection string spans are rejected before lookup or
dereference, and the compiler result is still released exactly once.

### Complete original Minecraft C++ application

`NativeProjection/FullMinecraft` retains the original SEED 1337/radius-6 game
and its 24 modules byte-for-byte. Its projection compiles 26 emitted application
translation units (plus the generated data-only material description), not the
compiler's suggested Babylon engine, renderer or PAL sources. Shader sources,
declarations, defaults and options come from the application's lowering manifest;
the real separate WGSL compiler prepares them through C99.

`C99Client` contains client handle/property forwarding and ownership bookkeeping,
not engine algorithms or copies of legacy Engine records. Node and camera writes
immediately use public C99 getters/setters. Each material has its own native
identity and uniform storage. The emitted variant/offset setters are mapped to
original uniform names and the single material instance reached by each factory
in this game. Repeating such a factory is explicitly refused: this compiler's
lost instance identity cannot establish general multi-instance material parity.
The `MaterialIdentity.ts` non-Minecraft lowering limitation is also retained.

Only header-only user-language container, closure, JSON and synchronous-promise
support is reused from the compiler checkout. These C++ value/collection helpers
are outside `LiteLayer`; they are not a JavaScript VM. The host links the existing
native XAudio2 service and provides input, WIC icons/PNG captures, a scoped native
HUD backing surface and real save/open dialogs. Its HUD is a native adaptation,
not a complete CSS/DOM implementation or RmlUI-equivalent renderer.
Human mouse usability, human hearing and cross-renderer pixel equivalence are
not established by the automated tests.

Build in the Visual Studio developer environment. This isolated target imports
only the existing exact-pin Release native libraries, not the integration
build's JavaScript/NativeEngine link graph. `nlohmann/json.hpp` is required for
the emitted user's save/load values, outside the Core library.

```powershell
node Apps\LiteTests\NativeProjection\FullMinecraft\Tools\generate.mjs `
    <built-bblitec-checkout> <BabylonNative-root> `
    build\lite-c99\full-native-projection
cmake -S Apps\LiteTests\NativeProjection\FullMinecraft `
    -B build\lite-c99\full-native-game -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DBBLITEC_SOURCE_DIR=<built-bblitec-checkout> `
    -DLITE_MINECRAFT_EMITTED_DIR=<root>\build\lite-c99\full-native-projection\Emitted `
    -DLITE_MINECRAFT_BUILD_GAME=ON `
    -DLITE_NATIVE_LIBRARY_DIR=<exact-pin-Release-integration-build> `
    -DLITE_BGFX_SOURCE_DIR=<exact-pin-bgfx.cmake-source> `
    -DLITE_TINT_LIBRARY_DIR=<exact-pin-Release-Tint-library-tree> `
    -DLITE_JSON_INCLUDE_DIR=<nlohmann-json-include-directory>
cmake --build build\lite-c99\full-native-game --parallel 3
ctest --test-dir build\lite-c99\full-native-game --output-on-failure
.\build\lite-c99\full-native-game\LiteMinecraftNative.exe
```

The isolated tests cover the 169-chunk original terrain oracle, independent
material instances and native mutable properties, full 180-frame GPU captures,
240-frame gameplay/audio replay, original save/load through two real OS dialogs,
and benchmark-control qualification. The game activates all 169 render chunks
before its first frame. Its post-water-prefill world hash is intentionally
different from the raw terrain-generation oracle.
Capture PNGs and receipts are under the isolated build's `Captures` directory.
`Tools/verify-run.mjs` verifies receipts and can compare two identical idle runs.
After `dumpbin /imports` is saved as build-local `imports.txt`,
`Tools/audit-native.mjs` checks the actual object/header/link/import graph and
records source, binary and public-header hashes in `native-provenance.json`.
These scoped results do not establish all 1814 exports or full corpus coverage.

Bounded `--frames` runs and benchmarks reject physical keyboard/raw-mouse input;
only the explicit scripted transport is dispatched. Hidden benchmarks do not
take pointer lock, move the cursor or activate a visible window. Interactive
visible play retains native input and uses elapsed wall-clock simulation delta.

#### Native benchmark controls and timing boundaries

```powershell
.\build\lite-c99\full-native-game\LiteMinecraftNative.exe `
    --benchmark-output=build\lite-c99\full-native-game\Benchmark\idle.json `
    --warmup=180 --measure=1200
```

This runs real D3D11/bgfx rendering at 1280×720, SEED 1337/radius 6, MSAA 1,
no vsync and fixed `1000/60` milliseconds (the original first frame still gets
zero). `BBLITE_BENCHMARK_FRAMES`, `BBLITE_BENCHMARK_WARMUP_FRAMES` and
`BBLITE_MSAA` are also accepted; this host supports sample counts 1 and 4.
`--replay` enables the separately documented host replay, not an assumed match
to a different compiler host's event-tape protocol.
Capture and save/load modes are forbidden during benchmarking.

QPC `cpuSamplesMs` and `totalHostCpuSamplesMs` measure the complete native host
frame: wall-clock platform timers/audio retry polling, original user update,
C99 rendering, bgfx submission/
present, native HUD GDI backing render, and user-language cycle collection.
`coreCpuSamplesMs` excludes the HUD and cycle-collection work. Both exclude OS
poll/replay dispatch, capture/readback, JSON writing, sleeps and deliberate pacing.
Native real-time audio generation runs on a separate worker; original game audio
graph operations within update remain in the frame bracket.
The hidden HUD renders its backing surface but does not present a desktop overlay.
The native HUD, backend, batching and color/depth configuration must therefore
be disclosed when comparing against SDL/RmlUI or Dawn variants.

Startup/shader compilation is recorded separately. Receipts are written only
after all measured frames, with per-frame CPU samples/draws, world/camera/time
observations and configuration. Real asynchronous bgfx GPU timestamp-query
intervals include their GPU frame IDs; unavailable queries use `-1`, never a
fallback zero advertised as measured GPU time. Repeated GPU frame IDs must not
be mistaken for independent GPU measurements. This host enables bgfx GPU
profiling; comparisons must disclose that setting as well.

#### Fresh comparison with the earlier bblitec implementations

On 2026-10-07, fifteen balanced rounds of all five actual executables used
180 warmup plus 1,200 measured frames, SEED 1337/radius 6, 1280x720, MSAA 1,
fixed `1000/60` ms and hidden/no-input/no-vsync operation on a Xeon W-2235 /
Quadro P620. These are fresh measurements, not reused historical averages.

| Complete measured implementation path | Median run-average CPU frame | Median run P95 |
|---|---:|---:|
| Standard bblitec, SDL_GPU/D3D12 | 1.948 ms | 2.342 ms |
| Previous native-layer experiment, SDL_GPU/D3D12 | 2.057 ms | 2.543 ms |
| Previous native layer with stock Dawn/D3D12 | 4.345 ms | 5.133 ms |
| Dawn-compatible executable with supplied bgfx/WebGPU DLL | 2.253 ms | 2.684 ms |
| Handwritten LiteLayer with the native C++ host, bgfx/D3D11 | **4.551 ms** | **4.924 ms** |

The native total-host CPU bracket is slower than standard bblitec by a mean
paired **130.00%** (bootstrap 95% interval **123.79% to 134.49%**), beyond the
5% practical threshold. It is slower than the previous native layer by 119.14%
(110.94% to 126.13%) and the compatibility DLL by 96.49% (90.81% to 101.82%).
Versus stock Dawn, +1.07% with an interval of -4.11% to +5.11% establishes
neither a statistical difference nor a conclusive 5% practical result.

The native core/application-update/render/submission bracket averages
**0.740 ms**, while remaining platform/HUD/cycle-collection work accounts for
**3.810 ms**, about 84% of its total. This does not individually attribute that
cost to HUD. Its real GPU queries average **1.338 ms**; 17,969 unique GPU frame
IDs remain after deduplicating 31 repeated IDs, with no unavailable samples.
The bgfx render thread's CPU work is outside the application-thread QPC bracket.
Do not compare this core-only figure with complete SDL/Dawn loop times and
claim an isolated engine speedup. HUD implementations, collection boundaries,
backend APIs and GPU profiling differ. Native startup, including runtime WGSL
compilation, has a median 3,733.8 ms and is excluded from the steady-state table.

The native executable is 7,280,640 bytes. With its baked atlas and original
icon pack, its as-built inputs total 7,941,602 bytes, versus standard bblitec's
10,567,137 bytes including adjacent DLLs/assets/shaders. System/installed VC
runtime DLLs and debug/build outputs are excluded consistently. The runtime
shader compiler is statically linked into this demo, not into `LiteLayer`.
This is not a minimized or relocatable-installer measurement; the test
application still references build-absolute asset paths.

Scene-only captures have matching camera/397 mesh records/eight materials.
The native scene is 98.995% RGB-exact against standard; 99.9925% of pixels are
within three levels per channel, with mean absolute difference 0.008979 on
the 0-255 scale. This is close rendering, not bit-exact parity. Full receipts,
hashes, commands, payload accounting and the paired-bootstrap harness are in
the investigation repository's `Experiments/Mincraft/pure-native/RESULTS.md`
and ignored `artifacts/pure-native-comparison/matched-15-rounds-20261007`.

The subsequent [performance investigation](LitePerformanceInvestigation.md)
attributes the gap principally to rebuilding the complete native HUD backing
surface every frame, not cycle collection. Opt-in retained-HUD and same-frame
material-grouping experiments, matched phase/thread measurements, independent
confirmation and implementation-agent guidelines are recorded there.
The experiments do not change the engine or host's default behavior, and
retained-HUD average improvements do not establish better tail/display latency.
The later Core rewrite adopts frame-local aggregation by default; those published
investigation numbers remain historical controls, not new rewrite measurements.

#### Final guideline-driven rewrite versus standard bblitec

A fresh fifteen-round, three-executable comparison used the final uninstrumented
rewritten production executable, qualified standard bblitec and the frozen
pre-rewrite native executable. It preserved the 180/1,200-frame, SEED 1337/radius 6,
1280x720/MSAA 1/no-input/no-vsync profile. HUD/GC, shader service, host and backend
defaults were unchanged; ambient `BBLITE_*` and `LITE_PERF_*` flags were cleared.
Absolute times from older batches must not be mixed into this comparison.

| Complete host path | Median run-average CPU frame | Median run P95 |
|---|---:|---:|
| Standard bblitec / SDL_GPU | 2.891 ms | 3.954 ms |
| Native C++ before rewrite | 4.997 ms | 6.293 ms |
| Native C++ with rewritten LiteLayer | **4.734 ms** | **6.166 ms** |

**The rewritten complete native host is still slower than standard bblitec.**
Its mean paired frame-time increase is **75.20%**, bootstrap 95% interval
**[+62.06%, +86.20%]**. Its paired P95 increase versus standard is 63.62%,
interval [+42.86%, +82.62%].

Versus our own pre-rewrite native host, mean paired time decreases 5.29%,
interval [-9.80%, -0.65%]. This is a modest statistically supported decrease,
but not a conclusive improvement beyond the 5% practical threshold. Paired P95
change is -5.28%, interval [-14.72%, +4.31%], so no tail improvement is proven.

The comparable native main-thread Core/update/submission bracket improves from
0.832 to 0.550 ms, mean paired -34.37% with interval [-37.23%, -31.51%].
About 4.184 ms remains outside this bracket, principally the unchanged HUD
rebuild identified by the earlier phase investigation. Native GPU averages
are effectively unchanged at about 1.495 ms. The grouping-specific native
controls also show about 89.7% less grouping time, but neither a Core-only
figure nor a historical retained-HUD experiment establishes a faster shipping
host than standard SDL.

The public 98-function ABI and mandatory policies are unchanged. Independent
13 integration and six final-game tests pass; final production scene/HUD pixels
match the frozen native baseline, and 180/1,380-frame ordered state/uniform-byte
qualification is exact. The final executable is 7,282,176 bytes, SHA256
`FB82A05341952ECB6CF51B1D26AC8926DD662ECCCE8C079F8E66CFACCED8D7F7`.
Receipts, fresh comparisons and hashes are in the investigation repository's
ignored `artifacts/pure-native-comparison/rewrite-vs-bblitec-15-rounds`.

#### Temporary identical-code comparison without UI

`NativeProjection/FullMinecraft/NoUiExperiment` is a separate opt-in build,
not a change to the normal demo. Its temporary input removes on-screen HUD,
FPS/debug formatting, loading/error/underwater overlays and toast work while
preserving logical block selection, all gameplay, controls, audio, save/load,
atlas canvas/RGBA texture data and the 3D highlight.

All four backends compile the **same 26 standard bblitec-produced C++ user files
and generated `application.hpp`, byte-for-byte**, verified through actual
compiler/dependency inputs. Native compatibility headers/linking and entry ABI
glue are outside those source bodies; no native-only world-probe injection,
renamed source entrypoint or removed profiling expression remains.
Standard host guards remove on-screen RmlUI layout/record/composition; the
native build never creates/renders a GDI backing surface. Texture preparation
is retained even though it reaches the original `ui:rml` feature.

Sixteen exactly position-balanced rounds, each 180 warmup + 1,200 measured
frames, use the same SEED 1337/radius 6, 1280x720/MSAA 1/fixed-delta/no-input/
no-vsync hidden-swapchain profile:

| No-UI implementation | Median run-average CPU frame | Median run P95 |
|---|---:|---:|
| Standard bblitec / SDL_GPU / D3D12 | 1.630 ms | 2.459 ms |
| Same user code / stock Dawn / D3D12 | 4.896 ms | 6.511 ms |
| Same Dawn host / compatible bgfx WebGPU DLL | 1.708 ms | 2.452 ms |
| Same user code / C99 LiteLayer / direct bgfx D3D11 | **1.672 ms** | **3.347 ms** |

Native versus SDL mean paired change is +2.36%, CI [-0.85%, +5.75%]:
no statistically established difference and no conclusive 5% practical result.
Native is 66.20% faster than stock Dawn, CI [-68.40%, -64.25%].
Versus the compatible provider, -2.06% with CI [-4.89%, +0.63%] lies within
the predefined +/-5% practical band.

**Tail latency remains worse than SDL/provider:** native paired P95 is
36.62% higher than SDL (CI [+30.72%, +42.46%]) and 37.83% higher than the
provider (CI [+31.00%, +44.80%]). This is not a blanket native-performance win.
Native asynchronous GPU query average is about 1.650 ms. Removing CPU-heavy
HUD work shifts waiting/backpressure into its Core/submission bracket;
that bracket's roughly 1.670 ms is not a new algorithm-only cost.

SDL/native brackets include GC; the Dawn reporter ends before GC. D3D11
versus D3D12 and rendering/thread/driver behavior remain explicit differences.
No old GC cost is assumed transferable to this standard emission.
Compiler-inlined audio routing prelude is classified separately from user
logic; it is shared, not rewritten, and is unreached in idle timing.
Private world/chunk/hash observations remain null rather than being fabricated
or injected into the timed user code. Actual 180/1,380-frame public
camera/material/geometry/membership and GPU qualification shows 397/403
mesh records/draws and eight materials; scene rendering is close, not RGB-exact
across backends.

Receipts/audits/captures are under `build/lite-c99/no-ui-comparison`, including
`Matched16`, `source-byte-audit.json`, `qualification-180.json`,
`qualification-1380.json` and `StandardSource/baseline-overlay.json`.
The investigation report is `Experiments/Mincraft/pure-native/NO-UI-RESULTS.md`.
No RmlUI/bgfx backend was implemented by this temporary comparison.

### Application-only native projection

`Apps/LiteTests/NativeProjection` uses an existing built bblitec checkout through
`-DBABYLON_LITE_BBLITEC_SOURCE_DIR=<checkout>`. Its first bounded profile
transpiles two actual TypeScript test applications: 10,000-cycle geometry and
hierarchy checks. Twelve explicitly annotated scalar ABI slots forward to
public C99 functions/data; their declaration bodies refuse execution and are
not compiled. Native identity bookkeeping belongs to the test adapter, not
the engine.

The custom output projection reuses bblitec's C++ token reader, retains only
the application namespace and exported user bodies, and rejects renderer/
non-scalar runtime reach. It deliberately discards the default generated
engine/PAL dependency suggestions and WebGPU library-session scaffolding.
`UserSemantics.h` provides only error values and an empty closure trace visitor;
it is not the bblitec runtime. `LiteNativeProjectedTests` links only `LiteLayer`
and gtest. Generated `provenance.json` records compiler/source/header/adapter
hashes, exact projected mappings and zero generated engine/runtime objects.
The two projected CPU tests pass; this is not full Minecraft native C++ or
complete transpiler-corpus coverage.

```powershell
cmake -S . -B build\lite-c99\integration `
    -DBABYLON_LITE_BBLITEC_SOURCE_DIR=E:\Github\bblitec-native-babylon-layer
cmake --build build\lite-c99\integration --config Release `
    --target LiteNativeProjectedTests --parallel 3
ctest --test-dir build\lite-c99\integration -C Release `
    -R LiteNativeProjectedTests --output-on-failure
```

Nonzero depth bias/slope and RGBA16F targets are currently explicitly
unsupported; importing external swap-chain backbuffer pointers is unsupported
by the pinned public bgfx API. Consult the C99 header for exact
defaults, lifecycle and failure behavior rather than inferring whole-package
support from the declared scoped functions.
