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
projected application tests. The complete bblitec corpus and full Minecraft's native
C++ application projection are still unfinished. Passing the scoped Minecraft workload
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
