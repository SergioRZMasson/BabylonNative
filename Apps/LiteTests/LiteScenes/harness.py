"""Serial parity and balanced run-pair measurements. Output is written only between runs."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

import numpy as np
from PIL import Image


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(path, value):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def catalog(data, scene):
    return next(x for x in json.loads((Path(data) / "scenes.json").read_text()) if x["id"] == scene)


def clean_environment(overrides=None):
    result = {k: v for k, v in os.environ.items() if not k.startswith(("BBLITE_", "LITE_PROFILE_"))}
    result.update(overrides or {})
    return result


def verify_inputs(record):
    for item in record["sourceInputs"]:
        if digest(item["path"]) != item["sha256"]:
            raise ValueError(f"Stale scene source: {item['path']}")
    for item in record["resources"]:
        if digest(item["path"]) != item["sha256"]:
            raise ValueError(f"Stale raw asset: {item['path']}")
    for item in record["packagedAssets"]:
        if digest(item["path"]) != item["sha256"]:
            raise ValueError(f"Stale packaged asset: {item['path']}")


def run(command, output, environment=None, cwd=None, timeout=180):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    begin = time.perf_counter()
    try:
        child = subprocess.run(
            [str(x) for x in command], capture_output=True, env=environment or clean_environment(),
            cwd=cwd, timeout=timeout, check=False,
        )
        receipt = {"command": [str(x) for x in command], "cwd": str(cwd) if cwd else None,
                   "exitCode": child.returncode, "processWallMs": (time.perf_counter() - begin) * 1000}
        (output / "stdout.log").write_bytes(child.stdout)
        (output / "stderr.log").write_bytes(child.stderr)
    except subprocess.TimeoutExpired as error:
        receipt = {"command": [str(x) for x in command], "exitCode": None, "timeoutSeconds": timeout}
        (output / "stdout.log").write_bytes(error.stdout or b"")
        (output / "stderr.log").write_bytes(error.stderr or b"")
    write(output / "process.json", receipt)
    return receipt


def compare_rgb(actual, reference, background, threshold):
    """Same RGB 0..255 MAD and reference-only Euclidean foreground mask as bblitec."""
    if actual.shape != reference.shape or actual.ndim != 3 or actual.shape[2] != 3:
        raise ValueError("Image extents must match exactly")
    difference = np.abs(actual.astype(np.int16) - reference.astype(np.int16)).mean(axis=2)
    delta = reference.astype(np.int32) - np.asarray(background, dtype=np.int32)
    mask = (delta * delta).sum(axis=2) > threshold * threshold
    return {"fullMad": float(difference.mean()),
            "foregroundMad": float(difference[mask].mean()) if mask.any() else 0.0,
            "foregroundPixels": int(mask.sum()), "pixels": int(mask.size)}


def parity(host, data, scene, output):
    record = catalog(data, scene)
    verify_inputs(record)
    if digest(record["golden"]) != record["goldenSha256"]:
        raise ValueError("Stale golden")
    output = Path(output)
    receipt = run([host, f"--scene={scene}", f"--data={Path(data).resolve()}",
                   f"--out={output.resolve()}", "--capture", "--warmup=120"], output)
    result = {"scene": scene, "host": str(host), "hostSha256": digest(host),
              "candidateScriptSha256": digest(Path(data) / f"scene{scene}.js")
              if Path(host).stem.endswith("Js") else None,
              "goldenSha256": record["goldenSha256"], "process": receipt, "pass": False}
    capture = output / "frame.bgra"
    if receipt["exitCode"] == 0 and capture.is_file():
        expected = record["width"] * record["height"] * 4
        if capture.stat().st_size != expected:
            raise ValueError("Invalid readback extent")
        image = Image.frombytes("RGBA", (record["width"], record["height"]),
                                capture.read_bytes(), "raw", "BGRA")
        image.save(output / "frame.png")
        reference = Image.open(record["golden"]).convert("RGB")
        if reference.size != image.size:
            raise ValueError("Registry/golden extent mismatch")
        config = record["parity"]
        metrics = compare_rgb(np.asarray(image.convert("RGB")), np.asarray(reference),
                              config["backgroundColor"], config["backgroundThreshold"])
        result.update(metrics)
        result["thresholds"] = {k: config[k] for k in ("maxFullMad", "maxForegroundMad")}
        result["pass"] = (metrics["fullMad"] <= config["maxFullMad"]
                          and metrics["foregroundMad"] <= config["maxForegroundMad"])
        difference = np.abs(np.asarray(image.convert("RGB")).astype(np.int16)
                            - np.asarray(reference).astype(np.int16))
        Image.fromarray(np.minimum(255, difference * 8).astype(np.uint8)).save(output / "diff.png")
    write(output / "parity.json", result)
    print(json.dumps({k: result.get(k) for k in ("scene", "pass", "fullMad", "foregroundMad")}))
    return result


def bootstrap_ratios(candidate, baseline, resamples=10000, seed=1337):
    a = np.asarray(candidate, dtype=np.float64)
    b = np.asarray(baseline, dtype=np.float64)
    if (len(a) != len(b) or len(a) < 2 or not
            (np.all(np.isfinite(a)) and np.all(np.isfinite(b)) and np.all(a > 0) and np.all(b > 0))):
        raise ValueError("At least two positive matched run pairs are required")
    logs = np.log(a / b)
    indices = np.random.default_rng(seed).integers(0, len(a), (resamples, len(a)))
    estimates = np.exp(logs[indices].mean(axis=1))
    low, high = np.quantile(estimates, [0.025, 0.975])
    return {"ratio": float(np.exp(logs.mean())), "ci95": [float(low), float(high)],
            "pairs": len(a), "resamples": resamples, "method": "paired-log-ratio-percentile-bootstrap"}


def baseline_parity(data, scene, backend, output):
    record = catalog(data, scene)
    verify_inputs(record)
    output = Path(output).resolve()
    executable = Path(record["executable"])
    env = clean_environment(record["pose"]["nativeEnvironment"])
    env.update({"BBLITE_GPU_BACKEND": backend, "BBLITE_MAX_FRAMES": "2",
                "BBLITE_BENCHMARK_FRAMES": "0", "BBLITE_SCREENSHOT_FRAME": "1",
                "BBLITE_SCREENSHOT": str(output / "frame.png"),
                "BBLITE_RENDER_CAPTURE": str(output / "capture.json"),
                "BBLITE_BUILD_STAMP_OUT": str(output / "build-stamp.txt"),
                "BBLITE_TEST_PASS": "1", "BBLITE_FRAME_DELTA_MS": "16.666666666666668"})
    process = run([executable], output, env, executable.parent)
    result = {"scene": scene, "backend": backend, "pass": False, "process": process}
    if process["exitCode"] == 0:
        reported = (output / "build-stamp.txt").read_text().strip()
        if reported != record["buildStamp"]:
            raise ValueError("Executable runtime stamp does not match verified source inputs")
        image = np.asarray(Image.open(output / "frame.png").convert("RGB"))
        reference = np.asarray(Image.open(record["golden"]).convert("RGB"))
        config = record["parity"]
        metrics = compare_rgb(image, reference, config["backgroundColor"], config["backgroundThreshold"])
        result.update(metrics)
        result["pass"] = metrics["fullMad"] <= config["maxFullMad"] and metrics["foregroundMad"] <= config["maxForegroundMad"]
        result["reportedBuildStamp"] = reported
    write(output / "parity.json", result)
    return result


def native_metrics(output):
    data = json.loads((Path(output) / "run.json").read_text())
    samples = np.asarray([s["completeHostWallMs"] for s in data["samples"]])
    if data["warmup"] != 120 or len(samples) != 2000:
        raise ValueError("Incorrect benchmark frame counts")
    gpu = [s["gpu"]["ms"] for s in data["samples"] if s["gpu"] is not None]
    return {"meanMs": float(samples.mean()), "p95Ms": float(np.quantile(samples, 0.95, method="lower")),
            "medianMs": float(np.median(samples)), "setupFromMainMs": data["setupFromMainMs"],
            "firstGpuCompletedFrameFromMainMs": data["firstGpuCompletedFrameFromMainMs"],
            "shaderCompileMs": data["shaderCompileMs"], "processCpuMs": data["processCpuMs"],
            "firstPresentMs": data["firstPresentMs"], "scope": "complete-host-enqueue-through-bgfx-frame",
            "gpuMeanMs": float(np.mean(gpu)) if gpu else None, "uniqueGpuQueries": len(gpu),
            "quartileMeansMs": [float(x.mean()) for x in np.array_split(samples, 4)]}


def bblitec_metrics(output):
    text = (Path(output) / "stdout.log").read_text(errors="replace")
    match = re.search(
        r"benchmark \| driver=.*?frames=(\d+).*?(?:average|avg|mean)=([\d.]+) ms"
        r".*?median=([\d.]+) ms.*?p95=([\d.]+) ms", text,
    )
    if not match or int(match[1]) != 2000:
        raise ValueError("Missing or incorrect bblitec benchmark report")
    return {"meanMs": float(match[2]), "medianMs": float(match[3]), "p95Ms": float(match[4]),
            "firstPresentMs": None, "shaderCompileMs": None, "processCpuMs": None,
            "scope": "existing-bblitec-loop-bracket-excludes-event-pump",
            "roundingMs": 0.001}


def benchmark_pair(host, data, scene, backend, index, output):
    record = catalog(data, scene)
    verify_inputs(record)
    executable = Path(record["executable"])
    if digest(executable) != record["executableSha256"]:
        raise ValueError("Stale bblitec executable")
    output = Path(output)
    order = ("native", "baseline") if index % 2 == 0 else ("baseline", "native")
    result = {"scene": scene, "pair": index, "order": list(order), "backend": backend,
              "host": str(host), "hostSha256": digest(host),
              "baselineSha256": digest(executable),
              "candidateScriptSha256": digest(Path(data) / f"scene{scene}.js")
              if Path(host).stem.endswith("Js") else None}
    for lane in order:
        directory = output / lane
        if lane == "native":
            command = [host, f"--scene={scene}", f"--data={Path(data).resolve()}",
                       f"--out={directory.resolve()}", "--warmup=120", "--measure=2000"]
            env = clean_environment()
            cwd = None
        else:
            command = [executable]
            env = clean_environment(record["pose"]["nativeEnvironment"])
            env.update({"BBLITE_GPU_BACKEND": backend, "BBLITE_BENCHMARK_FRAMES": "2000",
                        "BBLITE_TEST_PASS": "1", "BBLITE_FRAME_DELTA_MS": "16.666666666666668",
                        "BBLITE_BUILD_STAMP_OUT": str(directory.resolve() / "build-stamp.txt")})
            cwd = executable.parent
        receipt = run(command, directory, env, cwd)
        if receipt["exitCode"] != 0:
            result["error"] = {"lane": lane, "process": receipt}
            break
        if lane == "baseline" and (directory / "build-stamp.txt").read_text().strip() != record["buildStamp"]:
            raise ValueError("Baseline executable runtime stamp changed")
        result[lane] = native_metrics(directory) if lane == "native" else bblitec_metrics(directory)
    write(output / "pair.json", result)
    return result


def summarize(pairs):
    good = [p for p in pairs if "native" in p and "baseline" in p and "error" not in p]
    if len(good) < 2:
        return {"verdict": "blocked", "successfulPairs": len(good)}
    result = {"successfulPairs": len(good)}
    for metric in ("meanMs", "p95Ms"):
        result[metric] = bootstrap_ratios([p["native"][metric] for p in good],
                                        [p["baseline"][metric] for p in good])
    numerical = all(result[m]["ci95"][1] <= 1 for m in ("meanMs", "p95Ms"))
    result["timingOnlyVerdict"] = (
        "equal-or-faster" if numerical else
        "slower" if any(result[m]["ci95"][0] > 1 for m in ("meanMs", "p95Ms")) else "inconclusive"
    )
    result["verdict"] = "unqualified"
    result["acceptanceBlockers"] = [
        "Baseline bracket omits event pumping; no matched full-host envelope.",
        "Actual first-present timing, baseline raw samples, GPU and latency unavailable.",
        "Effective MSAA and matching hidden-window presentation require GPU-state qualification.",
        "Numerical timing is not acceptance without native/JS golden parity and workload qualification.",
    ]
    for lane in ("native", "baseline"):
        result[lane] = {m: float(np.mean([p[lane][m] for p in good]))
                        for m in ("meanMs", "p95Ms")}
    for metric in ("setupFromMainMs", "shaderCompileMs"):
        values = [p["native"].get(metric) for p in good]
        result["native"][metric] = float(np.mean(values)) if all(x is not None for x in values) else None
    result["startupRatio"] = None
    return result


def benchmark(host, data, scenes, backend, pairs, output):
    output = Path(output)
    successful = True
    def acquire(scene, index, directory):
        receipt = directory / "pair.json"
        if receipt.exists():
            result = json.loads(receipt.read_text())
            expected = catalog(data, scene)
            if (result["hostSha256"] != digest(host) or result["scene"] != scene or
                    result["backend"] != backend or
                    result["baselineSha256"] != expected["executableSha256"] or
                    result.get("candidateScriptSha256") !=
                    (digest(Path(data) / f"scene{scene}.js") if Path(host).stem.endswith("Js") else None)):
                raise ValueError(f"Existing pair belongs to another configuration: {receipt}")
            return result
        return benchmark_pair(host, data, scene, backend, index, directory)
    for scene in scenes:
        results = []
        directory = output / f"scene{scene}"
        for index in range(pairs):
            result = acquire(scene, index, directory / f"pair{index:02d}")
            results.append(result)
            write(directory / "summary.json", summarize(results))
            print(f"scene{scene} {backend} pair {index + 1}/{pairs}", flush=True)
            if "error" in result:
                successful = False
                break
        summary = summarize(results)
        # Increase close/inconclusive timing comparisons to 15 pairs, preserving AB/BA ordering.
        close = any(abs(summary.get(m, {}).get("ratio", 2) - 1) < 0.1
                    for m in ("meanMs", "p95Ms"))
        if pairs < 15 and (close or summary.get("timingOnlyVerdict") == "inconclusive"):
            for index in range(pairs, 15):
                result = acquire(scene, index, directory / f"pair{index:02d}")
                results.append(result)
                write(directory / "summary.json", summarize(results))
                print(f"scene{scene} {backend} pair {index + 1}/15", flush=True)
                if "error" in result:
                    successful = False
                    break
    return successful


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("parity", "baseline-parity", "benchmark"))
    parser.add_argument("--host", type=Path)
    parser.add_argument("--data", required=True, type=Path)
    parser.add_argument("--scene", required=True, help="Scene number or comma-separated numbers")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--backend", choices=("sdl_gpu", "dawn"), default="sdl_gpu")
    parser.add_argument("--pairs", type=int, default=5)
    args = parser.parse_args()
    scenes = [int(x) for x in args.scene.split(",")]
    if args.action == "baseline-parity":
        results = [baseline_parity(args.data, s, args.backend, args.output / f"scene{s}") for s in scenes]
        write(args.output / "summary.json", results)
        return 0 if all(x["pass"] for x in results) else 1
    if args.host is None:
        parser.error("--host is required for parity/benchmark")
    if args.action == "parity":
        if len(scenes) != 1:
            parser.error("parity takes one scene")
        return 0 if parity(args.host, args.data, scenes[0], args.output)["pass"] else 1
    if args.pairs < 5:
        parser.error("benchmark requires at least five run pairs")
    return 0 if benchmark(args.host, args.data, scenes, args.backend, args.pairs, args.output) else 1


if __name__ == "__main__":
    sys.exit(main())
