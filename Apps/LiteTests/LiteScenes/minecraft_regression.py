"""Balanced complete-host Minecraft old/new regression; original seed1337/radius6 and retained UI."""

import argparse
import json
from pathlib import Path

import numpy as np

from harness import bootstrap_ratios, clean_environment, digest, run, write


def metrics(path):
    value = json.loads(path.read_text())
    if (value["seed"] != 1337 or value["radius"] != 6 or value["sampleCount"] != 2000 or
            value["warmupFrames"] != 120 or not value["retainedUi"] or
            value["measuredCaptures"] or value["physicalInputAccepted"]):
        raise ValueError("Minecraft workload or timing protocol changed")
    frames = np.asarray([s["completeHostWallMs"] for s in value["samples"]])
    return {"meanMs": float(frames.mean()),
            "p95Ms": float(np.quantile(frames, .95, method="lower"))}


def main(args):
    output = args.output.resolve()
    binaries = {"old": args.old.resolve(), "new": args.new.resolve()}
    hashes = {k: digest(v) for k, v in binaries.items()}
    scripts = {lane: {path.name: digest(path) for path in
                     sorted((binary.parent / "JavaScript").glob("*.native.js"))}
               for lane, binary in binaries.items()}
    pairs = []
    for index in range(args.pairs):
        directory = output / f"pair{index:02d}"
        receipt = directory / "pair.json"
        if receipt.exists():
            result = json.loads(receipt.read_text())
            if result["binarySha256"] != hashes or result.get("scriptSha256") != scripts:
                raise ValueError("Existing Minecraft pair has different binaries")
        else:
            order = ["old", "new"] if index % 2 == 0 else ["new", "old"]
            result = {"pair": index, "order": order, "binarySha256": hashes,
                      "scriptSha256": scripts}
            for lane in order:
                target = directory / lane
                command = [binaries[lane], "minecraft", "--headless", "--fixed-step",
                           "--warmup=120", "--measure=2000",
                           f"--capture-root={target / 'unused-captures'}",
                           f"--benchmark-output={target / 'frames.json'}"]
                process = run(command, target, clean_environment(), timeout=300)
                if process["exitCode"] != 0:
                    raise RuntimeError(f"Minecraft benchmark failed: {target}")
                result[lane] = metrics(target / "frames.json")
            write(receipt, result)
        pairs.append(result)
        if len(pairs) > 1:
            summary = {
                metric: bootstrap_ratios([p["new"][metric] for p in pairs],
                                         [p["old"][metric] for p in pairs])
                for metric in ("meanMs", "p95Ms")}
            summary["pairs"] = len(pairs)
            summary["verdict"] = ("no-regression" if all(
                summary[m]["ci95"][1] <= 1 for m in ("meanMs", "p95Ms")) else
                "regression" if any(summary[m]["ci95"][0] > 1 for m in ("meanMs", "p95Ms"))
                else "inconclusive")
            write(output / "summary.json", summary)
            print(json.dumps(summary), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old", type=Path, required=True)
    parser.add_argument("--new", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pairs", type=int, default=15)
    main(parser.parse_args())
