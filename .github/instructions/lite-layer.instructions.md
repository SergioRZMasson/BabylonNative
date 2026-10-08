---
applyTo: "Core/LiteLayer/**"
---

# Mandatory LiteLayer implementation policy

Read and follow `Core/LiteLayer/AGENTS.md` and the canonical
`Core/LiteLayer/.ai/GUIDELINES.md` before any implementation or rewrite.
These scoped engine rules override general BabylonNative object-model guidance.

Use the single reviewed C99 public header and handwritten basic C-style C++
free functions/POD with no STL/engine classes and bgfx/bx dependencies.
The approved optional RmlUI feature may implement required private C++ interfaces
under the narrow exception documented in the canonical policy. Preserve
original TypeScript behavior, ownership, error handling
and API compatibility. Follow local clang-format/semantic style checks.

Keep NAPI, native platform/input, user-language runtime/GC and runtime WGSL compiler
dependencies outside LiteLayer. Implement only measured, parity-tested
performance improvements; do not copy NativeEngine/generated engine bodies or
remove required work to improve a benchmark. Read the canonical policy for the
complete test, lifecycle, shader, performance and handoff requirements.
RmlUI layout and its bgfx rendering backend are inside the isolated UI feature;
bgfx remains the single GPU renderer and presenter.
