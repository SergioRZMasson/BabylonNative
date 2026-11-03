"""Event-inclusive paired runs using isolated baseline PAL overlays and actual DXGI Present."""

import argparse
import json
from pathlib import Path
import sys

import numpy as np

import harness as h


def present_metrics(directory):
    value = json.loads((directory / "present.json").read_text())
    events = value["presents"]
    source = next(e for e in events if e["sourceFrame"] and e["result"] >= 0)
    if any(e["syncInterval"] != 0 for e in events):
        raise ValueError("Unexpected presentation pacing")
    return {
        "firstAnyPresentMs": value["firstPresentMs"],
        "firstSourcePresentMs": (source["endQpc"] - value["processStartQpc"]) *
        1000 / value["qpcFrequency"],
        "startupClockUncertaintyMs": value["calibrationUncertaintyTicks"] *
        1000 / value["qpcFrequency"],
        "presentCalls": len(events),
    }


def baseline_metrics(directory):
    value = json.loads((directory / "frames.json").read_text())
    samples = np.asarray(value["samplesMs"])
    if value["warmup"] != 120 or len(samples) != 2000 or value["measure"] != 2000:
        raise ValueError("Incorrect baseline frame counts")
    if not np.all(np.isfinite(samples)) or not np.all(samples > 0):
        raise ValueError("Invalid complete-host samples")
    return {
        "meanMs": float(samples.mean()),
        "p95Ms": float(np.quantile(samples, .95, method="lower")),
        "scope": value["scope"], "shaderCompileMs": None, "gpuMeanMs": None,
        "gpuUnavailableReason": "No live D3D12 query provider; replay counters are separate.",
    }


def pair(args, record, overlay, language, backend, index, directory):
    host = args.hosts / f"LiteScenes{language}.exe"
    executable = Path(overlay["executable"])
    identity = {
        "scene": record["id"], "language": language, "backend": backend,
        "hostSha256": h.digest(host), "baselineSha256": h.digest(executable),
        "originalBaselineSha256": h.digest(overlay["original"]),
        "probeSourceSha256": h.digest(Path(__file__).with_name("PresentProbe.cpp")),
        "copiedHostSha256": {name: h.digest(executable.parent / name)
                            for name in ("pal_sdl_gpu.cpp", "pal_dawn.cpp")},
        "scriptSha256": h.digest(args.data / f"scene{record['id']}.js") if language == "Js" else None,
    }
    receipt = directory / "pair.json"
    if receipt.exists():
        old = json.loads(receipt.read_text())
        if old["identity"] != identity:
            raise ValueError(f"Stale configuration: {receipt}")
        return old
    order = ["native", "baseline"] if index % 2 == 0 else ["baseline", "native"]
    result = {"identity": identity, "pair": index, "order": order}
    for lane in order:
        out = (directory / lane).resolve()
        if lane == "native":
            command = [host, f"--scene={record['id']}", f"--data={args.data}",
                       f"--out={out}", "--warmup=120", "--measure=2000", "--probe-present"]
            env = h.clean_environment()
            cwd = None
        else:
            command = [executable]
            cwd = executable.parent
            env = h.clean_environment(record["pose"]["nativeEnvironment"])
            env.update({
                "BBLITE_GPU_BACKEND": backend, "BBLITE_BENCHMARK_FRAMES": "2000",
                "BBLITE_TEST_PASS": "1", "BBLITE_FRAME_DELTA_MS": "16.666666666666668",
                "BBLITE_BUILD_STAMP_OUT": str(out / "build-stamp.txt"),
                "BBLITE_ASSET_DIR": overlay["assets"], "BBLITE_GPU_SHADER_DIR": overlay["shaders"],
                "LITE_M0_PRESENT_OUT": str(out / "present.json"),
                "LITE_M0_FRAME_OUT": str(out / "frames.json"),
            })
        process = h.run(command, out, env, cwd)
        if process["exitCode"] != 0:
            raise RuntimeError(f"Run failed: {out}")
        metrics = h.native_metrics(out) if lane == "native" else baseline_metrics(out)
        if lane == "baseline" and (out / "build-stamp.txt").read_text().strip() != record["buildStamp"]:
            raise ValueError("Baseline original-engine build stamp changed")
        metrics.update(present_metrics(out))
        result[lane] = metrics
    h.write(receipt, result)
    return result


def summarize(results, args, scene, language):
    summary = {"pairs": len(results), "scope": "complete-host-including-events"}
    for metric in ("meanMs", "p95Ms", "firstSourcePresentMs", "firstAnyPresentMs"):
        summary[metric] = h.bootstrap_ratios(
            [x["native"][metric] for x in results], [x["baseline"][metric] for x in results])
    summary["timingVerdict"] = (
        "equal-or-faster" if all(summary[m]["ci95"][1] <= 1 for m in ("meanMs", "p95Ms")) else
        "slower" if any(summary[m]["ci95"][0] > 1 for m in ("meanMs", "p95Ms")) else "inconclusive")
    parity_path = args.hosts / "parity" / language / str(scene) / "parity.json"
    parity = json.loads(parity_path.read_text()) if parity_path.exists() else {"pass": False}
    if parity.get("hostSha256") != results[0]["identity"]["hostSha256"]:
        raise ValueError("Parity belongs to another host binary")
    summary["parity"] = {k: parity.get(k) for k in ("pass", "fullMad", "foregroundMad")}
    summary["verdict"] = ("accept" if summary["timingVerdict"] == "equal-or-faster"
                          else summary["timingVerdict"])
    summary["acceptancePolicy"] = "Approved steady-state mean/P95 only; startup and GPU/latency are separate."
    summary["parity"]["expectedFailure"] = bool(parity.get("expectedFailure", False))
    for lane in ("native", "baseline"):
        summary[lane] = {
            metric: float(np.mean([x[lane][metric] for x in results]))
            for metric in ("meanMs", "p95Ms", "firstSourcePresentMs", "firstAnyPresentMs")}
        summary[lane]["shaderCompileMs"] = (
            float(np.mean([x[lane]["shaderCompileMs"] for x in results]))
            if lane == "native" else None)
    summary["limitations"] = [
        "Hidden-window DXGI Present completion, not display/photon time.",
        "sourceFrame is a CPU readiness tag; exact GPU source-frame/Present association is unqualified.",
        "Native bgfx D3D11 versus baseline SDL_GPU/Dawn D3D12; offline baked baseline assets/shaders.",
        "Baseline runtime PSO creation is included in startup but not separately instrumented.",
        "Native GPU spans overlap host spans; baseline GPU counters are replay-only.",
        "No inference of latency equivalence from throughput.",
    ]
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("preflight", "benchmark"))
    parser.add_argument("--hosts", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenes", default="159,161,163,168,200,247,248,249,256,257,258,259,260,266")
    parser.add_argument("--backends", default="sdl_gpu,dawn")
    parser.add_argument("--languages", default="Native,Js")
    args = parser.parse_args()
    args.hosts = args.hosts.resolve()
    args.data = args.data.resolve()
    args.output = args.output.resolve()
    overlays = {x["scene"]: x for x in json.loads(args.manifest.read_text())}
    for scene in [int(x) for x in args.scenes.split(",")]:
        record = h.catalog(args.data, scene)
        h.verify_inputs(record)
        overlay = overlays[scene]
        if h.digest(overlay["original"]) != record["executableSha256"]:
            raise ValueError("Original baseline binary changed")
        if args.action == "preflight":
            modified = json.loads(json.dumps(record))
            modified["executable"] = overlay["executable"]
            modified["executableSha256"] = h.digest(overlay["executable"])
            modified["pose"]["nativeEnvironment"].update({
                "BBLITE_ASSET_DIR": overlay["assets"], "BBLITE_GPU_SHADER_DIR": overlay["shaders"]})
            data = args.output / "catalog" / str(scene)
            h.write(data / "scenes.json", [modified])
            for backend in args.backends.split(","):
                result = h.baseline_parity(data, scene, backend, args.output / backend / str(scene))
                if not result["pass"]:
                    raise ValueError(f"Instrumented baseline parity failed: {scene}/{backend}")
            continue
        for language in args.languages.split(","):
            if language not in ("Native", "Js"):
                raise ValueError(f"Unknown language: {language}")
            for backend in args.backends.split(","):
                directory = args.output / language / backend / str(scene)
                results = []
                index = 0
                target = 15
                while index < target:
                    results.append(pair(args, record, overlay, language, backend, index,
                                        directory / f"pair{index:02d}"))
                    index += 1
                    if index < 2:
                        continue
                    summary = summarize(results, args, scene, language)
                    h.write(directory / "summary.json", summary)
                    print(f"{scene}/{language}/{backend}: {index}/{target} {summary['timingVerdict']}", flush=True)
                    if index == 6 and (summary["timingVerdict"] == "inconclusive" or any(
                            abs(summary[m]["ratio"] - 1) < .1 for m in ("meanMs", "p95Ms"))):
                        target = 15
    return 0


if __name__ == "__main__":
    sys.exit(main())
