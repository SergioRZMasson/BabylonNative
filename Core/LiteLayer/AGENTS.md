# LiteLayer agent instructions

Before reading implementation for a change, read `.ai/GUIDELINES.md` in this
directory. It is the mandatory canonical policy for all agents working on
LiteLayer implementation, tests, performance changes or rewrites.

LiteLayer-specific design rules take precedence over general BabylonNative
engine conventions: one C99 `babylon_lite.h`, handwritten C-style C++ free
functions/POD, no STL or engine classes, bgfx/bx plus the explicitly approved
optional RmlUI feature and narrow mandatory-interface adapter exception,
explicit ownership/errors, and the local enforced Allman/braced style.
The user-approved private cgltf importer/Gary glTF-loader reuse exception is
defined in the canonical policy. It allows parsing-to-native-resource loading,
not parser types in the public API or a general Core architecture rewrite.

Trace the pinned Babylon Lite implementation bodies and shaders before designing
changes. Babylon Lite is the authority, including CPU/GPU execution placement;
API similarity or bblitec output is not enough. Follow the canonical policy's
source-fidelity rules while retaining the established C-style C++ restrictions.
Preserve the reviewed API and original TypeScript semantics. Do not copy
NativeEngine or generated bblitec engine implementations. Keep shader-compiler dependencies, native platform/input, NAPI and user-language
runtime services outside Core. RmlUI layout and its bgfx backend are the
user-approved isolated UI feature inside LiteLayer.

Use test-driven changes and the measured performance priorities in the policy.
Before proposing any bgfx change, check how Babylon Native/Babylon.js already
implement the feature and reuse that approach (policy section 2).
Shader compilation strategy is decided (policy section 6.1): runtime WGSL
compilation by default, with Tint translating directly to the platform shader;
offline compilation/compiler-free builds and shader-cache capture are future
work. Current priority (policy section 8): cover the API surface exercised by
the bblitec test scenes with native + JS projection implementations, tests,
and measured performance equal to or better than bblitec.
Do not infer speedup from a shorter Core-only timer, skip required work, weaken
checks or treat stale/empty evidence as passing. Public API/architecture changes
require review. Follow this policy in any same-task handoff to another agent.
