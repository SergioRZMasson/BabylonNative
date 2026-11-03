"""Recompile only copied PAL host files and relink immutable baseline engine objects."""

import argparse
import ctypes
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path


def argv(command):
    shell = ctypes.WinDLL("shell32")
    shell.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
    kernel = ctypes.WinDLL("kernel32")
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    count = ctypes.c_int()
    values = shell.CommandLineToArgvW(command, ctypes.byref(count))
    try:
        return [values[i] for i in range(count.value)]
    finally:
        kernel.LocalFree(values)


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Stale PAL instrumentation anchor: {old[:100]!r}")
    return text.replace(old, new)


def prepare(corpus, output, scenes, probe_source=None):
    here = Path(__file__).resolve().parent
    probe_source = probe_source or here / "PresentProbe.cpp"
    records = []
    for scene in scenes:
        original = corpus / "native" / f"build-scene{scene}-release"
        destination = output / f"scene{scene}"
        destination.mkdir(parents=True, exist_ok=True)
        commands = subprocess.run(
            ["ninja", "-C", str(original), "-t", "commands", "bblite_native"],
            capture_output=True, text=True, check=True,
        ).stdout.splitlines()
        build = []
        replacements = {}
        for name in ("pal_sdl_gpu.cpp", "pal_dawn.cpp"):
            source = corpus / "native" / "src" / name
            text = source.read_text()
            text = '#include "PresentProbe.h"\n' + text
            anchor = "    const FrameOptions frame_options = read_frame_options();"
            text = replace_once(text, anchor, "    LiteScenesProbe::Install(0);\n" + anchor)
            anchor = "while (captures.keep_running(running, frame)) {"
            text = replace_once(text, anchor, anchor + "\n"
                                "            const double m0_full_start = monotonic_milliseconds();\n"
                                "            if (frame == 0) LiteScenesProbe::MarkSourceFrame();")
            if name == "pal_sdl_gpu.cpp":
                text = replace_once(text, "samples.push_back(end - start);",
                                    "samples.push_back(end - m0_full_start);")
                text = replace_once(text, "std::vector<double> samples;",
                                    "std::vector<double> samples;\n"
                                    "        samples.reserve(static_cast<size_t>(frame_options.benchmark_frames));")
                text = replace_once(text, "        report_benchmark(\n",
                                    "        LiteScenesProbe::WriteBaseline(samples.data(), samples.size());\n"
                                    "        report_benchmark(\n")
            else:
                old = """        if (benchmark && frame >= benchmark_warmup) {
            benchmark_samples.push_back(
                monotonic_milliseconds() - benchmark_start);
        }
"""
                text = replace_once(text, old, "")
                text = replace_once(text, "const double benchmark_start = monotonic_milliseconds();",
                                    "[[maybe_unused]] const double benchmark_start = monotonic_milliseconds();")
                old = "        finish_frame(engine);\n        ++frame;\n"
                text = replace_once(text, old, old + """
        if (benchmark && frame - 1 >= benchmark_warmup) {
            benchmark_samples.push_back(monotonic_milliseconds() - m0_full_start);
        }
""")
                text = replace_once(text, '    report_benchmark(benchmark_samples, "Dawn", "D3D12");',
                                    "    LiteScenesProbe::WriteBaseline(benchmark_samples.data(), benchmark_samples.size());\n"
                                    '    report_benchmark(benchmark_samples, "Dawn", "D3D12");')
            copied = destination / name
            copied.write_text(text)
            command = next(c for c in commands if c.endswith(str(source)) or
                           c.endswith(str(source).replace("\\", "/")))
            args = argv(command)
            old_object = next(a[3:] for a in args if a.startswith("/Fo"))
            new_object = destination / (name + ".obj")
            replacements[old_object.lower()] = str(new_object)
            args = [str(copied) if a == str(source) else a for a in args]
            args = ["/Fo" + str(new_object) if a.startswith("/Fo") else
                    "/Fd" + str(destination) + "\\" if a.startswith("/Fd") else a for a in args]
            args += ["/I" + str(here), "/I" + str(source.parent)]
            build.append(subprocess.list2cmdline(args))
        compiler = argv(next(c for c in commands if " /Fo" in c))[0]
        probe_object = destination / "PresentProbe.cpp.obj"
        build.append(subprocess.list2cmdline([
            compiler, "/nologo", "/std:c++20", "/EHsc", "/MD", "/O2", "/W4", "/WX",
            "/DWIN32_LEAN_AND_MEAN", "/DNOMINMAX", "/c", str(probe_source),
            "/I" + str(here), "/Fo" + str(probe_object),
        ]))
        link = next(c for c in reversed(commands) if "link.exe " in c and " /out:" in c)
        link = link.split(" -- ", 1)[1].split(" && ", 1)[0].rstrip('"')
        args = argv(link)
        rewritten = []
        for arg in args:
            if arg.lower() in replacements:
                rewritten.append(replacements[arg.lower()])
            elif arg.lower().endswith(".obj") and not Path(arg).is_absolute():
                rewritten.append(str(original / arg))
            elif arg.lower().startswith("/out:"):
                rewritten.append("/out:" + str(destination / "bblite_native.exe"))
            elif arg.lower().startswith("/implib:"):
                rewritten.append("/implib:" + str(destination / "bblite_native.lib"))
            elif arg.lower().startswith("/pdb:"):
                rewritten.append("/pdb:" + str(destination / "bblite_native.pdb"))
            else:
                rewritten.append(arg)
        rewritten += [str(probe_object), "psapi.lib", "d3d11.lib", "dxgi.lib"]
        build.append(subprocess.list2cmdline(rewritten))
        script = destination / "build.cmd"
        script.write_text("@echo off\n" + "\n".join(
            command + "\nif errorlevel 1 exit /b 1" for command in build) + "\n")
        for dll in original.glob("*.dll"):
            shutil.copy2(dll, destination / dll.name)
        records.append({
            "scene": scene, "build": str(script), "original": str(original / "bblite_native.exe"),
            "originalSha256": hashlib.sha256((original / "bblite_native.exe").read_bytes()).hexdigest(),
            "executable": str(destination / "bblite_native.exe"),
            "probeSourceSha256": hashlib.sha256(probe_source.read_bytes()).hexdigest(),
            "assets": str(original / "assets"), "shaders": str(original / "shaders"),
            "copiedHostSha256": {name: hashlib.sha256((destination / name).read_bytes()).hexdigest()
                                 for name in ("pal_sdl_gpu.cpp", "pal_dawn.cpp")},
        })
    (output / "manifest.json").write_text(json.dumps(records, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenes", required=True)
    parser.add_argument("--probe-source", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    prepare(args.corpus.resolve(), args.output.resolve(), [int(n) for n in args.scenes.split(",")],
            args.probe_source.resolve() if args.probe_source else None)
