"""Paired observer on/off controls, retaining the same event-inclusive frame brackets."""

import argparse
import json
from pathlib import Path

import numpy as np

import harness as h
from formal_benchmark import baseline_metrics


def main(args):
    root = args.output.resolve()
    overlay = json.loads(args.manifest.read_text())[0]
    record = h.catalog(args.data, 159)
    for language in ("Native", "Js", "sdl_gpu", "dawn"):
        pairs = []
        for index in range(15):
            pair = {}
            for enabled in ([False, True] if index % 2 == 0 else [True, False]):
                mode = "on" if enabled else "off"
                output = root / language / f"pair{index:02d}" / mode
                if language in ("Native", "Js"):
                    executable = args.hosts.resolve() / f"LiteScenes{language}.exe"
                    command = [executable, "--scene=159", f"--data={args.data.resolve()}",
                               f"--out={output}", "--warmup=120", "--measure=2000"]
                    if enabled:
                        command.append("--probe-present")
                    env = h.clean_environment()
                    cwd = None
                else:
                    executable = Path(overlay["executable"])
                    command = [executable]
                    env = h.clean_environment(record["pose"]["nativeEnvironment"])
                    env.update({
                        "BBLITE_GPU_BACKEND": language, "BBLITE_BENCHMARK_FRAMES": "2000",
                        "BBLITE_TEST_PASS": "1", "BBLITE_FRAME_DELTA_MS": "16.666666666666668",
                        "BBLITE_ASSET_DIR": overlay["assets"],
                        "BBLITE_GPU_SHADER_DIR": overlay["shaders"],
                        "LITE_M0_FRAME_OUT": str(output / "frames.json"),
                    })
                    if enabled:
                        env["LITE_M0_PRESENT_OUT"] = str(output / "present.json")
                    else:
                        env["LITE_M0_SKIP_PROBE"] = "1"
                    cwd = executable.parent
                process = h.run(command, output, env, cwd)
                if process["exitCode"] != 0:
                    raise RuntimeError(f"Observer control failed: {output}")
                metrics = (h.native_metrics(output) if language in ("Native", "Js")
                           else baseline_metrics(output))
                if language in ("Native", "Js"):
                    samples = json.loads((output / "run.json").read_text())["samples"]
                    metrics["coreMs"] = float(np.mean([x["coreAndCallbacksMs"] for x in samples]))
                pair[mode] = metrics
            h.write(root / language / f"pair{index:02d}" / "pair.json", pair)
            pairs.append(pair)
        summary = {
            "pairs": 15, "scene": 159, "scope": "observer-on/off; same complete-host envelope",
            "binarySha256": h.digest(executable),
            "meanMs": h.bootstrap_ratios([x["on"]["meanMs"] for x in pairs],
                                         [x["off"]["meanMs"] for x in pairs]),
            "p95Ms": h.bootstrap_ratios([x["on"]["p95Ms"] for x in pairs],
                                        [x["off"]["p95Ms"] for x in pairs]),
        }
        if language in ("Native", "Js"):
            summary["coreMs"] = h.bootstrap_ratios([x["on"]["coreMs"] for x in pairs],
                                                   [x["off"]["coreMs"] for x in pairs])
        h.write(root / language / "summary.json", summary)
        print(language, json.dumps(summary), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hosts", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    main(parser.parse_args())
