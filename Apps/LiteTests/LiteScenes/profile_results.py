"""Summarize measured host spans without adding overlapping CPU/GPU intervals."""

import argparse
import json
from pathlib import Path
import statistics


def profile(directory):
    results = []
    for lane in sorted(directory.iterdir()):
        if not lane.is_dir():
            continue
        for scene in sorted(lane.glob("scene*")):
            summary_path = scene / "summary.json"
            if not summary_path.exists():
                continue
            summary = json.loads(summary_path.read_text())
            runs = []
            for path in sorted(scene.glob("pair[0-9][0-9]/native/run.json")):
                data = json.loads(path.read_text())
                samples = data["samples"]
                complete = statistics.mean(x["completeHostWallMs"] for x in samples)
                core = statistics.mean(x["coreAndCallbacksMs"] for x in samples)
                submit = statistics.mean(x["submitAndWaitMs"] for x in samples)
                gpu = [x["gpu"]["ms"] for x in samples if x["gpu"] is not None]
                runs.append({
                    "completeHostWallMs": complete, "coreAndCallbacksMs": core,
                    "submitAndWaitMs": submit, "coreFraction": core / complete,
                    "submitFraction": submit / complete,
                    "gpuMeanMs": statistics.mean(gpu) if gpu else None,
                    "processCpuPerFrameMs": data["processCpuMs"] / len(samples),
                })
            if not runs:
                continue
            average = {key: statistics.mean(r[key] for r in runs)
                       for key in runs[0] if all(r[key] is not None for r in runs)}
            results.append({
                "lane": lane.name, "scene": scene.name, "runs": len(runs),
                "timingOnlyVerdict": summary.get("timingOnlyVerdict"), "averages": average,
                "interpretation": (
                    "Measured candidate spans only. GPU and CPU overlap; do not sum them. "
                    "No baseline GPU/phase trace exists to establish a causal backend attribution."
                ),
            })
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--perf", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(profile(args.perf), indent=2) + "\n")
