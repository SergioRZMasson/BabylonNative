"""Isolate bgfx's existing D3D11 staging option without changing vendor source or default targets."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess

from prepare_baseline_probe import argv
from harness import digest


def recipes(build):
    return json.loads(subprocess.run(
        ["ninja", "-C", str(build), "-t", "compdb", "-x"],
        capture_output=True, text=True, check=True).stdout)


def link_args(entries, name):
    command = next(x["command"] for x in entries if x.get("output", "").endswith(name)
                   and "link.exe" in x.get("command", ""))
    command = command.split(" -- ", 1)[1].split(" && ", 1)[0].rstrip('"')
    return argv(command)


def relink(args, build, output, replacements):
    result = []
    for arg in args:
        normalized = arg.replace("/", "\\").lower()
        if normalized in replacements:
            result.append(str(replacements[normalized]))
        elif arg.lower().startswith("/out:"):
            result.append("/out:" + str(output))
        elif arg.lower().startswith("/implib:"):
            result.append("/implib:" + str(output.with_suffix(".lib")))
        elif arg.lower().startswith("/pdb:"):
            result.append("/pdb:" + str(output.with_suffix(".pdb")))
        else:
            result.append(arg)
    if not any(x.lower().startswith("/manifest:") for x in result):
        result.append("/MANIFEST:EMBED")
    subprocess.run(result, cwd=build, check=True)
    return result


def prepare(build, output, old_core):
    output.mkdir(parents=True, exist_ok=True)
    entries = recipes(build)
    graphics = link_args(entries, "LiteNativeGraphicsTests.exe")
    old_core_replacements = {
        "core\\litelayer\\litelayer.lib": old_core / "Core" / "LiteLayer" / "LiteLayer.lib"}
    red = output / "LiteNativeGraphicsTests-old-core.exe"
    red_command = relink(graphics, build, red, old_core_replacements)
    renderer = next(x for x in entries if x.get("file", "").endswith("renderer_d3d11.cpp")
                    and x.get("command"))
    compile_args = argv(renderer["command"])
    obj = output / "renderer_d3d11.cpp.obj"
    compile_args = ["/Fo" + str(obj) if a.startswith("/Fo") else
                    "/Fd" + str(output) + "\\" if a.startswith("/Fd") else a
                    for a in compile_args]
    compile_args.append("/DBGFX_CONFIG_RENDERER_DIRECT3D11_USE_STAGING_BUFFER=1")
    subprocess.run(compile_args, cwd=build, check=True)
    native = link_args(entries, "LiteScenesNative.exe")
    original = next(a for a in native if a.lower().endswith("bgfx.lib"))
    original_path = build / original
    members = subprocess.run(["lib", "/nologo", "/list", str(original_path)],
                             check=True, capture_output=True, text=True).stdout.splitlines()
    member = next(x.strip() for x in members if x.strip().endswith("renderer_d3d11.cpp.obj"))
    stripped = output / "bgfx-without-d3d11.lib"
    library = output / "bgfx-staging.lib"
    subprocess.run(["lib", "/nologo", "/out:" + str(stripped),
                    "/remove:" + member, str(original_path)], check=True)
    subprocess.run(["lib", "/nologo", "/out:" + str(library), str(stripped), str(obj)], check=True)
    commands = []
    for name in ("LiteScenesNative.exe", "LiteScenesJs.exe", "LiteNativeGraphicsTests.exe"):
        command = relink(link_args(entries, name), build, output / name,
                         {original.replace("/", "\\").lower(): library})
        commands.append({"target": name, "command": command, "sha256": digest(output / name)})
    hosts = build / "Apps" / "LiteTests" / "LiteScenes"
    for dll in hosts.glob("*.dll"):
        shutil.copy2(dll, output / dll.name)
    (output / "manifest.json").write_text(json.dumps({
        "scope": "isolated-supported-bgfx-option-experiment-not-default",
        "baseBgfxSha256": digest(original_path), "experimentalBgfxSha256": digest(library),
        "rendererCompileCommand": compile_args, "targets": commands,
        "oldCoreRedCommand": red_command, "oldCoreSha256": digest(old_core_replacements[
            "core\\litelayer\\litelayer.lib"]),
    }, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--old-core", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.build.resolve(), args.output.resolve(), args.old_core.resolve())
