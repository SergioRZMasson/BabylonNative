# Babylon Lite native component

`LiteLayer` is a handwritten C-style implementation of the Minecraft- and
auxiliary native-cube-reached Babylon Lite 1.32.0 API, based on original TypeScript at
`2e064d88ec7422af946f8ec7f089ac6519f99295`. Its only public header is
`Core/LiteLayer/Include/babylon_lite.h`; its direct link dependencies are `bgfx`
and `bx`, plus explicitly optional `RmlUi::Core` when
`BABYLON_LITE_ENABLE_UI=ON`. It contains no JavaScript runtime, NAPI, NativeEngine, SDL, Dawn/WebGPU
renderer, generated Babylon engine or bblitec runtime.

The original Babylon Lite source is Apache-2.0 licensed. Its license is retained
in `Core/LiteLayer/LICENSE.txt` for this native port and alongside the copied
demo sources; the surrounding Babylon Native repository retains its own license.

The header is strict C99/C++17. Private implementation compilation requires
C++20 because the pinned bx public headers require it. Native users link
`LiteLayer` (`Babylon::LiteLayer` alias); bgfx's existing transitive dependencies
are carried by the static target.

## Components

### Additional native-cube slice (2026-10-08)

The reviewed additive contract now has **160 functions**: the unchanged 122
engine/optional-UI declarations plus 38 camera/control/light/material transports.
All 122 legacy signatures and public record field declarations are preserved.
Strict-C99 binary witnesses also match all 389 sizes/offsets of the 72 legacy
records against the parent-frozen header.
ArcRotate orbit/world/view math, pointer/touch inertia, live control-option
replacement and masked limits, hemispheric light packing, and untextured Standard
Blinn-Phong composition are handwritten C-style Core modules. Standard packets
retain per-scene/per-mesh versions and ordering; plain material/light color writes
still require original explicit dirty/rebuild operations. Frame-graph rebuild
requests, Standard color geometry, textures/PBR/other lights/shadows, keyboard,
viewport and orthographic features fail explicitly in this slice.

The external runtime compiler reflects semantic struct/array leaves and integer
vector components at actual offsets/strides. Standard uses the original logical
368/1040/144/96-byte scene/light/mesh/material blocks and the shared native
packing/submission path. No compiler dependency or generated engine is in Core.

`Apps/LiteTests/NativeCube` compiles the exact STANDARD auxiliary `native-cube`
user `main.cpp` through external compiler ABI headers, and separately bundles the
unchanged auxiliary TypeScript with the native shim only. This is a bblitec-owned
sample, **not registered demo-cube or original-corpus completeness**. Existing
header-only user-language containers/GC remain outside Core. GPU screenshots at
frames 1/61/121 match the frozen SDL hulls (C++ IoU 1.0; JS last pose
0.999979), with no interior C++ differences above 2 LSB. BGFX's quantized clear
blue is 77 versus SDL's 76; float C++ versus double JS rotation is disclosed.

Fresh independent current-source regression builds passed 19 UI-ON, 13 UI-OFF,
and 10 original native Minecraft cases. Neither the earlier Minecraft binaries
nor their measurements were overwritten. Commands/configuration, source/binary
hashes, actual GPU captures, source/original-TS/routing/PE audits and detailed
qualification are retained in the ignored
`build/lite-c99/demo-native-cube/` artifacts.
The CPU/GPU/compiler runners exercise 21/14/9 individual cases respectively.
Separate control fixtures cover live mappings during a gesture, atomic rejected
options, borrowed predicate replacement/error/reentry, raw/equal limits and
frame-callback replacement preserving the APPEND hook. The cube itself never
attaches controls. Eight malformed reflection cases reject with diagnostics and
exactly one service-result release per attempt, then successfully retry.

The cube's external C++ compiler-ABI adapter intentionally covers its reached
calls and one application mesh; it is not a full bblitec API/runtime replacement.
The Core material/scene implementation is independently shared-owner and
per-mesh tested. Actual executed configuration/build/test commands are indexed
in `implementation-receipts/configure-native.json`,
`final-expanded-commands.json` and `MatchedCube15/round-*/.../process.json`.
Reuse the source/configuration in a new build tree rather than overwriting
measured binaries.

The fresh 15-round/45-process MSAA4 cube comparison is **not a speedup**:
median run means were C++/bgfx-D3D11 **0.335292 ms**, original-JS/native
**0.359839 ms**, and STANDARD SDL-GPU/D3D12 **0.303000 ms**; median run P95s
were 0.5743/0.6448/0.381 ms respectively. Paired mean C++ versus SDL was
+12.58%, 95% bootstrap interval [+7.80%, +19.52%]. The observed C++ bracket was
0.00622 ms Core/user versus 0.32908 ms submission/handoff/wait and approximately
0.03003 ms real GPU execution. This identifies native renderer handoff as the
dominant measured category, not an algorithm-cost or universal backend claim.
JS includes VM dispatch/completion wait. SDL's standard summary provides no
process/GPU breakdown; unavailable fields are not zeros. No Minecraft timing,
workload reduction, capture/input, concurrent compilation or outlier removal is
used in this result. Raw protocol, run-level confidence intervals and limitations
are in `MatchedCube15/summary.json`.
A separate six-round alternating host diagnostic selects bgfx's supported
same-API-thread rendering without changing Core, shaders or workload. It
reproduces all three GPU poses, but does not reliably improve C++ timing and
worsens JS timing; it is **not adopted** and does not replace the 45-process
comparison. Its commands/raw results are in `HostHandoffExperiment/`.

The parent independently reran the 19/13/10 regression suites, strict Core
style checks and cube C++/JS GPU/semantic fixtures, and verified 541 indexed
fingerprints plus the unchanged legacy signatures. See
[LiteNativeCubePerformance.md](LiteNativeCubePerformance.md) for the accepted
functional scope and explicitly unmet cube performance target.

| Target | Responsibility |
|---|---|
| `LiteLayer` | Native transforms, geometry, scenes, materials, rendering/resource lifetime, audio routing and optional retained RmlUI/bgfx UI. |
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
| `LiteUiTests` | Optional native retained UI lifecycle, input, real runtime shaders and GPU pixel qualification; `--visible` presents/captures a real bgfx swapchain sample. |

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

### Optional retained UI infrastructure

The optional UI feature lives **inside `LiteLayer`**, in isolated `UiGpu.cpp`,
`UiRmlAdapter.cpp`, `UiInternal.h` and a disabled-feature C99 stub. Native records,
identity/resource storage and GPU algorithms remain C-style/POD. The four private
mandatory RmlUI interface adapters are the narrow third-party interface exception;
RmlUI/FreeType implementation and their containers remain dependency code.
No RmlUI/STL pointer, class, method table or native shader format enters the
single public C99 header.

The dependency declarations use the repository's FetchContent patterns:

* RmlUI upstream `mikke89/RmlUi`, source
  `b7b4a0688262832eacf3b9abb41f8bbe73868af8` (6.4 development, MIT).
* FreeType source `42608f77f20749dd6ddc9e0536788eaad70ea4b5`
  (2.13.3, upstream FTL/GPL licensing).
* FreeType compression/image/shaping extras and RmlUI samples/Lua/SVG/Lottie
  are disabled in dependency-local configuration scope. This does not change
  unrelated BabylonNative dependency/install/shared-library options.

Source-cache overrides must contain these exact sources, either pristine or with
only the reviewed renderer-scoped box-shadow cache patch.
The compiler checkout's RmlUI cache has additional patches; its private
prebuilt libraries are not this component's dependency. Dependency licenses
remain in their source trees. The unit font is upstream's Lato Latin Regular,
with provenance/license in `RmlUi/Samples/assets/LICENSE.txt`; no font is
silently substituted or vendored into Core.
`Dependencies/RmlUi/Patches/BoxShadowRenderManager.patch` is MIT-licensed and
does not change the base pin, geometry/style/hash data, blur or quality.
It scopes the otherwise process-global shadow cache by owning render manager,
retains stable geometry-key references and erases empty manager buckets.
`ApplyBoxShadowPatch.cmake` verifies the pin/identity files and exact canonical
LF before/after hashes, applies cached overrides too, accepts the patched pair
idempotently and rejects mismatched/partial states. `RmlUiBoxShadowPatch.json`
records each build's source, patch digest and verified postcondition; failed
verification cannot leave a stale successful receipt.

Root builds enable `BABYLON_LITE_ENABLE_UI=ON` with
`BABYLON_NATIVE_BUILD_LITE_LAYER=ON`.
`BABYLON_NATIVE_BUILD_LITE_UI_TESTS=ON` additionally requires the separate
`LiteShaderCompiler` target. Standalone `Core/LiteLayer` also supports the UI
flag and the same source pins; UI OFF does not fetch RmlUI or FreeType.

The new C99 context attaches to a **BORROWED** engine and the same host-owned
target, reserving an exclusive later view range outside every engine/context
reservation. Host ordering is:

1. Dispatch native input and original user updates/3D callbacks.
2. Submit 3D through the borrowed Lite engine.
3. Call `bl_updateUi` with process-wide nondecreasing seconds, then `bl_renderUi`.
4. Advance/present bgfx **once**, in the host.

UI never calls `bgfx::frame`, resets/presents bgfx or owns an OS event loop.
It preserves the target's 3D color/depth. Rounded/transformed clip masks use
D24S8 stencil; rectangular scissoring needs no stencil. Static geometry, texture
and font atlas uploads are retained. Resource release uses bgfx's ordered
destruction commands, not guessed frame counts or a per-text-update GPU stall.
Context teardown uses the existing real submitted-work synchronization callback.
Engine disposal returns BUSY while UI reservations remain; runtime cascading
disposal destroys UI first.

Contexts use generation-protected element identities and own detached subtrees.
Append rejects cycles, already-attached children and cross-context identities.
Remove detaches without disposing; text/markup replacement invalidates replaced
descendant handles. Plain text is escaped; identical repeated text setters avoid
rebuilding geometry. Markup is RML, not browser HTML. Narrow click/change/pointer/
key listeners provide C99 values and tokens. Callbacks may change style/text/
attributes, but must not throw; nested frames/input, topology/listener mutation
and disposal during dispatch return BUSY.

RmlUI is process-global: all UI contexts share one creating thread and clock.
Different runtimes on that thread are supported; externally configured RmlUI
cannot coexist with this owned UI lifecycle. Registered font data is copied,
deduplicated by family/weight/style/data and retained until LAST UI shutdown.
Native identities/staging/resources use the runtime allocator. RmlUI/FreeType
allocations and retained font bytes are explicitly **external** to its counters.
An external allocation failure during non-transactional RmlUI global startup
makes UI unavailable in that process, rather than attempting unsafe reuse.

The backend invokes the **injected runtime WGSL compiler** for authored UI
color, textured and gradient programs. It validates v12 containers, linked
attribute semantics (position2/color4/UV2), uniform offsets/types/counts/native
packing and sampler reflection. Results are released once for every invocation,
including failures. No Tint/compiler/VM dependency enters `LiteLayer`.
UI colors/textures use display-encoded RGBA8 premultiplied-alpha composition;
registered/IO straight-alpha images are converted once before uploading.
Registered sources default to linear sampling.
`bl_setUiImageSampling(context, source, true)` selects real point
minification/magnification before RmlUI retains that source's texture. Retained
sources return BUSY even for an identical setting; unknown sources are invalid.
It is an immutable-source sampler choice, not per-element CSS or a font-atlas
override. A browser `image-rendering: pixelated` bridge must register/select the
appropriate source before referencing it in RML.

Current supported scope: retained elements, RML, properties/attributes/plain
text, physical viewport and RmlUI `dp` density, real font atlas/images,
source-over alpha, translation/scale/matrix transforms, rectangular and stencil
clipping, linear/radial gradients with 2–8 resolved stops (including repetition),
and the documented narrow input/event transport.
The approved white-only context composition is
`bl_setUiWhiteDifference(context, true)`: exact white premultiplied difference
using inverse-destination-color/source-over blend factors, retaining source-over
alpha. Color geometry/images/gradient stops and layers are explicitly rejected
in that mode. Hosts use a separate ordered context for the crosshair; this is
not an approximation for arbitrary colored-source difference.

The private backend now records real cached offscreen RGBA8/D24S8 layers,
ordered reserved-view passes, source-over/replace composition, cropped GPU
saved textures and separable Gaussian blur. The Gaussian kernel uses a normalized
discrete three-sigma extent; paired bilinear samples implement the same weighted
neighbor taps, not a lower-resolution gradient or CPU shadow raster. Sigma
0–512, up to eight layers and one blur filter per composite are checked limits.
Texture dimensions and the granted view budget are checked before use.
Actual layer resources are reused; resize retires old GPU resources through
bgfx ordering. The real runtime compiler supplies the additional WGSL program.
Unknown properties and unsupported renderer effects fail explicitly.
Missing images/fonts/services and retained effect failures are latched: later
update/render calls keep reporting the error instead of silently omitting cached
content. Recreate a failed context to recover.

**Host translation limits matter.** At this pristine RmlUI pin, `rgba()` alpha
is **0–255**, not browser 0–1; hex RGBA or percentage alpha avoids that ambiguity.
RmlUI `px` are physical; `dp` scale with `densityRatio`. Hosts translating browser
logical CSS must convert appropriate units. Absolute overflow may need
`clip: always` to preserve browser clipping. Browser background shorthands,
`cssText`, system-font fallback and text-shadow need deliberate host translation
to actual RmlUI properties/font effects/resources. Complex filter chains,
mask images, conic gradients, clipboard and general colored-source browser
`mix-blend-mode:difference` remain unsupported. The original underwater
inset shadow is implemented through RmlUI's actual layer/blur/stencil/save
commands. There is
no full Minecraft/browser DOM parity claim and no placeholder white texture,
GDI backing surface or silent difference/filter approximation.

The exact 1280×720 sigma-110 inset-shadow fixture passes center/edge color/alpha
and a Gaussian numeric reference, zero/half/full opacity, resize/density
invalidation, native allocation failure and reserved-view exhaustion checks.
Across 1000 static frames, texture creations remain 9; draw count falls from
16 cold to 2 warm. These are resource/command counters, not a complete-host
timing claim.
The reviewed patch resolves an upstream-pinned multicontext issue: RmlUI's process-global
shadow cache omits render-manager identity, so identical shadows can reuse the
first context's geometry/texture. The regression now passes in Release
and Debug: simultaneous identical shadows match the single-context pixels,
destroying the first owner leaves the second valid, recreation and sixteen
mixed-context churn cycles preserve exact pixels and per-context submissions.
The immutable original failure evidence remains in
`rml-shadow-cache-defect.json`; patch application/idempotence/mismatch rejection
is also an executable test. Single-context original underwater rendering
has been qualified independently; the host's complete original-input audit is
owned by its separate integration agent.

Fresh infrastructure Release/Debug/UI-OFF evidence is under
`build/lite-c99/rmlui-infrastructure`, not historical qualified binary trees.
`LiteUiTests` verifies actual pixels over a 3D box for font/image/alpha/gradient/
scissor/transform/rounded-stencil/DPI cases, unchanged-frame retention, live
mutations/events, 1000-node churn, stale identities, native allocation failure,
multiple contexts/runtimes, view conflicts and compiler-result ownership.
Point-versus-linear pixels use the same two-color image data; failed native
image-registration allocations leave no issued source and permit clean retry.
Host read/decode failures release exactly the results whose callbacks were
invoked, and attempts to re-enter Core during the IO callback return BUSY.
Failed submitted-work synchronization leaves the retained context/resources
alive and unchanged, rather than prematurely destroying or changing samplers.
The 1000-node warmup/regrowth check requires exact native allocator plateau;
process private-byte growth also stays within a disclosed 2 MiB allowance.
The latter includes driver/test/library heaps and is not a precise RmlUI-only
allocation counter or a claim about arbitrary UI workloads.
The visible sample captures the actual swapchain to
`Release/UiTests/ui-visible-swapchain.ppm`; offscreen captures have separate
initial/mutated/DPI files. Original native CPU/GPU/implementation tests are
recompiled in this new tree, and the UI-enabled data-only map contains only
the original four CPU modules, with no UI/font/renderer/compiler/VM objects.
This is infrastructure/component evidence, not a fresh full-host benchmark,
binding qualification or full demonstration corpus result.

Executed gates: six Release CTests (UI plus dependency-patch and recompiled original CPU/GPU/
implementation/data-only tests), three Debug CTests (UI/patch/data-only), strict C99
header compilation, LLVM 22 formatting/AST checks including interface methods,
and eight style-checker unit tests. UI-OFF builds with no RmlUI/FreeType source
population; its independently linked data-only executable passes the same
four-CPU-module map audit. `source-link-audit.json` records unchanged original
98 C99 signatures, unchanged non-UI engine bodies except the minimal engine
view-reservation hooks, and actual linker/source hashes.
The Debug Core/RmlUI build is unoptimized with symbols and library assertions,
using Release CRT/iterator ABI to match the independently imported qualified
shader compiler. This is not a Debug build of Tint or the shader service.

Standalone infrastructure reproduction uses exact-pin source caches, an existing
qualified shader-service library tree and its exact-pin Tint libraries:

```powershell
# Run configure/build in the same vcvars64.bat command process; use jobs 3.
cmake -S Core\LiteLayer -B build\lite-c99\rmlui-infrastructure\Release -G Ninja `
    -DCMAKE_BUILD_TYPE=Release -DBABYLON_LITE_ENABLE_UI=ON `
    -DBABYLON_NATIVE_BUILD_LITE_UI_TESTS=ON -DBABYLON_LITE_ENABLE_STYLE_CHECKS=ON `
    -DFETCHCONTENT_SOURCE_DIR_LITEBGFX=<exact-bgfx.cmake-source> `
    -DFETCHCONTENT_SOURCE_DIR_RMLUI=<unmodified-pinned-RmlUI-source> `
    -DFETCHCONTENT_SOURCE_DIR_LITEFREETYPE=<pinned-FreeType-source> `
    -DLITE_UI_COMPILER_LIBRARY_DIR=<qualified-Release-native-build> `
    -DLITE_UI_TINT_LIBRARY_DIR=<exact-pin-Release-Tint-libraries>
cmake --build build\lite-c99\rmlui-infrastructure\Release `
    --target LiteUiTests LiteUiHeaderC99 LiteUiDataOnly --parallel 3
ctest --test-dir build\lite-c99\rmlui-infrastructure\Release --output-on-failure
Push-Location build\lite-c99\rmlui-infrastructure\Release\UiTests
.\LiteUiTests.exe --visible
Pop-Location
```

The optional `LITE_UI_CORE_REGRESSION_TESTS=ON` uses existing source-pinned
GoogleTest via `LITE_UI_GTEST_SOURCE_DIR`/`LITE_UI_GTEST_LIBRARY_DIR` to recompile
the original Core CPU/GPU/implementation tests without altering them or
historical binaries. Root integration uses its normal GoogleTest/shader targets.
This task does not qualify the original thirteen full integration CTests or
six native Minecraft CTests against a new full-UI host; that is a separate
coordinated host/binding qualification, not an inference from component passes.

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

### Original JavaScript with retained native UI

With `BABYLON_LITE_ENABLE_UI=ON`, `LitePlayground minecraft` now uses the
published Core C99 RmlUI/bgfx implementation, **not** the GDI HUD described in
the historical host qualification above. `--legacy-gdi-ui` explicitly selects
the regression path; UI OFF retains that path. Original Minecraft sources,
all 24 modules, shaders and assets are unchanged. Application-only bundling
continues to reject engine implementation inputs.

The JS creating thread initializes bgfx and performs every C99 engine/UI call.
Its engine is BORROWED: `bl_frame`, UI update/render in crosshair/HUD/underwater
order, then **one** host `bgfx::frame`. Disjoint view ranges are engine 0–31,
white-difference crosshair 32–63, HUD 64–159 and underwater 160–255.
Window/message ownership remains on the OS thread. Submitted-work retirement
uses actual GPU readback synchronization, not an assumed number of frames.
Bounded runs reject physical/raw input; native-window replay is explicitly
marked scripted. Hidden runs do not capture the desktop mouse or change focus.

`LiteJSBinding` marshals all 24 UI functions with typed, generation-protected
opaque identities, copied byte spans and native status/source diagnostics.
Native contexts own attached/detached elements, so collecting a JS parent
wrapper does not detach its subtree. Listener identities avoid JS-number
pointer/uint64 truncation and stale-token aliasing. Original callback exceptions
are restored only **after** the C callback returns. `nativeUi` and `nativeExtras`
are explicitly native embedding extensions. Static routing reaches all current
122 C99 functions: 120 in the binding and two runtime-lifecycle functions in
the host. This is not semantic coverage of 1814 upstream exports.

The outside-Core `UiCss` frontend adapts browser CSS to Rml values, including
RGBA percentages, DPI-scaled units, fonts, images, borders, gradients, shadows
and the exact opacity `ease` curve/coalescing/reversal. RmlUI performs layout,
clipping, retained effects and rendering. Installed Segoe UI/Consolas fonts
are loaded through copied C99 font bytes; no licensed fonts are committed.
FreeType glyph rasterization differs from the historical Windows/GDI renderer.
The narrow DOM host is not a complete browser: it supports one engine,
plain text/explicit RML markup, native click/change/textbox transport and the
documented CSS subset. Other font stacks, transitions and background shorthands
fail explicitly. Colored-source difference and unsupported Core features
retain native errors; there are no approximate filter chains or PBR claims.
Non-canvas element bounds are not exposed by this C99 contract; the DOM host
throws rather than estimating browser layout. Clipboard/IME/full browser DOM
and asynchronous event-handler semantics are not claimed.

New builds/evidence belong under `build\lite-c99\js-rmlui`:

```powershell
cmake --build build\lite-c99\js-rmlui\Release --target LitePlayground LiteJavaScriptCssTests --parallel 3
ctest --test-dir build\lite-c99\js-rmlui\Release -R Lite --output-on-failure
cmake --build build\lite-c99\js-rmlui\Release --target LiteJavaScriptUiHostStyleCheck
```

`LiteJavaScriptUiContract` covers forced-GC parent retention, copied image
inputs, stale/wrong-type identities, native callback reentry/exception identity,
generic DOM click and textbox/change/value transport. `LiteMinecraftUiInvalidation`
uses original controls/save/load/audio plus explicitly **validation-only**
resize/synthetic DPI, forced GC, underwater visibility and toast expiry.
Headless save/open automation supplies a declared test path without showing a
desktop dialog; the separate visible native-input test still exercises real
Win32 dialogs. Receipts distinguish file selections from completed dialogs.
`LiteMinecraftCombinedGpu1380` captures combined 3D/native UI at 180/1380 with
fixed simulation steps. These overrides are rejected by the timing selector.

`--benchmark-output=<file> --warmup=180 --measure=1200 --headless` writes
`lite-js-rmlui-frames-v1` raw samples after the run, with no measured captures,
hashing, file output or physical input. It separates API/complete-host wall
spans, nested user callbacks, UI update/render, presentation, Windows process/
thread execution counters and deduplicated real GPU queries. Missing queries
are null; 64-bit timestamps are decimal strings. Callback spans include native
setters; subtracting them is **not** total engine-algorithm CPU. GC remains
normal V8 behavior, included but not separately instrumented. This JS host
retains source-default MSAA 4; the published C++ comparison used MSAA 1.
No JS performance conclusion follows from that C++ comparison.
`JavaScript\tools\audit-js-rmlui.py` records exact binary/config/source hashes,
checks the unedited 27-file source slice and qualifies resource/query receipts.
Parent validation subsequently ran a fresh 15-round, five-path comparison at
matching **MSAA4**: original JavaScript averages 1.544 ms/frame, native C++
1.521 ms and SDL/bblitec 2.767 ms (medians of run averages). The JS/native C++
paired mean interval [+0.73%, +3.96%] is within the practical 5% band, but JS
whole-process execution is higher and its VM/DLL footprint is substantially
larger. Full results, P95, rendering/runtime-pin caveats and frozen evidence
are in [LiteJavaScriptPerformance.md](LiteJavaScriptPerformance.md).

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

### Original STANDARD C++ Minecraft with retained RmlUI

`NativeProjection/FullMinecraft/RmlUiExperiment` is a separate, explicit opt-in
native host. It compiles the **same unedited 26 STANDARD compiler-produced user
translation units and generated `application.hpp`** used by the fresh full-UI
SDL/Dawn/provider baselines under `build/lite-c99/rmlui-minecraft/Common`.
`main=MinecraftApplication` is only a user-TU compiler entrypoint adaptation.
Material descriptions are rehydrated manifest **data**, outside those files;
the shared compiler-inlined audio routing prelude is separately classified,
not advertised as exclusively user-authored logic.

The target builds current UI-enabled `LiteLayer`, bgfx, RmlUI and FreeType
through their source/transitive CMake targets, not a mixture of old Core
archives. Only the independently qualified runtime shader compiler/Tint archives
are imported outside Core. It has no NAPI, JS VM, NativeEngine, generated engine
or legacy PAL renderer objects. The host reuses existing outside-Core Win32
input/file/dialog/WIC/XAudio transport. **This is Win32, not SDL3.** It does not
create a GDI overlay/backing bitmap, scan/copy a complete HUD surface, remove
the original HUD or replace the game with a reduced scene.

The browser/standard-compiler style front end retains C99 DOM identities and
forwards actual text/style/child mutations to LiteLayer. It translates logical
pixels to density-scaled `dp`, exact decimal browser alpha to RmlUI percentage
alpha, the original font stacks, background metadata and text shadows. Original
hotbar PNGs are decoded once with WIC and registered with **point** sampling
before texture retention; their logical URLs have stable registered aliases.
Absolute help/toast content uses the browser's remaining-containing-block
shrink-to-fit width instead of an unbounded pristine-RmlUI inline width.
The original `opacity Ns ease` transition is evaluated in the outside-Core
browser-style front end as CSS `cubic-bezier(.25,.1,.25,1)`, including initial
style, unchanged targets, same-frame coalescing and reversal shortening.
Unknown/unrepresented style requirements fail rather than silently disappearing.

The source-authored white crosshair difference effect is restored at the binding
boundary: STANDARD CSS lowering omitted it in the reference baselines. Three
disjoint retained white rectangles preserve its union/opacity without blending
the center twice. They use the approved white-only difference C99 context,
not normal white drawing or framebuffer readback. View reservations/order are
3D `0..31`, crosshair `32..63`, normal HUD/underwater `64..255`; insufficient
renderer view capacity is an explicit startup failure. The original underwater
radial gradient/inset shadow remains real Core bgfx layer/blur/saved-texture work,
not a CPU raster or replacement gradient. Native updates submit 3D, update/render
both UI contexts, and call `bgfx::frame` **once**. Captures are requested on the
same submit packet before that advance; extra fence/readback frames are outside
benchmarks.

The host uses installed, hashed Segoe UI/Consolas/Symbol faces without committing
or redistributing private font binaries. Pristine RmlUI/FreeType rasterization
differs from the compiler host's Windows font engine. Restored crosshair
composition also intentionally differs from the stripped reference rendering.
**No RGB-exact full-UI parity is claimed.** The fresh performance results below
do not establish an isolated engine speedup or solved tail latency. This app does not
establish general arbitrary colored-source difference, all browser CSS or
complete demonstration-corpus coverage. The final source-transitive builds
include the separately qualified, pin-preserving renderer-scoped shadow-cache
correction. Each build's `RmlUiBoxShadowPatch.json` is independently checked by
the app source audit; general multi-context shadow qualification remains the
Core test's evidence, not an inference from this app's one shadow-producing context.

Configure in the same Visual Studio developer command process, with the
exact-pin source-cache paths and qualified shader-library paths described in
the UI infrastructure section:

```powershell
cmake -S Apps\LiteTests\NativeProjection\FullMinecraft\RmlUiExperiment `
    -B build\lite-c99\rmlui-minecraft\NativeRelease -G Ninja `
    -DCMAKE_BUILD_TYPE=Release -DLITE_MINECRAFT_RMLUI=ON `
    -DBBLITEC_SOURCE_DIR=<built-standard-compiler-checkout> `
    -DLITE_COMMON_EMITTED_DIR=<root>\build\lite-c99\rmlui-minecraft\Common\Generated `
    -DLITE_UI_COMPILER_LIBRARY_DIR=<qualified-native-shader-library-root> `
    -DLITE_UI_TINT_LIBRARY_DIR=<exact-pin-Release-Tint-library-root> `
    -DLITE_JSON_INCLUDE_DIR=<nlohmann-include-directory> `
    -DFETCHCONTENT_SOURCE_DIR_LITEBGFX=<exact-bgfx.cmake-source> `
    -DFETCHCONTENT_SOURCE_DIR_RMLUI=<declared-pinned-RmlUI-source> `
    -DFETCHCONTENT_SOURCE_DIR_LITEFREETYPE=<declared-FreeType-source>
cmake --build build\lite-c99\rmlui-minecraft\NativeRelease --parallel 3
ctest --test-dir build\lite-c99\rmlui-minecraft\NativeRelease --output-on-failure
.\build\lite-c99\rmlui-minecraft\NativeRelease\LiteMinecraftRmlUiNative.exe
```

Ten Release tests and the same ten Debug tests qualify translation/tools,
1000 unchanged retained UI frames with native click/text/border/scale/font/DPI
mutation, original 180/1380-frame 397/403-mesh idle runs, the explicitly separate
37-event Win32 replay, real save/load dialogs and nonzero device audio, original
toast timer expiry through frame 500 and numerical exact-ease/reversal/coalescing,
resize/DPI, and the source underwater overlay over the actual GPU scene.
The underwater case is explicitly a **UI-only opacity override**, not evidence
that the player entered water; it is forbidden in benchmark mode. Captures,
receipts and stronger DOM/control guards are kept in unique build-local
`Cases` directories. The Debug source graph has symbols/unoptimized code and
assertions, but uses Release CRT/iterator ABI to match the imported qualified
shader service; it is not a Debug Tint qualification.

The full original idle UI has an exact retained-resource plateau at frames
480 and 1380: 325 geometry compilations, 25 texture creations, 1,298,760 uploaded
bytes, 53 live geometries, 25 live textures and 34 C99 elements. These are actual
backend counters, not a claim about every RmlUI/driver heap allocation.

The parent independently reran all ten Release and ten Debug tests and verified
every measured binary/source/qualification fingerprint. A subsequent sixteen
balanced four-variant comparison used the original UI, common standard C++
files, 180 warmup + 1,200 measured frames, SEED 1337/radius 6, 1280x720/MSAA 1,
fixed simulation delta and hidden real swapchains with no input/vsync/pacing:

| Full original-UI implementation | Median run-average CPU frame | Median run P95 |
|---|---:|---:|
| Standard bblitec / SDL_GPU / D3D12 | 2.676 ms | 3.571 ms |
| Stock Dawn / D3D12 | 5.445 ms | 7.393 ms |
| Same Dawn host / compatible bgfx WebGPU provider | 2.824 ms | 3.651 ms |
| Native LiteLayer + RmlUI / bgfx D3D11 | **1.751 ms** | **3.292 ms** |

Native mean paired frame time is 21.06% lower than SDL, bootstrap 95% interval
[-35.68%, -2.75%]. This excludes zero but does **not** robustly establish a
benefit greater than the 5% practical threshold. It is 64.09% lower than stock
Dawn ([-70.24%, -56.04%]) and 27.65% lower than the compatible provider
([-41.04%, -12.48%]).

**Run variability is substantial and no P95 win versus SDL/provider is proven.**
Native paired P95 versus SDL has interval [-8.73%, +105.14%], despite its lower
median in the table. All runs were retained; none was silently excluded.
Shared-workstation load, driver queues and D3D11 versus D3D12 remain unresolved
confounds, alongside glyph-renderer and collection timing-boundary differences.

Measured native RmlUI update/layout averages 0.0158 ms and draw recording
0.0673 ms. UI geometry/texture/upload counters change **zero** times during
all sixteen measured native windows. The Core/update/3D bracket is 0.4721 ms;
presenter/handoff is 1.1989 ms, including about 1.1920 ms waiting for render.
Whole-process execution is 1.9141 ms/frame and GPU queries average 1.7247 ms.
Do not add overlapping spans or call a short enqueue timer whole-engine CPU.
The frozen detailed evidence is `build/lite-c99/rmlui-minecraft/Matched16`;
the investigation report is `Experiments/Mincraft/pure-native/RMLUI-RESULTS.md`.

`Tools/audit.py` checks actual Ninja compiler/header dependencies, unchanged
source bytes, source-transitive Core/UI objects, PE imports and font/binary
hashes. `Tools/verify-run.py` rejects missing/empty UI, fabricated private world
observations, absent icons/selection/F3/help/toast changes, stale captures and
unmatched timing arrays. Fresh receipts refuse overwrites.

```powershell
.\build\lite-c99\rmlui-minecraft\NativeRelease\LiteMinecraftRmlUiNative.exe `
    --benchmark-output=<fresh-absolute-output.json> --warmup=180 --measure=1200
```

The benchmark interface retains SEED 1337/radius 6, all original user updates,
1280×720/MSAA 1/no-vsync/hidden **real swapchain**, fixed simulation and explicit
virtual UI/timer clocks. It rejects physical input, validation hashing,
capture/readback, save/load, resize and diagnostic UI overrides. Raw totals
include platform timers/audio polling, original C++ update/Core 3D, retained
RmlUI update/render, bgfx advance/present and user GC. OS polling/script dispatch,
file output and shutdown are explicitly outside that bracket. Separate raw
Core, RmlUI update/render, presenter, process/main-thread execution,
renderer-submit/wait and GPU frequency/frame-ID samples retain their different
scopes. Unavailable GPU timings are `-1`; repeated GPU IDs must be deduplicated.
No per-frame JSON or file output occurs. Parent-controlled balanced full-host
comparisons, profiling-overhead checks and tail/latency acceptance remain
separate from these functional qualifications.

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
