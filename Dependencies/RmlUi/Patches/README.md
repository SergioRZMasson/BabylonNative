# Renderer-scoped box-shadow cache

Base: canonical upstream RmlUI
`b7b4a0688262832eacf3b9abb41f8bbe73868af8`.

The global cache originally keyed only `BoxShadowGeometryInfo`, although its
cached geometry and callback texture belong to a particular `RenderManager`.
Two identical shadows in independent contexts therefore reused the first
manager's resources and view submissions.

This MIT-licensed two-file patch partitions the stable geometry-key map by
render-manager identity. Geometry info, hashing, layout, color, blur and quality
are unchanged. Renderables retain the original stable geometry-key reference
and their owning manager. Destruction erases the exact inner entry and removes
the empty outer manager entry before that manager's address can be recycled.

`ApplyBoxShadowPatch.cmake` verifies the source pin/identity files and canonical
LF SHA256 before/after hashes, applies only to the exact pristine pair, accepts
the exact patched pair idempotently and rejects mismatched/partial states.
It also runs for cached FetchContent source overrides. A build-local JSON
receipt records source, pin, patch digest and verified postcondition; no
historical compiler cache, binary, shader or baseline is changed.

The regression uses simultaneous identical shadows, first-owner destruction,
recreation/churn, actual pixels and per-context draw/resource counts. This is
a dependency correctness patch, not copied engine implementation or a change
to the dependency version.
