# Mandatory LiteLayer implementation guidelines

These rules apply to every implementation agent and contributor working in
`Core/LiteLayer`. Read them before proposing, rewriting or reviewing code.
The directory's `AGENTS.md` and scoped Copilot instructions point here.
This is the canonical policy; do not maintain competing copies.

## 1. Purpose and source authority

LiteLayer is an owned, reusable native Babylon Lite implementation, not an
application-specialized compiler output or a smaller NativeEngine wrapper.

- Manually implement native algorithms from the reviewed C99 contract and the
  original Babylon Lite TypeScript declarations **and bodies**.
- The only public header is `Include/babylon_lite.h`. Preserve its ABI, documented
  defaults, precision, ordering, identity, ownership and failure behavior.
  Contract changes require explicit review before implementation.
- Current authority is Babylon Lite 1.32.0, source
  `2e064d88ec7422af946f8ec7f089ac6519f99295`. An intentional version upgrade
  must update the contract, provenance and parity tests together.
- Use bblitec declarations/API inventories as comparison evidence, not its
  generated or previously migrated engine bodies. Transpilation is permitted
  for **test/demo user code**, never the LiteLayer engine implementation.
- Do not model the implementation after BabylonNative NativeEngine/Graphics
  classes, the rejected first attempt, or a teammate's prototype internals.
  Project organization and formatting follow BabylonNative; engine design
  follows Babylon Lite's functional/data-oriented principles.
- Preserve upstream license/provenance. Distinguish source-equivalent behavior
  from a necessary native embedding adaptation and an unsupported capability.

Read `Documentation/BabylonLite.md` and
`Documentation/LitePerformanceInvestigation.md` for current evidence.
API declarations or imported function names are not proof of implementation
or semantic coverage. The Minecraft slice is not the entire Babylon Lite API.

## 2. Architecture and dependency boundaries

**Basic C-style C++, free functions, plain data, minimal coupling.**

- Use POD/standard-layout records, enums, explicit pointer/count spans, opaque
  typed identities and small free-function helpers.
- No STL in engine algorithms: no `std::` containers, strings, smart pointers, function wrappers,
  streams, algorithms, threading, exceptions or other STL machinery in Core.
  The narrow RmlUI interface exception below is the only UI-related exception.
- No application-defined engine classes, constructors/destructors, member methods,
  inheritance, virtual dispatch, Pimpl or RAII engine/object hierarchies.
  Private use of bgfx/bx's own API types and necessary stack temporaries is
  allowed; do not memset an unconstructed nontrivial C++ object.
- **bgfx and bx**, plus optional **RmlUI for UI support**, are the approved
  direct third-party dependencies. Standard C headers,
  libc and basic C++ language facilities are allowed. The pinned bx headers
  require C++20; that does not justify a modern C++ framework.
- No NAPI/JS VM, SDL, WebGPU/Dawn renderer, Windows platform calls, image/audio
  codec library, cgltf, Tint, glslang or SPIRV-Cross inside LiteLayer.
  bgfx's own transitive dependencies do not permit direct Core use of them.
- User-approved JSON storage (2026-10-09): unmodified nlohmann_json 3.12.0 and
  its MIT license are vendored at `json/json.hpp` and `json/LICENSE`, using the
  pinned glTF-SDK reference recorded in `json/README.md`. This is a storage
  location, not permission to use JSON/STL in engine algorithms. The separate
  native asset decoder consumes `Babylon::LiteAssetJson`; LiteLayer's direct
  dependency guard and sole public C99 header remain unchanged. Preserve
  third-party bytes rather than formatting or rewriting the vendor header.
- Runtime shader compilation, native windows/input/files/image decoding,
  audio output and user-language containers/GC stay in **separate** components.
  Host services cross a narrow C-compatible callback/data boundary.
- User-approved change (2026-10-07): RmlUI and its bgfx backend live **inside
  LiteLayer**, rather than a separate UI target. Keep UI as an explicit,
  optional feature with isolated implementation files. A data-only consumer
  must not initialize UI/graphics or pull UI objects into its link graph.
- RmlUI's mandatory C++ interface implementations may use the inheritance,
  overrides, methods and third-party STL-shaped parameters required by those
  interfaces. This exception is limited to private RmlUI adapter files, not
  engine data/algorithms. Keep native UI state and helper algorithms C-style
  where practical; do not use this exception to introduce an engine hierarchy.
- Private RmlUI adapter boundaries must contain `std::bad_alloc` from the
  third-party API and translate it to the C99 failure contract. This narrow
  exception-handling allowance is not an engine exception architecture.
  Non-transactional third-party ownership/global-initialization failures need
  explicit invalidation/latched failure, never unsafe retry or silent success.
- RmlUI is the layout/style/text system. Its backend records/uploads UI through
  **bgfx**, with bgfx as the single GPU renderer/presenter. No SDL_Renderer,
  GDI backing overlay or WebGPU renderer inside Core. SDL/window input remains
  a host concern.
- The public UI boundary stays in the single `babylon_lite.h`: C99 records,
  handles, statuses and callbacks only. No RmlUI/STL/virtual types leak into
  public declarations. Additive UI declarations require contract review.
- Declare/pin/configure RmlUI using BabylonNative's existing FetchContent and
  Dependencies conventions. Do not vendor its implementation or reuse private
  dependency binaries as a substitute for a reproducible source dependency.
- LiteLayer owns transforms, scene membership, draw ordering, material values,
  shader-interface construction, GPU resources/retirement and engine audio
  routing. Do not move those algorithms into JS, NAPI or host callbacks.
- Keep feature modules independently usable/dead-strippable where practical.
  Data-only factories must not initialize graphics or require a compiler/VM.
  Do not create a central cleanup/dispatch dependency that pulls every feature
  into a data-only consumer.
- Prefer the simplest correct helpers and explicit storage. No generic
  extensibility framework, unnecessary templates, hidden global service locator
  or speculative configuration surface.

The CMake target's direct dependency guard accepts bgfx/bx and only the explicit
RmlUI feature targets when UI is enabled. The separate shader compiler must not
become a transitive LiteLayer link requirement. Native UI rendering may invoke
the existing injected runtime shader service without linking that compiler.

## 3. Code style and readability

Use the directory's `.clang-format` and `.clang-tidy`, with LLVM 22.

- Allman braces, four spaces, no tabs, 100-column wrapping.
- Every `if`, `else` body, loop, `do` and `switch` uses explicit braces.
  `else if` chains remain ordinary braced branches.
- No single-line functions, blocks or control-flow bodies. Do not pack several
  statements onto one line.
- One variable declaration per statement and one struct field per declaration:
  no `int first, second;`, including globals and loop initialization.
- Avoid macros that conceal important control flow or ownership. Necessary
  macros must also use explicit, multiline braced control flow.
- Follow established naming and small feature boundaries. Do not rename public
  APIs or unrelated internals solely for stylistic preference.
- Comments explain non-obvious semantics/invariants, not obvious operations.
- Do not use pointer-to-pointer casts to write through incompatible types.
  Look up a base record into a correctly typed temporary, then convert it
  using the checked first-member/standard-layout relationship.

Enable `BABYLON_LITE_ENABLE_STYLE_CHECKS=ON`. `LiteLayerStyleCheck` checks
formatter output and compiler-AST declaration/control-flow rules, including
macros and fields. clang-format alone cannot enforce all these rules.
Do not disable the checks or bypass them with formatting-off markers.

## 4. Identity, memory and resource lifetime

- Use the runtime allocator with explicit, correct alignment and checked
  arithmetic. Validate spans, counts, ranges, UTF-8 and overflow before use.
  Validate typed-array alignment where the contract requires it.
- Public numbers/transforms are double where the contract specifies; geometry,
  uniform backing and GPU uploads preserve the original Float32/Uint8/Uint32
  representations. Do not silently trade precision for speed.
- Runtime/type/engine/thread-affinity validation and generation-protected
  identities are required. Slot reuse must never make a stale handle resolve
  to a new object. Disposed identities retain their documented error semantics.
- Copy input data and preserve borrowed-view lifetimes exactly as documented.
  Do not invalidate live uniform backing or client identities during growth.
- Scene membership is not automatically a set: preserve duplicate additions,
  shared-scene ownership, recursive add/remove and separate parent/children
  mutation semantics.
- Last-owner mesh removal retires the identity immediately; submitted GPU
  work may keep its storage/resources alive until safe destruction.
  Materials/textures shared by live owners must survive another owner's removal.
- Free/reuse retired record storage safely. Long-running create/remove loops
  must plateau after warmup, not accumulate unbounded tombstones.
- GPU destruction waits for actual submitted work, not an assumed frame count.
  Preserve OWNED versus BORROWED bgfx lifecycle and framebuffer ownership.
- `isExternalBgfxInitialized` reports **host** initialization; Lite tracks its
  own init/shutdown. Do not probe `getStats` before initialization or infer
  initialization from stale `getCaps` after shutdown.
- No exceptions cross C/service callbacks. Callbacks/userdata must remain valid
  through dispatch; same-frame allowed mutations affect the current draw.
  Reject forbidden re-entry, nested frames and unsafe teardown explicitly.
- Core has no GC. User-language collection outside Core must retain its own
  required semantics; do not replace reference-based lifetime with destruction
  when a temporary C++ wrapper leaves scope.

## 5. Correctness, errors and rendering semantics

- Preserve all documented return statuses and output-parameter behavior.
  Report errors through the existing status/diagnostic contract when safe;
  do not mutate thread-affine error state from the wrong thread.
- No broad catch-and-ignore, fake success, silent invalid-input return,
  substituted shader/texture, fake GPU time or pretend audible audio.
  Unsupported features fail explicitly and remain listed as unsupported.
- Keep material/data creation independent of a renderer. Prepare real pipelines
  only at the appropriate lifecycle boundary, not in a JS binding.
- Preserve left-handed/reversed-Z math, Float32 versus high-precision matrices,
  dirty parent versions, cached Euler behavior and zero scaling.
- Preserve callback order, first-frame delta/completion, fixed scene delta,
  scene registration order, overlay clear behavior and stopped-engine behavior.
- Preserve opaque-before-transparent phases, minimum effective opaque-group
  order, stable sequence, and transparent depth-before-order semantics.
  Backend sorting must not override required submission order.
- Geometry factories match original counts, winding, normals/UVs and independent
  allocations. Preserve geometry/capacity/range updates, CPU-versus-GPU-only
  updates, buffer identity and deferred buffer retirement.
- Texture color space, filters, addressing, alpha and bounds are real semantics,
  not tuning knobs to change silently for a better benchmark.
- Audio Core manages original routing/volume/ramp/retry semantics over actual
  host primitives. No host means explicit unavailability; offline output is
  not a claim of audibility.

## 6. Runtime WGSL compiler boundary

- Keep arbitrary runtime WGSL support through the injected C99 shader service.
  Offline baking is future/optional work, not a replacement for this requirement.
- Core manually constructs the original material prelude, declaration order,
  alignment, sorted defines, system/custom uniforms and texture/sampler bindings.
- The compiler returns real backend/version-compatible bgfx containers,
  stage-link information, active reflection and diagnostics.
  Do not replace the compiler with regex/string guesses, demo-specific canned
  shaders or copied renderer-plan implementations.
- Preserve original member layout and integer representation. Pack members
  sharing a native uniform into the correctly sized, zero-padded buffer and
  submit the native uniform once; do not overwrite a block one member at a time.
- Validate reflection/type/count/offset/binding consistency before creating GPU
  resources. Release compiler results exactly once after every invocation,
  including failures and partial/empty results.
- Byte containers, service tables and userdata have explicit ownership.
  No compiler dependency or native shader format leaks into the public contract.
- A direct Tint HLSL path is a separate compiler experiment needing equivalent
  reflection/packing/link/error tests, not permission to copy a fragile text
  bridge or change dependency pins during an engine rewrite.

## 7. Performance rules based on measurements

The investigation identified full-frame GDI HUD rebuilding in the host as the
largest complete-host bottleneck. The approved RmlUI feature now replaces that
path with retained UI and a bgfx backend inside LiteLayer. Do not recreate the
GDI full-surface rebuild there or remove required UI to make results look faster.

For Core implementation:

- Aggregate repeated material-group order/membership information once per
  **current frame**, not a complete member scan for every opaque draw.
  The measured experiment reduced that phase by about 91%, from about
  0.238 to 0.020 ms. Include invisible/non-drawn relevant members, explicit
  and default orders, earliest group sequence and shared-scene semantics.
  Across-frame caching needs a separate complete invalidation proof.
- Keep existing lazy dirty/parent-version matrix evaluation; several component
  setters should not force several eager matrix recomputations.
- Reuse scene scratch/storage capacities and validated layouts/handles.
  Do not allocate, compile shaders, rebuild unchanged resources or rediscover
  stable interfaces in a hot frame without a measured/semantic reason.
- Reuse immutable geometry when useful; mutable geometry needs independent
  updates or safe detach/copy-on-write. Distinct voxel chunks cannot simply
  share one buffer.
- Profile lookups, copying, allocation and upload categories before optimizing
  them. Preserve guards, generation checks and reflection validation.
  Avoid trading correctness for a tiny unmeasured lookup win.
- C-style/no-STL is an architectural constraint, **not proof of performance**.
  Prioritize measured CPU/memory cost and simple data access over imitation of
  another engine or speculative algorithm changes.

For measurements and interaction with host agents:

- Record complete host, user callbacks, Core, render-thread/process execution,
  GPU and queue/wait spans separately. Do not compare Core-only enqueue time
  with another implementation's complete loop or sum overlapping spans.
- Keep SEED 1337/radius 6/all 169 Minecraft chunks and original user logic,
  shader/texture quality, resolution, MSAA and fixed trace when comparing.
  No physical input, capture/readback, file output, pacing or concurrent builds
  contaminate measured frames.
- Warm up consistently; use repeated balanced run pairs and confidence
  intervals from runs, not individual frames. Record source/binary/config hashes.
  Measure profiling overhead against the short Core bracket as well as totals.
- Deduplicate real GPU query IDs; unavailable queries are not measured zeros.
  `init.profile=false` does not disable frame queries on the current D3D11 pin.
- Require both computation and complete-host/P95/latency acceptance criteria.
  Less CPU work can become more GPU handoff wait rather than higher throughput.
  Deeper queues can trade latency/capture alignment for submission throughput.
- Do not remove user GC: its measured idle cost is tiny, and idle traces do
  not establish arbitrary reference-cycle safety.
- Retained UI/layout/backend work belongs to LiteLayer's isolated RmlUI feature;
  host input, worker decoding, shader-cache/backend stripping and native
  presentation changes belong to their respective outside-Core owners.
  Hidden/fixed-size parity is not general interactive invalidation evidence.
- Retain compiled UI geometry/textures and backing resources. Update only
  correctly invalidated content, preserving viewport/DPI/fonts/icons/style/
  text/visibility changes, clipping, transforms, alpha and composition order.
  Test F3/selection/toasts/save/load, resize/DPI and texture lifetime. Rendering
  UI every frame must not entail creating/rebuilding all CPU/GPU resources.

## 8. Test-driven implementation and rewrite workflow

1. Read this policy, the entire relevant public contract, original TS bodies,
   existing native tests and the performance report before designing changes.
   Keep implementation exploration isolated from NativeEngine/generated engine
   implementation bodies.
2. Identify the source semantics and every non-mechanical decision: lifetime,
   async/threading, union adaptation, native resource ownership and lazy/eager
   behavior require an explicit invariant and test.
3. Write/extend focused tests before the change. Implement coherent feature
   batches while preserving the public contract and independently usable modules.
   A rewrite is not permission to add unrelated APIs or erase behavior.
4. Check strict C99 public-header consumption, C++ consumption, warnings-as-errors,
   style/AST rules and actual dependency/forbidden-include audits.
5. Run original-source geometry/audio goldens, allocator-failure/churn and
   stale-handle tests, node/scene ordering, sharing/disposal, material/uniform/
   texture state and shader-service success/failure/release tests.
6. Validate real runtime shader compilation and GPU readback, JS forwarding,
   full native C++ Minecraft, replay/audio and save/load. Compare actual ordered
   draw state and pixels, not only nonempty images or successful process exit.
7. Add negative invalidation and small randomized order/lifetime cases for
   caches/aggregation. Preserve memory-safe teardown and submitted-work retirement.
8. Keep prior executables/receipts immutable. Build rewritten/instrumented code
   in a new ignored directory. Do not edit the original demo to pass a comparison.
9. Update directly dependent tests/profiling adapters when implementation layout
   changes. Reject stale source anchors explicitly; never treat empty hashes,
   stale captures or unexecuted branches as validation.
10. Report implemented scope, exact commands/results, errors/unsupported cases,
    measured performance and remaining gaps. No full-package coverage claim
    from 98 C99 helpers or a single passing workload.

Preserve unrelated dirty work and shared caches. Do not commit/push, post
findings or change these guidelines unless the coordinating user/agent has
authorized that action. Ask before changing the reviewed ABI or architecture.
