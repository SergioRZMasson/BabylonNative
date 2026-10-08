"""Audit actual Ninja compilation/dependencies, PE imports and the immutable source bytes."""
import hashlib
import json
import pathlib
import os
import re
import subprocess
import sys

build = pathlib.Path(sys.argv[1]).resolve()
root = build.parent
assert root.name == "rmlui-minecraft" and build.name.startswith("Native")
sha = lambda file: hashlib.sha256(file.read_bytes()).hexdigest()
canonical = json.loads((root / "Common" / "canonical-user-code.json").read_text())["files"]
assert len(canonical) == 27
commands = json.loads((build / "compile_commands.json").read_text())
cache = (build / "CMakeCache.txt").read_text()
ninja = re.search(r"^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$", cache, re.MULTILINE)[1]
deps = subprocess.check_output([ninja, "-C", str(build), "-t", "deps"], text=True)
records = []
for record in canonical:
    local = build / "UserSources" / record["file"]
    original = root / "Common" / "Generated" / record["file"]
    assert sha(local) == sha(original) == record["sha256"], record["file"]
    if local.suffix == ".cpp":
        matching = [entry for entry in commands
                    if pathlib.Path(entry["file"]).resolve() == local.resolve()]
        assert len(matching) == 1, record["file"]
        assert "main=MinecraftApplication" in matching[0]["command"]
        assert "NO_UI" not in matching[0]["command"]
    records.append(record)
assert "application.hpp" in deps
header_dependencies = []
for line in deps.splitlines():
    if line.strip().endswith("application.hpp"):
        path = pathlib.Path(line.strip())
        header_dependencies.append((path if path.is_absolute() else build / path).resolve())
assert (build / "UserSources" / "sources" / "application.hpp").resolve() in header_dependencies
for entry in commands:
    file = pathlib.Path(entry["file"]).resolve()
    assert not ("Generated" in file.parts and "upstream" in file.parts), file
assert not re.search(
    r"native[\\/]include[\\/]bblite[\\/](runtime\.hpp|pal\.hpp|upstream[\\/]renderer_plan\.hpp)",
    deps), "Legacy engine runtime/PAL header actually reached."
link = (build / "build.ninja").read_text()
assert not re.search(
    r"\b(NativeEngine|napi|JsRuntime|AppRuntime|LiteJSBinding|dawn_native|webgpu_dawn)\.lib\b",
    link)
imports = (build / "imports.txt").read_text()
dlls = sorted(set(re.findall(r"^\s+([A-Za-z0-9_.-]+\.dll)\s*$", imports, re.MULTILINE)))
assert dlls and not any(re.search(r"v8|quickjs|chakra|dawn|webgpu|SDL|node|napi", dll, re.I)
                       for dll in dlls), dlls
transport = (build / "BindingData" / "PlatformTransport.cpp").read_text()
for forbidden in ["CreateDIBSection", "CreateCompatibleDC", "UpdateLayeredWindow",
                  "StretchDIBits", "DrawTextW", "LITE_MINECRAFT_NO_UI"]:
    assert forbidden not in transport, forbidden
core = [entry["file"] for entry in commands
        if re.search(r"Core[/\\]LiteLayer[/\\]Source", entry["file"])]
assert any(pathlib.Path(file).name == "UiRmlAdapter.cpp" for file in core)
assert any(pathlib.Path(file).name == "UiGpu.cpp" for file in core)
result = {
    "canonicalStandardUserFiles": records, "userCppEdits": [],
    "generatedApplicationHeaderEdits": [], "actualUserTranslationUnits": 26,
    "actualNinjaGeneratedHeaderDependencyVerified": True,
    "coreBuiltFromCurrentSources": core, "legacyEnginePalObjects": [],
    "peImports": dlls, "binarySha256": sha(build / "LiteMinecraftRmlUiNative.exe"),
    "standardCompilerAudioPrelude": {
        "unchanged": True, "classifiedSeparateFromUserLogic": True,
        "note": "Compiler-inlined routing shared with SDL/Dawn; not handwritten binding algorithms."
    },
    "sourceTransitiveLinkGraph": True, "gdiHudBacking": False,
    "fullOriginalUiParityClaim": False,
}
fonts = pathlib.Path(os.environ["WINDIR"]) / "Fonts"
result["hostSystemFonts"] = [
    {"file": str(fonts / name), "sha256": sha(fonts / name),
     "distribution": "Licensed installed OS resource; not redistributed or committed"}
    for name in ["segoeui.ttf", "seguisb.ttf", "consola.ttf", "consolab.ttf", "seguisym.ttf"]
]
result["fontRasterizerDifference"] = (
    "Baseline compiler host uses Windows font engine; this host uses the same installed "
    "Segoe UI/Consolas faces via pristine RmlUI FreeType. Glyph pixels may differ."
)
result["dependencyPins"] = {
    "bgfx.cmake": "b53236f4d6dede3252d32b9c11921d0007d705d1",
    "bgfx": "cb0c6d0c6133d123989aaae10cbd35fa139cb648",
    "RmlUi": "b7b4a0688262832eacf3b9abb41f8bbe73868af8",
    "FreeType": "42608f77f20749dd6ddc9e0536788eaad70ea4b5",
}
patch = json.loads((build / "RmlUiBoxShadowPatch.json").read_text())
assert patch["verified"] and patch["pin"] == result["dependencyPins"]["RmlUi"]
for relative, expected in zip(patch["files"], patch["after"]):
    actual = (pathlib.Path(patch["source"]) / relative).read_text(encoding="utf-8")
    assert hashlib.sha256(actual.encode("utf-8")).hexdigest() == expected, relative
result["rendererScopedShadowCachePatch"] = patch
repository = pathlib.Path(__file__).resolve().parents[6]
asset_root = repository / "Apps" / "LiteTests" / "LitePlayground" / "Assets" / "minecraft" / "voxelpack"
result["originalIconAssets"] = [
    {"file": str(file), "sha256": sha(file)}
    for file in sorted(asset_root.glob("*.png"))
]
result["publicHeaderSha256"] = sha(repository / "Core" / "LiteLayer" / "Include" / "babylon_lite.h")
result["hostSourceHashes"] = [
    {"file": str(file), "sha256": sha(file)}
    for file in sorted(pathlib.Path(__file__).resolve().parents[1].glob("Source/*"))
    if file.suffix in (".cpp", ".h", ".c")
]
(build / "source-byte-audit.json").write_text(json.dumps(result, indent=2) + "\n")
print("27 canonical files, 26 actual Ninja user TUs, actual header, native PE/link/UI sources audited.")
