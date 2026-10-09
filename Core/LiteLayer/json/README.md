# Vendored nlohmann_json

User-approved nlohmann_json 3.12.0, copied byte-for-byte from the glTF-SDK
reference. The MIT license is retained in `LICENSE`.

- Repository: `SergioRZMasson/glTF-SDK`
- Reference branch: `Release/2.0.0`
- Immutable commit: `5da500e034ebcd12fadcdacb4c09a7ae67c1a5c2`
- Header source: `External/json/nlohmann/json.hpp`
- Header Git blob: `82d69f7c5d044c9887c96b90c97f5639083ecd14`
- Header SHA256: `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`
- License SHA256: `c0d068392ea65358b798b8c165103560f06e9e3b38c4ab4e2d8810a7b931af86`

The requested storage location is `Core/LiteLayer/json/json.hpp`.
Runtime asset parsing remains in the separate native asset-decoder component,
not LiteLayer engine algorithms. `Dependencies/LiteAssetJson.cmake` exposes
a private-use header target and checks the vendored hashes. Do not include
JSON/STL types in the public C99 contract, install this as another LiteLayer
public header, or add the parser as a direct/transitive engine dependency.

This is unmodified third-party source, not owned engine implementation. Preserve
its bytes rather than applying LiteLayer formatting or AST declaration rules.
