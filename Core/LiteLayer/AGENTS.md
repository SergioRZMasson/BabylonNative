# LiteLayer agent instructions

Before reading implementation for a change, read `.ai/GUIDELINES.md` in this
directory. It is the mandatory canonical policy for all agents working on
LiteLayer implementation, tests, performance changes or rewrites.

LiteLayer-specific design rules take precedence over general BabylonNative
engine conventions: one C99 `babylon_lite.h`, handwritten C-style C++ free
functions/POD, no STL or application-defined classes, only bgfx/bx dependencies,
explicit ownership/errors, and the local enforced Allman/braced style.

Preserve the reviewed API and original TypeScript semantics. Do not copy
NativeEngine or generated bblitec engine implementations. Keep shaders,
platform/HUD, NAPI and user-language runtime services outside Core.

Use test-driven changes and the measured performance priorities in the policy.
Do not infer speedup from a shorter Core-only timer, skip required work, weaken
checks or treat stale/empty evidence as passing. Public API/architecture changes
require review. Follow this policy in any same-task handoff to another agent.
