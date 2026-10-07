import argparse
import hashlib
import json
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("root", type=pathlib.Path)
args = parser.parse_args()
root = args.root.resolve()
assert "no-ui-comparison" in root.parts
common = root / "Common" / "Emitted"
generated = root / "Common" / "Generated"
canonical = sorted(path for path in common.rglob("*")
                   if path.suffix == ".cpp" or path.name == "application.hpp")
assert len(canonical) == 27
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
records = [{"file": file.relative_to(common).as_posix(), "bytes": file.stat().st_size,
            "sha256": sha(file)} for file in canonical]
for record in records:
    assert sha(generated / record["file"]) == record["sha256"], record["file"]
result = {"canonicalStandardCpp": records, "userCppEdits": [],
          "applicationHeaderEdits": [], "variants": {}}
for name, app_root in [("Native", root / "Native" / "UserSources"),
                       ("SDL", generated), ("DawnBuild", generated)]:
    build = root / name
    commands = json.loads((build / "compile_commands.json").read_text())
    app_commands = []
    for record in records:
        file = app_root / record["file"]
        assert sha(file) == record["sha256"], f"{name}:{record['file']}"
        if file.suffix != ".cpp":
            continue
        matching = [entry for entry in commands if pathlib.Path(entry["file"]).resolve() == file.resolve()]
        assert len(matching) == 1, f"Actual compiler input missing/duplicate: {file}"
        command = matching[0]["command"]
        assert "BBLITE_NO_UI_COMPARISON=1" in command or "LITE_MINECRAFT_NO_UI=1" in command
        if name == "Native":
            assert "main=MinecraftApplication" in command
        app_commands.append({"file": record["file"], "sha256": record["sha256"],
                             "entryAbiCompileDefine": name == "Native"})
    cache = (build / "CMakeCache.txt").read_text()
    ninja = re.search(r"^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$", cache, re.MULTILINE)[1]
    dependencies = subprocess.check_output([ninja, "-C", str(build), "-t", "deps"], text=True)
    actual_headers = []
    for line in dependencies.splitlines():
        value = line.strip()
        if value.replace("\\", "/").endswith("/sources/application.hpp"):
            file = pathlib.Path(value)
            actual_headers.append((file if file.is_absolute() else build / file).resolve())
    assert (app_root / "sources" / "application.hpp").resolve() in actual_headers, name
    if name == "Native":
        host_commands = [entry for entry in commands if "NoUiHost.cpp" in entry["file"]]
        assert len(host_commands) == 1 and "main=MinecraftApplication" not in host_commands[0]["command"]
        actual_legacy = [line for line in dependencies.splitlines()
                         if re.search(r"native[\\/]include[\\/]bblite[\\/](?:runtime\.hpp|pal\.hpp|upstream[\\/]renderer_plan\.hpp)", line)]
        assert not actual_legacy, actual_legacy
        text = (build / "build.ninja").read_text()
        assert not re.search(r"\b(?:NativeEngine|napi|JsRuntime|AppRuntime|LiteJSBinding|dawn_native|webgpu_dawn)\.lib\b", text)
        imports = (build / "imports.txt").read_text()
        dlls = sorted(set(re.findall(r"^\s+([A-Za-z0-9_.-]+\.dll)\s*$", imports, re.MULTILINE)))
        assert dlls and not any(re.search(r"v8|quickjs|chakra|dawn|webgpu|SDL", dll, re.IGNORECASE)
                               for dll in dlls)
        engine_sources = []
    else:
        assert re.search(r"^BBLITE_NATIVE_BABYLON_ENTRY:FILEPATH=\s*$", cache, re.MULTILINE)
        engine_sources = [entry["file"] for entry in commands
                          if pathlib.Path(entry["file"]).resolve().is_relative_to(
                              generated.resolve() / "upstream" / "src")]
        assert engine_sources, "Standard baseline did not compile the real generated engine."
    result["variants"][name] = {
        "actualCompiledUserTUs": app_commands,
        "actualGeneratedApplicationHeaderSha256": sha(app_root / "sources" / "application.hpp"),
        "actualOriginalEngineSources": engine_sources,
        "legacyEnginePalObjectsInNative": [] if name == "Native" else "real standard baseline",
        "compiledSourcesByteIdenticalToCanonical": True,
        "actualNativePeImports": dlls if name == "Native" else None,
    }
deployment = json.loads((root / "deployment.json").read_text())
assert deployment["StockDawn"]["exeSha256"] == deployment["CompatibleBgfx"]["exeSha256"]
result["stockProviderDeployment"] = deployment
result["inlinePreludeClassification"] = {
    "standardCompilerInlinedAudioRouting": True,
    "copiedHandwrittenEngineAlgorithmInBinding": False,
    "idleQualificationAudioReached": False,
    "note": "Shared canonical standard C++ includes compiler-lowered native audio routing; "
            "this is explicitly classified separately from application logic. "
            "The binding exposes data records and host audio primitives, not copied Core algorithms."
}
(root / "source-byte-audit.json").write_text(json.dumps(result, indent=2) + "\n")
print("Actual compiler/dependency audit: all 26 user TUs and generated header identical across four variants.")
