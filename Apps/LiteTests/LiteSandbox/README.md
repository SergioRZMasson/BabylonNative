# Native GLB sandbox

The accepted geometry revision includes sparse accessors, vertex colors,
triangle-strip/fan conversion and non-blocking authored cameras. A parent review
also removed a spurious rejection of a valid single strided element whose view
omits unused tail padding. Existing API functions and prior record layouts remain
unchanged; the optional decoder explicitly advertises the additive raw v4 data.

`LiteSandbox` is a plain native Win32 application calling the C99 Babylon Lite API.
It uses no browser, JavaScript runtime, generated model plan, or NativeEngine adapter.
Loading is synchronous. The default mode stays open until its window is closed.

## Build and run

Enable these options in a configured BabylonNative build:

```text
BABYLON_NATIVE_BUILD_LITE_LAYER=ON
BABYLON_NATIVE_BUILD_LITE_SHADER_COMPILER=ON
BABYLON_NATIVE_BUILD_LITE_ASSET_DECODER=ON
BABYLON_NATIVE_BUILD_LITE_SANDBOX=ON
BABYLON_LITE_ENABLE_GLTF=ON
```

Build the `LiteSandbox` target. The current renderer service requires D3D11.
`LITE_SANDBOX_ASSET_DIRECTORY` may point to an existing licensed directory containing
`environmentSpecular.env` and `brdf-lut.png`; CMake copies these lighting resources
to the executable's `Assets` directory. Alternatively:

```powershell
.\LiteSandbox.exe --assets 'E:\path\to\lighting' 'E:\models\example.glb'
```

An initial model argument is optional. Drag files from Explorer onto the visible
window. Multiple dropped files are processed in order. The title reports loading,
source mesh/vertex/material counts, actual draws, and failures. A failed replacement
retains the previous model. Left-drag orbits and the wheel zooms using the existing
native ArcRotate control. The initial host has a fixed 1280×720 client area.

## Current input scope

With `BABYLON_LITE_ENABLE_GLTF=ON`, this sandbox explicitly selects the private
native cgltf importer. It loads GLB2 or JSON glTF, including supported data/external
buffers and PNG/JPEG images through the existing host IO/image services.
`bl_loadGltf` itself defaults to the unchanged legacy service route for other
consumers; feature-OFF sandbox builds also retain that route.
Native loads own a copy of caller bytes through parsing/construction. The
sandbox resolves relative external resources against the selected file's folder
in `--memory` mode, without changing process CWD or the frozen byte-source record.
Other embedding callers provide this base-folder policy through their IO service.
The native UI counts actual selected native nodes/primitives, not a second raw
parser projection; file-level source counts are unavailable on that route and
smoke receipts mark `sourceCountsAvailable=false` and
`sourceSceneIndexAvailable=false`; legacy-only source fields are placeholders,
not inferred parser metadata. Empty-scene detection still
uses the actual imported hierarchy. The native route also loads skeletal skins
and core node-TRS clips directly into retained Core-owned workspaces, without a
raw-v6 parser protocol. Historical static and paused-motion evidence is preserved.

### Animation controls and deformation

Every source clip is retained in its original order. On successful load the first
clip is selected, evaluated at0 and automatically plays with looping enabled.
The persistent native toolbar displays clip ordinal/count/name, Play/Pause,
Restart, Loop and time/duration. Its Paused versus Stopped label uses Core's
`isStopped`, not a host guess based on time0. Selecting a clip (or Restart)
exclusively resets/evaluates that clip at0; no clips are discarded. Interactive
elapsed time is supplied to C99; clocks, interpolation and pose math live in Core.
Synchronous replacement time is not charged to a retained model's clock.

Core supports STEP, LINEAR and CUBICSPLINE translation/rotation/scale channels,
including normalized integer rotation outputs and shortest-path quaternion LINEAR.
Each sampler clamps to its own range; clip time starts0 and duration is the largest
last input time. Non-loop playback holds the endpoint while still marked playing;
Restart starts again. Pause holds time/pose. The C99 stop operation resets the
clock but holds the last published pose until play/evaluation or explicit seek.
Zero-duration clips remain0. Selection restores clip-targeted rest properties
before applying the new clip, while non-animated properties remain unchanged.

Skinning is genuine native CPU deformation of position/normal/tangent streams
and actual dynamic GPU buffers. All legal paired influence sets participate;
there is no small joint-UBO cap or half-float bone matrix path. Skin without any
clip is evaluated too. The joint-root/IBM palette excludes the synthetic root,
whose reflection/world transform is applied once when drawing. The source mesh
node and ordinary/joint/camera children remain; its transform is ignored only as
an extra transform on its own skinned geometry. Deformed bounds frame/cull/order
the actual drawable, not a bind-pose proxy.

Normal mapping retains original Lite's linear N/T/independent-B convention,
not inverse-transpose normal parity. Mapped authored tangents use a private
mutable RGBA32F bitangent texture and real vertex-index fetch; transformed T is
not renormalized or B reconstructed by a cross product. Flat/cotangent fallback,
MR/SG, vertex colors, all common maps and alpha remain supported.

Imported motion topology/child arrays are protected against public reparent/list
edits; moving/parenting the synthetic import root is allowed. Shared scenes have
one Core clock using the earliest registered owning scene. Last-owner removal
still retires mesh identities; re-adding disposed geometry is not resurrection.
Public geometry writes on skinned drawables fail before mutation. These are
explicit native ownership adaptations, not a full skeleton/bone-override manager.

`--exercise-clips` is an optional deterministic qualification mode: it uses the
actual Win32 controls for every loaded clip, checks pause/restart/loop/end states
through Core, and records `clipCount`, `selectedClip` and `clipControlEvents` in
smoke receipts. Normal interactive runs remain open until closed. Smoke frames
retain their fixed delta; this mode is not a performance benchmark.

The legacy service executable loads raw self-contained GLB2 files through `bl_loadGltf`, with
arbitrary file paths and names. `--memory` exercises the same loader's raw byte-source
route. There is no asset-name/hash/count dispatch or regeneration step.

Implemented static geometry includes multiple nodes, meshes, primitives and
PBR materials; ordinary TRS and locked affine matrices; mirrored-node winding;
double-sided surfaces; tightly packed/interleaved FLOAT attributes; normalized
U8/U16 UVs; U8/U16/U32 indices and nonindexed triangles. TRIANGLE_STRIP and TRIANGLE_FAN
are expanded into native triangle lists with alternating/cyclic winding and retained
degenerate triples. Source POSITION, NORMAL, TEXCOORD_0/1, TANGENT and COLOR_0 streams
are retained. RGB/RGBA FLOAT and normalized U8/U16 vertex colors become linear F32 RGBA;
RGB supplies alpha 1. Colors multiply base RGB and MASK/BLEND coverage once, never
emissive or a second sRGB transfer. OPAQUE still outputs alpha 1.

The legacy sandbox explicitly requests the additive raw-service/document v5 protocol,
which retains the full nested v4 geometry/accessor storage.
Sparse accessors retain their original raw descriptors and are resolved in Core:
optional zero base, packed/interleaved base, checked U8/U16/U32 sparse indices and
tightly packed value replacements, final typed bounds and normalization. Provider
Accessor ranges end at the final element, not at a full trailing stride: a single
12-byte VEC3 element may use stride 16 in a 12-byte view, and interleaved final
elements need no extra trailing padding. Stride limits and checked overruns remain.
v2/v3 defaults/layouts remain unchanged and still reject sparse inputs explicitly.
Camera and texture behavior is retained across v4; environment parsing stays v2.
Legal JSON-only GLBs need no BIN chunk when they declare no buffer. Empty assets,
absent scenes and camera-only scenes load successfully; a declared buffer still
requires its complete BIN payload, and invalid references/ranges remain errors.
Core retains the synthetic root and selected-node names/TRS/children even when
there are no renderable meshes. v4's already supported implicit-zero accessor
path also works without a buffer; this does not invent missing declared data.

The host counts primitives reachable from the selected scene separately from
file-level meshes, including unused meshes or other scenes. A selected scene
without primitives replaces the previous model and renders only its clear color,
with **zero actual draws**, not a placeholder. It needs no PBR lighting upload.
Its explicit host camera policy is an orbit camera centered at `(0,0,0)`, radius
`1`, alpha `-pi/2`, beta `pi/2`, near `0.01`, far `1000`. Native controls remain
attached. Nonempty scenes still use `bl_createDefaultCamera`; its public/source
contract is unchanged, and other camera/render failures still propagate.

Missing normals keep the original generated CPU storage, but imported PBR derives
flat face normals, including its mapped-normal fallback. Authored tangents are
ignored for shading when NORMAL is absent. Shared materials select per-mesh
color/flat/authored-normal variants and follow geometry color-layout replacement.

Embedded bufferView PNG/JPEG base-color, metallic/roughness, normal, occlusion and
emissive textures are supported
through the explicitly advertised raw-service v3 protocol. Base color uses sRGB,
metallic/roughness, normals and occlusion use linear data. Emissive RGB uses sRGB
with linear alpha. Source factors are applied in native Core, including normal
scale, occlusion strength and emissive factor (default zero, with or without an
image). Normal mapping preserves authored tangent handedness; absent tangents use
the original source cotangent-derivative frame. Geometric/shading normals both
follow double-sided face correction, and mapped normals enable source IBL horizon
occlusion.
Core glTF alpha modes are implemented. Absent `alphaMode` means OPAQUE, which ignores
base-color alpha and writes fully opaque output. MASK defaults to cutoff 0.5, discards
only when image alpha times factor is strictly below cutoff, and writes depth/opaque
alpha for survivors. Factor-only MASK/BLEND uses the source factor through a Float32
uniform, not its generated RGBA8 texture's quantized alpha. Authored image alpha remains
linear even in sRGB textures, and its factor is applied exactly once. This intentionally
corrects the original Lite glTF builder's factor-texture alpha quantization and possible
double factor multiplication, without changing manual ShaderMaterial/StandardMaterial APIs.

BLEND emits straight RGB/alpha and uses source-over color (`srcAlpha`, `1-srcAlpha`)
and alpha (`one`, `1-srcAlpha`) factors. It depth-tests against opaque/MASK surfaces
without writing depth. Existing opaque-before-transparent and stable view-space
translation depth-before-renderOrder ordering remains authoritative; bgfx uses sequential
submission. Transparent intersecting geometry and individual instances are not independently
sorted: these retain the existing per-mesh translation ordering contract.
The alpha-channel association intentionally uses source-over rather than original
Lite PBR's additive destination-alpha factor; emitted RGB remains straight, not
premultiplied. glTF coverage uses base-color alpha only, without Lite's optional
specular-luminance opacity inflation. MASK discard is emitted after all implicit
samples/normal derivatives
so the existing production WGSL validator remains enabled and succeeds.
UV0/UV1 selection, nearest/linear/mipmap filtering, repeat/clamp/mirror addressing and
non-mip LOD0 clamping are retained. Core uploads a complete native-generated mip chain;
the existing pixel-update API refreshes its levels. Exact per-sampler anisotropy remains
deferred (source requests up to 4, effective native support is 1).

Canonical `KHR_materials_pbrSpecularGlossiness` is supported as a distinct
workflow, including factor-only materials and an empty extension object. Its
diffuse RGBA, specular RGB and glossiness factors remain unquantized uniforms;
both maps use sRGB RGB and linear alpha. Packed-map alpha controls glossiness,
not coverage. Colored F0, diffuse energy attenuation and `1 - glossiness`
roughness drive the real production BRDF/IBL path. Normal/occlusion/emissive,
UV0/1, samplers, complete mip updates, vertex colors, flat normals and instances
retain their existing behavior. SpecGloss takes precedence over an authored MR
fallback, without decoding MR-only resources or requiring their UV channels.

This intentionally corrects incomplete original Lite factor/workflow propagation.
Existing MR rendering and getters remain unchanged. For SG the frozen
`bl_getPbrMaterialInfo` returns `BL_UNSUPPORTED` with output unchanged and an
explicit diagnostic because the record cannot represent this workflow. Material
conversion, actual rendering and disposal remain valid. Imported PBR direct-light
terms remain zero; this batch adds no direct-light support. Noncanonical extension
capitalization and other unsupported extensions remain explicit failures; no
optional LOD/layer fallback or filename/hash exceptions are implemented.

`GenerateSpecularGlossinessFixtures.py`, `LiteAssetSpecularGlossinessTests`,
`LiteAssetSpecularGlossinessGpuTests` and `VerifySpecularGlossinessCaptures.py`
provide labeled synthetic raw/schema/ownership/fault, production reflection,
colored BRDF/coverage and physical role/mip/update/sharing tests. They are separate
from unchanged local-model viewer acceptance. New v5 PODs preserve all 151 prior
record layouts, 37 enums and 199 Core signatures; old factories/callbacks and
environment-v2 storage are frozen. Empty/no-BIN/zero-buffer behavior remains.

Authored glTF camera definitions and attachments are ignored for rendering.
Camera-bearing nodes retain their ordinary transforms, meshes, children and scene
membership; no authored camera is instantiated or selected. The sandbox continues
to use its native auto-framed orbit camera. Raw camera counts and attachment indices
are retained, and malformed camera references or unsafe schema values still fail.

This is an expanding geometry/material/skin/TRS subset, **not full glTF support**.
Broader valid-GLB compatibility remains the current priority; unsupported domains
below are pending feature batches, not silently ignored or considered complete.
The completion target is core glTF 2.0 plus Babylon.js 9.30.0 loader extension
coverage, pinned at `b248b370267f13bfaf5b2a0f8fd525bad7c99b52`. Native clips retain
all entries, autoplay the first and allow selection as described above.

Morph targets and weights animation, KHR_animation_pointer, LOD, skinned EXT
instancing, compressed geometry/images, unsupported vertex semantics, point/line
modes, quantization, other unsupported material extensions, and mirrored instancing scales
produce explicit errors rather than dropping parts or substituting models.

The existing runtime shader compiler and PBR/environment path are provisional.
Pixel-perfect scene35 comparison and full camera features remain separate work.
Shader-compiler strategy/performance work remains paused, not a claim made by
this application. RenderDoc is optional test-only tooling, not a runtime dependency.

## Executed model checks

Parent acceptance of the direct-cgltf skin/TRS batch ran every model in the
supplied local acceptance folder through both URI and memory loading. Each route
loads **19 of 35 models**, up from 16; the other 16 fail explicitly. All previous
successes remain successful, and all 41 original model/sidecar files remain
byte-identical. Per-model receipts and assets stay local, not in this repository.
This is progress toward full loading coverage, not a claim that every valid GLB
or Babylon.js extension is supported.

The same acceptance independently exercised 18 actual animated GPU draws with
draw-bound vertex/texture and post-VS readback, all four synthetic clips through
the persistent native controls, and 107 existing static native GPU cases.
Allocator failure/churn, legacy and feature-OFF routing, frozen ABI and mandatory
Core style checks also pass. UI image receipts explicitly identify their own-window
logical-composite capture method; they do not claim physical foreground capture.

The same executable has displayed unchanged BoxTextured and
BoxTexturedNonPowerOfTwo GLBs (embedded PNG, including non-power-of-two dimensions),
the original bowling-ball GLB (embedded JPEG base color and PNG metallic/roughness),
Babylon Burster (JPEG), the original instanced model, and the eight-mesh/seven-material
Cornell model. Disk and byte-source checks include Unicode/spaces filenames, repeated
replacement, malformed JSON and a missing file. The material compatibility batch
adds unchanged Avocado (base/MR/authored tangent normal; its pinned file declares
no occlusion binding) and BoomBox (all five texture roles, shared MR/occlusion and
emissive factor). Failed replacements preserve the previous asset
identity and continue drawing it; exact retained-frame byte equality is not claimed.
Runtime/decoder tracked allocation bytes return to zero at teardown.

Earlier parent acceptance independently repeated disk and byte-source visible smoke
loads: each had seven successes and three expected failures with the previous
model retained. Native contract/legacy CPU/data-only checks, the version3 decoder,
texture upload/update/ownership fault tests and the strict Core style gate pass.
Those earlier results predate the three-role material batch and are preserved.
They do not establish full glTF or source-perfect image qualification.

Parent acceptance of the material batch independently reran the seven focused
native/decoder/data-only suites, numeric asymmetric map/basis/UV/color-space
capture checks, distinct/shared-role GPU resources and update/fault/churn checks.
Fresh visible disk and memory lists each produced seventeen successful loads
and five expected retained-model errors, with zero tracked bytes at teardown.
The public 199-function header is byte-identical to the accepted baseline.
The alpha batch also displays the unchanged `AlphaBlendModeTest` sample
(nine source meshes, six materials; OPAQUE, BLEND and MASK cutoffs .25/.5/.75).
It is CC-BY-4.0 at the same glTF-Sample-Assets revision listed below; preserve
its license, metadata and contributor attribution. Synthetic fixtures are
explicitly separated from these unchanged real-model checks.

`GenerateAlphaFixtures.py` and `LiteAssetAlphaTests` exercise labeled synthetic
MASK/BLEND inputs through the raw decoder and unmodified production WGSL. A real
BGRA8 plus depth framebuffer preserves all four output channels for numeric checks
with `VerifyAlphaCaptures.py`. Cases cover factor precision around/equal to cutoff,
image/factor multiplication, opaque alpha, straight source-over color and alpha,
reversed overlapping submission, depth blocking/no transparent depth writes,
double-sided/mirrored mapped surfaces, mixed phases and instances. Production
shader strategy and the public ABI are unchanged.
Parent acceptance independently repeated all 52 production GPU cases and 138
numeric color/alpha checks, seven focused native/decoder regressions, the style
gate and MASK/BLEND resource fault/update checks. Fresh visible URI and memory
lists each completed 41 loads and nine expected retained-model errors, with
zero tracked allocation bytes at teardown. This completes alpha modes, not the
broader accessor/resource/deformation/extension compatibility goal.

BoxTextured and BoxTexturedNonPowerOfTwo are Cesium's 2017 models, CC-BY-4.0;
their Cesium logos have separate trademark restrictions. Keep their upstream
`LICENSE.md` and `metadata.json` when distributing test assets. These models are
from glTF-Sample-Assets revision `edc7c9e67c639d230715049ee31f9a96a6babbbe`.
Other original playground assets retain their original source/license provenance;
they are not relicensed by this application. Lighting assets must also retain their
own notices. No test models are generated or selected by filename in Core.

Build `LiteAssetDecoder3Tests` for the registered raw-provider CTest. Build
`LiteAssetDecoder4Tests` and `LiteAssetAccessorTests` for nested-prefix/presence/
sparse-padding/bounds/normalization/topology/fault checks. `GenerateGeometryFixtures.py`,
`LiteAssetGeometryTests` and `VerifyGeometryCaptures.py` separate independent CPU
byte oracles, unmodified production RGBA rendering, and explicitly labeled vertex/
normal diagnostic probes. The unchanged licensed `SimpleSparseAccessor.gltf/.bin`
reference has no upstream GLB variant; its test GLB is labeled a container adaptation
with only BIN URI removal, container encoding and padding, not an unchanged real GLB.
`GenerateCameraFixtures.py` covers ignored perspective/orthographic definitions,
camera-bearing transformed mesh hierarchies and camera-free equivalents.
`GenerateEmptyFixtures.py` creates independent labeled empty/no-BIN, camera-only,
selected-empty/unused-mesh, implicit-zero and malformed resource/reference cases.
Run `LiteAssetEmptyTests <fixture-directory>` for raw services 2/3/4, URI/BYTES,
metadata/root/TRS, ownership/stale identity, partial-result and OOM checks without
initializing graphics. Run
`LiteAssetEmptyGpuTests <fixture-directory> <lighting-directory> <fresh-output>`
for actual clear/readback, nonempty recovery, faults and repeated replacement
with real submitted-work retirement. `VerifyEmptyCaptures.py <smoke-output>
<expected-json>` checks every visible drop's selected identity/counts, complete
empty clear, nonempty pixels and byte-exact failure retention.

Build
`LiteAssetTextureTests` and run it with an unchanged embedded JPEG-plus-PNG GLB
and a new result-file path for GPU upload/mip/update, shared-owner, fault,
allocation-failure and retirement checks. Its actual GPU staging checks do not
establish original-source framebuffer or mip-generation equivalence.

`GenerateMaterialFixtures.py` creates labeled synthetic asymmetric material inputs,
not regenerated real models. The separate `LiteAssetMaterialOracleTests` executable
compiles the production WGSL with only its final output encoded for numeric GPU
checks; this hook is excluded from `LiteSandbox`. `VerifyMaterialCaptures.py` checks
normal/UV/scale/basis, occlusion/strength, emissive/defaults/sRGB and linear alpha
against independent arithmetic. Shader compilation strategy remains paused.

## Automated smoke mode

```powershell
.\LiteSandbox.exe --smoke-list 'E:\checks\paths.txt' --output 'E:\checks\fresh-output'
```

The UTF-8 list contains one absolute file path per line. Smoke mode creates real
Win32 `WM_DROPFILES` payloads, uses the same drop handler and native API, captures
actual GPU BGRA frames, and exits after the list. `events.jsonl` records outcomes,
active model identity, source counts, visible-window status and draw counts;
selected-scene/node/primitive counts and the explicit empty-scene camera path
are recorded separately from file-level counts;
`teardown.json` records runtime/decoder allocator cleanup. These checks are not
pixel-perfect source qualification or performance measurements.
