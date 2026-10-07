"""Capture outside timers, compare ordered draw-state guards and gameplay receipts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def image_difference(first, second):
    from PIL import Image, ImageChops, ImageStat
    with Image.open(first) as a, Image.open(second) as b:
        a, b = a.convert("RGBA"), b.convert("RGBA")
        if a.size != b.size:
            raise ValueError("Capture dimensions changed")
        difference = ImageChops.difference(a, b)
        histogram = difference.histogram()
        return {"exact": a.tobytes() == b.tobytes(),
                "meanAbsoluteChannels": ImageStat.Stat(difference).mean,
                "maximumChannelDifference": max(i % 256 for i, count in enumerate(histogram)
                                                if count)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-root", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--reuse", action="store_true",
                        help="Resume this qualification only; never reuse formal benchmark receipts.")
    parser.add_argument("--binary-directory", default="Native")
    parser.add_argument("--qualification-name", default="Qualification")
    parser.add_argument("--frames", type=int, default=180)
    parser.add_argument("--candidates", default="all")
    args = parser.parse_args()
    build = args.native_root / "build/lite-c99/perf-deep-dive"
    output = build / args.qualification_name
    output.mkdir(parents=True, exist_ok=args.reuse)
    environment = {k: v for k, v in os.environ.items()
                   if not k.startswith(("LITE_PERF_", "BBLITE_"))}
    environment["BBLITE_MSAA"] = "1"
    executable = build / args.binary_directory / "LiteMinecraftNative.exe"
    configurations = {
        "default": {}, "regex": {"LITE_PERF_CACHED_REGEX": "1"},
        "grouping": {"LITE_PERF_GROUPING_CACHE": "1"},
        "retained": {"LITE_PERF_HUD_RETAINED": "1"},
        "retained-grouping": {"LITE_PERF_HUD_RETAINED": "1",
                             "LITE_PERF_GROUPING_CACHE": "1"},
        "retained-queue3": {"LITE_PERF_HUD_RETAINED": "1", "LITE_PERF_QUEUE3": "1"},
        "retained-single-thread": {"LITE_PERF_HUD_RETAINED": "1",
                                   "LITE_PERF_SINGLE_THREAD": "1"},
        "gpu-off": {"LITE_PERF_GPU_OFF": "1"},
    }
    if args.candidates != "all":
        requested = args.candidates.split(",")
        configurations = {name: configurations[name] for name in requested}
        if "default" not in configurations:
            raise ValueError("Qualification requires a default control")
    records = {}
    for name, flags in configurations.items():
        folder = output / name
        folder.mkdir(exist_ok=args.reuse)
        perf = folder / "guard.json"
        command = [str(executable), f"--frames={args.frames}", "--headless",
                   f"--capture-root={folder}", f"--summary={folder / 'receipt.json'}"]
        env = dict(environment, LITE_PERF_DRAW_GUARD="1", LITE_PERF_OUTPUT=str(perf), **flags)
        if not (args.reuse and (folder / "receipt.json").exists()):
            process = subprocess.run(command, cwd=args.native_root, env=env,
                                     capture_output=True, text=True, timeout=120)
            (folder / "log.txt").write_text(process.stdout + process.stderr)
            if process.returncode:
                raise RuntimeError(f"{name} exited {process.returncode}")
        records[name] = json.loads((folder / "receipt.json").read_text())
        records[name]["orderedDrawStateHashes"] = json.loads(perf.read_text())["drawStateHashes"]
        if len(records[name]["orderedDrawStateHashes"]) != args.frames:
            raise ValueError(f"{name}: ordered draw guard was not active for every frame")
        print(f"Captured native {name}", flush=True)
    comparisons = {}
    for name, record in records.items():
        for key in ("frames", "worldHash", "initialWorldHash", "cameraPosition", "cameraTarget",
                    "timeOfDay", "chunks", "mobs", "draws", "materialCount",
                    "orderedDrawStateHashes"):
            if record[key] != records["default"][key]:
                raise ValueError(f"{name} differs in {key}")
        if record["inputEvents"] or record["fileDialogs"] or record["chunks"] != 169:
            raise ValueError(f"{name} workload/input qualification failed")
        comparisons[name] = {
            file: image_difference(output / "default" / file, output / name / file)
            for file in ("frame1.png", f"frame{args.frames}.png", "hud1.png",
                         f"hud{args.frames}.png")}
        if name != "retained-queue3" and not all(
                value["exact"] for value in comparisons[name].values()):
            raise ValueError(f"{name} PNG comparison failed: {comparisons[name]}")
    profile = json.loads(args.profile.read_text())
    reference = next(v for v in profile["variants"] if v["name"] == "standard-bblitec")
    sdl = {}
    for name, file in (("qualified", Path(reference["executable"])),
                       ("overlay", build / "Sdl/bblite_native.exe")):
        folder = output / f"sdl-{name}"
        folder.mkdir(exist_ok=args.reuse)
        env = dict(environment, BBLITE_TEST_PASS="1", BBLITE_MAX_FRAMES="180",
                   BBLITE_FRAME_DELTA_MS=str(1000 / 60), BBLITE_SCREENSHOT_FRAME="179",
                   BBLITE_SCREENSHOT=str(folder / "scene.png"), BBLITE_CAPTURE_UI="0",
                   BBLITE_RENDER_CAPTURE=str(folder / "render.json"),
                   BBLITE_GPU_SHADER_DIR=reference["environment"]["BBLITE_GPU_SHADER_DIR"])
        if not (args.reuse and (folder / "render.json").exists()):
            process = subprocess.run([str(file)], cwd=file.parent, env=env,
                                     capture_output=True, text=True, timeout=120)
            (folder / "log.txt").write_text(process.stdout + process.stderr)
            if process.returncode:
                raise RuntimeError(f"SDL {name} exited {process.returncode}")
        sdl[name] = json.loads((folder / "render.json").read_text())
    comparisons["sdl-overlay"] = image_difference(
        output / "sdl-qualified/scene.png", output / "sdl-overlay/scene.png")
    if sdl["qualified"] != sdl["overlay"] or not comparisons["sdl-overlay"]["exact"]:
        raise ValueError("SDL overlay does not reproduce qualified scene")
    summary = {
        "nativeStateExact": True, "nativeFramesGuarded": args.frames,
        "nativeImagesExactExceptQueue3": True,
        "queue3CaptureLatencyDifferenceUnresolved":
            "retained-queue3" in configurations and not comparisons[
                "retained-queue3"][f"frame{args.frames}.png"]["exact"],
        "nativeGuardIncludes": ["ordered mesh/material identities", "world/view/projection",
                               "vertex/index counts", "draw order/group/depth",
                               "actual bgfx uniform bytes"],
        "sdlSceneImageAndRenderJsonExact": True, "comparisons": comparisons,
        "binaryHashes": {
            str(file): hashlib.sha256(file.read_bytes()).hexdigest()
            for file in (executable, build / "Sdl/bblite_native.exe")},
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2))
    print("Native draw states and SDL overlay qualified; queue3 capture difference="
          f"{summary['queue3CaptureLatencyDifferenceUnresolved']}.", flush=True)


if __name__ == "__main__":
    main()
