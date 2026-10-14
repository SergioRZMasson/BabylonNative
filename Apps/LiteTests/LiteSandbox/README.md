# Native GLB sandbox

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

The executable loads raw self-contained GLB2 files through `bl_loadGltf`, with
arbitrary file paths and names. `--memory` exercises the same loader's raw byte-source
route. There is no asset-name/hash/count dispatch or regeneration step.

Implemented static geometry includes multiple nodes, meshes, primitives and opaque
PBR materials; ordinary TRS and locked affine matrices; mirrored-node winding;
double-sided surfaces; tightly packed/interleaved FLOAT attributes; normalized
U8/U16 UVs; U8/U16/U32 indices and nonindexed triangles. Missing normals are generated
in native Core. Source POSITION, NORMAL, TEXCOORD_0/1 and TANGENT streams are retained.

Embedded bufferView PNG/JPEG base-color and metallic/roughness textures are supported
through the explicitly advertised raw-service v3 protocol. Base color uses sRGB,
metallic/roughness uses linear data, and source factors are applied in native Core.
UV0/UV1 selection, nearest/linear/mipmap filtering, repeat/clamp/mirror addressing and
non-mip LOD0 clamping are retained. Core uploads a complete native-generated mip chain;
the existing pixel-update API refreshes its levels. Exact per-sampler anisotropy remains
deferred (source requests up to 4, effective native support is 1).

This is a common-static-model subset, **not full glTF support**. Normal, occlusion and
emissive texture bindings are preserved by the raw parser but explicitly rejected by
the initial native material slice; they are not silently ignored.

Skins, animation playback, morph targets, compressed geometry/images, external/data
buffer or image URIs, unsupported vertex semantics, imported cameras, non-triangle
modes, unsupported material extensions/alpha modes, and mirrored instancing scales
produce explicit errors rather than dropping parts or substituting models.

The existing runtime shader compiler and PBR/environment path are provisional.
Pixel-perfect scene35 comparison, full camera features, and parser/compiler strategy
selection are separate work, not claims made by this application.

## Executed model checks

The same executable has displayed unchanged BoxTextured and
BoxTexturedNonPowerOfTwo GLBs (embedded PNG, including non-power-of-two dimensions),
the original bowling-ball GLB (embedded JPEG base color and PNG metallic/roughness),
Babylon Burster (JPEG), the original instanced model, and the eight-mesh/seven-material
Cornell model. Disk and byte-source checks include Unicode/spaces filenames, repeated
replacement, malformed JSON, a missing file, and Avocado's explicitly unsupported
normal/occlusion texture inputs. Failed replacements preserve the previous asset
identity and continue drawing it; exact retained-frame byte equality is not claimed.
Runtime/decoder tracked allocation bytes return to zero at teardown.

Parent acceptance independently repeated disk and byte-source visible smoke
loads: each had seven successes and three expected failures with the previous
model retained. Native contract/legacy CPU/data-only checks, the version3 decoder,
texture upload/update/ownership fault tests and the strict Core style gate pass.
This does not establish full glTF or source-perfect image qualification.

BoxTextured and BoxTexturedNonPowerOfTwo are Cesium's 2017 models, CC-BY-4.0;
their Cesium logos have separate trademark restrictions. Keep their upstream
`LICENSE.md` and `metadata.json` when distributing test assets. These models are
from glTF-Sample-Assets revision `edc7c9e67c639d230715049ee31f9a96a6babbbe`.
Other original playground assets retain their original source/license provenance;
they are not relicensed by this application. Lighting assets must also retain their
own notices. No test models are generated or selected by filename in Core.

Build `LiteAssetDecoder3Tests` for the registered raw-provider CTest. Build
`LiteAssetTextureTests` and run it with an unchanged embedded JPEG-plus-PNG GLB
and a new result-file path for GPU upload/mip/update, shared-owner, fault,
allocation-failure and retirement checks. Its actual GPU staging checks do not
establish original-source framebuffer or mip-generation equivalence.

## Automated smoke mode

```powershell
.\LiteSandbox.exe --smoke-list 'E:\checks\paths.txt' --output 'E:\checks\fresh-output'
```

The UTF-8 list contains one absolute file path per line. Smoke mode creates real
Win32 `WM_DROPFILES` payloads, uses the same drop handler and native API, captures
actual GPU BGRA frames, and exits after the list. `events.jsonl` records outcomes,
active model identity, source counts, visible-window status and draw counts;
`teardown.json` records runtime/decoder allocator cleanup. These checks are not
pixel-perfect source qualification or performance measurements.
