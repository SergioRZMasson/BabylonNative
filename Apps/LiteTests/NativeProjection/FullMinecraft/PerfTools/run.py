"""Balanced full-workload controls; bootstrap run averages, never pooled frames."""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import statistics
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-root", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--parent-harness", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=15)
    parser.add_argument("--suite", choices=("primary", "renderer"), default="primary")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    for name in list(os.environ):
        if name.startswith(("LITE_PERF_", "BBLITE_")):
            del os.environ[name]
    spec = importlib.util.spec_from_file_location("parent_benchmark", args.parent_harness)
    parent = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(parent)
    profile = json.loads(args.profile.read_text())
    config = dict(profile["workload"])
    config["repetitions"] = args.rounds
    config.update(seed=1337, radius=6)
    original_native = next(v for v in profile["variants"] if v["name"] == "pure-cpp")
    original_sdl = next(v for v in profile["variants"] if v["name"] == "standard-bblitec")
    build = args.native_root / "build/lite-c99/perf-deep-dive"
    native = dict(original_native, executable=str(build / "Native/LiteMinecraftNative.exe"))
    sdl = dict(original_sdl, executable=str(build / "Sdl/bblite_native.exe"),
               workingDirectory=str(build / "Sdl"))

    def variant(base, name, flags=None, phases=True):
        result = dict(base, name=name, environment=dict(base["environment"]))
        if phases:
            result["environment"]["LITE_PERF_PHASES"] = "1"
        result["environment"].update(flags or {})
        return result

    if args.suite == "primary":
        variants = [
            dict(original_sdl, name="sdl-qualified"),
            variant(sdl, "sdl-off", phases=False),
            variant(sdl, "sdl-phases"),
            variant(sdl, "sdl-allocations", {"LITE_PERF_ALLOCATIONS": "1"}),
            dict(original_native, name="native-qualified"),
            variant(native, "native-off", phases=False),
            variant(native, "native-phases"),
            variant(native, "native-allocations", {"LITE_PERF_ALLOCATIONS": "1"}),
            variant(native, "native-hud-off", {"LITE_PERF_HUD_OFF": "1"}),
            variant(native, "native-hud-retained", {"LITE_PERF_HUD_RETAINED": "1"}),
            variant(native, "native-regex", {"LITE_PERF_CACHED_REGEX": "1"}),
            variant(native, "native-gc-off", {"LITE_PERF_GC_OFF": "1"}),
            variant(native, "native-grouping", {"LITE_PERF_GROUPING_CACHE": "1"}),
        ]
        pairs = [
            ("sdl-qualified", "sdl-off"), ("sdl-off", "sdl-phases"),
            ("sdl-phases", "sdl-allocations"),
            ("native-qualified", "native-off"), ("native-off", "native-phases"),
            ("native-phases", "native-allocations"),
            ("native-phases", "native-hud-off"), ("native-phases", "native-hud-retained"),
            ("native-phases", "native-regex"), ("native-phases", "native-gc-off"),
            ("native-phases", "native-grouping"), ("sdl-qualified", "native-qualified"),
            ("sdl-phases", "native-phases"), ("sdl-qualified", "native-hud-retained"),
        ]
    else:
        variants = [
            variant(native, "native-phases"),
            variant(native, "native-init-profile-off", {"LITE_PERF_GPU_OFF": "1"}),
            variant(native, "native-retained", {"LITE_PERF_HUD_RETAINED": "1"}),
            variant(native, "native-retained-init-profile-off", {
                "LITE_PERF_HUD_RETAINED": "1", "LITE_PERF_GPU_OFF": "1"}),
            variant(native, "native-retained-queue3", {
                "LITE_PERF_HUD_RETAINED": "1", "LITE_PERF_QUEUE3": "1"}),
            variant(native, "native-retained-single-thread", {
                "LITE_PERF_HUD_RETAINED": "1", "LITE_PERF_SINGLE_THREAD": "1"}),
            variant(native, "native-retained-grouping", {
                "LITE_PERF_HUD_RETAINED": "1", "LITE_PERF_GROUPING_CACHE": "1"}),
        ]
        pairs = [("native-phases", "native-init-profile-off")] + [
            ("native-retained", v["name"]) for v in variants[3:]]

    manifest = {
        "profile": str(args.profile), "parentHarness": str(args.parent_harness),
        "parentHarnessSha256": parent.digest(args.parent_harness),
        "config": config, "variants": variants, "suite": args.suite,
        "binaryHashes": {v["name"]: parent.digest(Path(v["executable"])) for v in variants},
        "sourceFiles": {},
        "nonParityDiagnostics": ["native-hud-off", "native-gc-off"],
        "gpuToggleMeaning": (
            "LITE_PERF_GPU_OFF changes init.profile only; this pinned D3D11 backend "
            "still issues frame timestamp queries unconditionally when supported. "
            "It is NOT a GPU timestamp-query disable control."),
        "timestampUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    for folder in (build / "Native/PerfOverlay", build / "Native/Projected",
                   build / "SdlOverlay", args.native_root /
                   "Apps/LiteTests/NativeProjection/FullMinecraft/PerfTools"):
        for file in folder.rglob("*"):
            if file.is_file() and file.suffix in (".cpp", ".hpp", ".h", ".py", ".json", ".txt"):
                manifest["sourceFiles"][str(file)] = parent.digest(file)
    for file in (build / "Native/CMakeCache.txt", build / "Sdl/CMakeCache.txt"):
        manifest["sourceFiles"][str(file)] = parent.digest(file)
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2))
    results = {v["name"]: [] for v in variants}
    runs = []
    by_name = {v["name"]: v for v in variants}
    for repetition, order in enumerate(parent.balanced_order(list(by_name), args.rounds)):
        for name in order:
            selected = dict(by_name[name], environment=dict(by_name[name]["environment"]))
            perf = args.output.resolve() / f"{repetition:02d}-{name}.perf.json"
            if name not in ("native-qualified", "sdl-qualified"):
                selected["environment"]["LITE_PERF_OUTPUT"] = str(perf)
            measurement = parent.run_variant(selected, config, args.output, repetition)
            if name not in ("native-qualified", "sdl-qualified"):
                data = json.loads(perf.read_text())
                if data["phasesEnabled"] and len(data["phases"]["total"]) != config["frames"]:
                    raise ValueError(f"{name}: wrong phase sample count")
                if data["phasesEnabled"]:
                    if any(not math.isfinite(x) or x < 0 for values in data["phases"].values()
                           for x in values):
                        raise ValueError(f"{name}: invalid phase values")
                measurement["performanceReceipt"] = str(perf)
            results[name].append(measurement)
            runs.append(dict(measurement, name=name))
            (args.output / "measurements.json").write_text(json.dumps(runs, indent=2))
            print(f"round={repetition + 1}/{args.rounds} {name}: "
                  f"{measurement['averageMs']:.6f} ms", flush=True)
            time.sleep(config.get("cooldownSeconds", 1))
    summary = {
        "mediansOfRunAveragesMs": {
            name: statistics.median(r["averageMs"] for r in values)
            for name, values in results.items()},
        "comparisons": {
            f"{before} -> {after}": parent.paired_comparison(results[before], results[after])
            for before, after in pairs},
        "rounds": args.rounds,
    }
    for variant_name, values in results.items():
        receipts = [json.loads(Path(r["performanceReceipt"]).read_text()) for r in values
                    if "performanceReceipt" in r]
        if not receipts:
            continue
        count = config["frames"]
        summary.setdefault("phaseMedianRunMeansMs", {})[variant_name] = {
            key: statistics.median(statistics.mean(r["phases"][key]) for r in receipts)
            for key in receipts[0]["phases"] if receipts[0]["phases"][key]}
        summary.setdefault("counterMedianRunMeans", {})[variant_name] = {
            key: statistics.median(statistics.mean(r["counts"][key]) for r in receipts)
            for key in receipts[0]["counts"] if receipts[0]["counts"][key]}
        summary.setdefault("processCpuMsPerFrame", {})[variant_name] = statistics.median(
            r["processCpuMs"] / count for r in receipts)
        summary.setdefault("measuredWallMsPerFrame", {})[variant_name] = statistics.median(
            r["measuredWallMs"] / count for r in receipts)
        summary.setdefault("privateByteGrowthMedian", {})[variant_name] = statistics.median(
            r["privateBytesEnd"] - r["privateBytesBegin"] for r in receipts)
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary["mediansOfRunAveragesMs"], indent=2), flush=True)


if __name__ == "__main__":
    main()
