"""Persist continuation evidence without replacing the committed batch's historical receipts."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess

import numpy as np
from PIL import Image

import harness as h


def collect(build, evidence):
    destination = evidence / "continuation-v3"
    destination.mkdir(parents=True, exist_ok=True)
    hosts = build / "build" / "Apps" / "LiteTests" / "LiteScenes"
    catalog = json.loads((hosts / "data" / "scenes.json").read_text())
    scenes = []
    for record in catalog:
        scene = record["id"]
        row = {"scene": scene}
        for language in ("Native", "Js"):
            parity = json.loads((hosts / "parity" / language / str(scene) / "parity.json").read_text())
            lanes = {}
            for backend in ("sdl_gpu", "dawn"):
                path = build / "formal-perf-v2" / language / backend / str(scene) / "summary.json"
                value = json.loads(path.read_text())
                if value["pairs"] != 15:
                    raise ValueError(f"Incomplete formal lane: {path}")
                if parity["hostSha256"] != h.digest(hosts / f"LiteScenes{language}.exe"):
                    raise ValueError("Stale parity binary")
                if value["verdict"] == "unqualified-gpu-latency-overhead":
                    value["verdict"] = "unqualified-gpu-latency-first-correct-frame"
                lanes[backend] = value
            row[language] = {
                "parity": {k: parity[k] for k in ("pass", "fullMad", "foregroundMad", "hostSha256")},
                "perf": lanes,
            }
        scenes.append(row)
    controls = []
    for scene, variant in ((200, "xfirst"), (249, "aniso4"), (257, "aniso4"), (256, "gpu-mips")):
        record = next(x for x in catalog if x["id"] == scene)
        path = build / "replay-controls" / f"{scene}-{variant}.png"
        actual = np.asarray(Image.open(path).convert("RGB"))
        reference = np.asarray(Image.open(record["golden"]).convert("RGB"))
        config = record["parity"]
        metrics = h.compare_rgb(actual, reference, config["backgroundColor"], config["backgroundThreshold"])
        controls.append({"scene": scene, "control": variant, **metrics,
                         "pass": metrics["fullMad"] <= config["maxFullMad"] and
                         metrics["foregroundMad"] <= config["maxForegroundMad"],
                         "imageSha256": h.digest(path)})
    changed = subprocess.run(["git", "--no-pager", "diff", "--name-only"],
                             check=True, capture_output=True, text=True).stdout.splitlines()
    changed += subprocess.run(["git", "ls-files", "--others", "--exclude-standard"],
                              check=True, capture_output=True, text=True).stdout.splitlines()
    summary = {
        "baseCommit": "5680aaed", "publicAbiAdditions": [], "changedFiles": changed,
        "scenes": scenes, "controlledReplay": controls,
        "qualification": "M0 remains open: three scene parity failures, five ABI-query approvals, "
                         "supported bgfx-option approval, live baseline GPU/latency and exact "
                         "first-correct-frame association. Observer overhead is characterized.",
        "measurement": {
            "pairs": 840, "processes": 1680, "warmup": 120, "measured": 2000,
            "startup": "warm OS/driver caches; actual Present completion, plus CPU-source-readiness tag",
            "excluded": ["original370pairs", "formal-perf invalid native startup-marker smoke",
                         "RenderDoc capture/replay counters"],
        },
        "stagingOptionExperiment": {
            "status": "opt-in isolated build; default unchanged; approval required",
            "definition": "BGFX_CONFIG_RENDERER_DIRECT3D11_USE_STAGING_BUFFER=1",
            "pairs": 60,
            "scenes": {
                str(scene): json.loads((build / "staging-perf" / "Native" / "sdl_gpu" /
                                        str(scene) / "summary.json").read_text())
                for scene in (200, 247, 256, 258)},
            "fullNativeJsParityPasses": 22, "parityGates": 28,
            "graphicsTestsPassed": 24,
            "limitation": "Not enabled for the default build; full batch perf/Minecraft "
                          "qualification under this global backend option is pending.",
        },
        "observerOverhead": {
            lane: json.loads((build / "observer-control" / "perf" / lane / "summary.json").read_text())
            for lane in ("Native", "Js", "sdl_gpu", "dawn")},
        "sourceSha256": {path: h.digest(Path(path)) for path in changed},
        "configurationSha256": h.digest(build / "build" / "CMakeCache.txt"),
    }
    for directory in ("formal-perf-v2", "baseline-probe-preflight", "legacy-hosts-final", "evidence"):
        shutil.copytree(build / directory, destination / directory, dirs_exist_ok=True)
    shutil.copytree(build / "staging-perf", destination / "staging-perf", dirs_exist_ok=True)
    shutil.copytree(build / "observer-control" / "perf", destination / "observer-overhead", dirs_exist_ok=True)
    shutil.copy2(build / "staging-experiment" / "manifest.json", destination / "staging-manifest.json")
    shutil.copy2(build / "observer-control" / "PresentProbeControlled.cpp",
                 destination / "PresentProbeControlled.cpp")
    for path in build.glob("*.log"):
        shutil.copy2(path, destination / path.name)
    shutil.copy2(build / "initial-cache.cmake", destination / "initial-cache.cmake")
    shutil.copy2(build / "evidence" / "commands.ps1", destination / "commands.ps1")
    shutil.copy2(build / "baseline-probe-final" / "manifest.json", destination / "baseline-overlay-manifest.json")
    shutil.copy2(hosts / "data" / "bundles.json", destination / "bundles.json")
    shutil.copy2(hosts / "data" / "scenes.json", destination / "scenes.json")
    shutil.copytree(hosts / "parity", destination / "parity", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("*.bgra"))
    for directory in ("gpu-analysis", "replay-controls", "mip-analysis", "original125"):
        output = destination / directory
        output.mkdir(exist_ok=True)
        for pattern in ("*.json", "*.py", "*.png", "*.hlsl", "*.asm", "*.diff", "*.ts"):
            for path in (build / directory).glob(pattern):
                shutil.copy2(path, output / path.name)
    shutil.copytree(build / "original125", destination / "original125", dirs_exist_ok=True)
    captures = []
    for path in (build / "replay-controls").glob("*.rdc"):
        captures.append({"localPath": str(path), "sha256": h.digest(path)})
    h.write(destination / "local-captures.json", captures)
    h.write(destination / "summary.json", summary)

    def ratio(value):
        return f"{value['ratio']:.4f} [{value['ci95'][0]:.4f},{value['ci95'][1]:.4f}]"

    def gate(value):
        return f"{'P' if value['pass'] else 'F'} {value['fullMad']:.7g}/{value['foregroundMad']:.7g}"

    lines = [
        "## Continuation after 5680aaed — v3 executed evidence",
        "",
        "Historical v2 evidence above is preserved. No public ABI additions. Changes remain uncommitted.",
        "",
        "22/28 scene gates pass; 200/249/257 still fail native and JS. Scene256 is fixed.",
        "All 24 non-LiteScenes suites, 24 graphics tests, 10 native Minecraft suites and six legacy smokes pass.",
        "All28 isolated-baseline golden preflights pass. Core LLVM22 format/AST and host style checks pass.",
        "",
        "840 balanced run pairs / 1,680 processes: fifteen per scene/language/backend;120 warmup +2,000 measured.",
        "New complete-host brackets include events; actual Present receipts include a CPU source-readiness tag.",
        "Baseline PSO time is not separated; live baseline GPU/latency and exact correct-frame association remain unavailable.",
        "Observer overhead:60 additional paired controls, all four mean/P95 CIs include1; no zero-cost claim.",
        "**Numerical throughput passes are not full performance acceptance.** No thresholds/workloads were relaxed.",
        "",
        "Full/foreground MAD is in0..255 units. Ratios are paired geometric mean with95% bootstrap CI.",
        "Table startup is process creation→first post-CPU-readiness DXGI Present completion, not display/photon time.",
        "Actual first-any-Present is also recorded. Exact GPU source-frame/Present association remains unqualified.",
        "For failed golden gates the source frame is not a qualified correct frame.",
        "",
        "|Scene|Native MAD|JS MAD|Native/SDL startup ms|Native startup ratio [CI]|Native mean ratio [CI]|Native P95 ratio [CI]|Native verdict|JS/SDL startup ms|JS startup ratio [CI]|JS mean ratio [CI]|JS P95 ratio [CI]|JS verdict|",
        "|---|---|---|---:|---|---|---|---|---:|---|---|---|---|",
    ]
    for row in scenes:
        n = row["Native"]["perf"]["sdl_gpu"]
        j = row["Js"]["perf"]["sdl_gpu"]
        lines.append(
            f"|{row['scene']}|{gate(row['Native']['parity'])}|{gate(row['Js']['parity'])}|"
            f"{n['native']['firstSourcePresentMs']:.1f}/{n['baseline']['firstSourcePresentMs']:.1f}|"
            f"{ratio(n['firstSourcePresentMs'])}|{ratio(n['meanMs'])}|{ratio(n['p95Ms'])}|"
            f"{n['timingVerdict']}; {n['verdict']}|"
            f"{j['native']['firstSourcePresentMs']:.1f}/{j['baseline']['firstSourcePresentMs']:.1f}|"
            f"{ratio(j['firstSourcePresentMs'])}|{ratio(j['meanMs'])}|{ratio(j['p95Ms'])}|"
            f"{j['timingVerdict']}; {j['verdict']}|")
    lines += ["", "### Isolated supported-option performance fix (not the default build)", "",
              "Changing only captured vertex/index buffer usage from DYNAMIC/CPU-write to DEFAULT/no-CPU-access "
              "preserves0/0 pixels and gives GPU replay ratio0.9236 [0.9058,0.9403]. Physical heap residency "
              "was not measured. No vendor source was edited.",
              "",
              "The existing bgfx staging option was then compiled in isolation. All28 native/JS gates retain "
              "the same22 passes, and all24 graphics tests pass. Sixty additional matched Native/SDL pairs:",
              "",
              "|Scene|Mean ratio [CI]|P95 ratio [CI]|Timing verdict|",
              "|---|---|---|---|"]
    for scene, value in summary["stagingOptionExperiment"]["scenes"].items():
        lines.append(f"|{scene}|{ratio(value['meanMs'])}|{ratio(value['p95Ms'])}|{value['timingVerdict']}|")
    lines += ["", "This global backend build option needs parent review and complete-batch/Minecraft qualification; "
              "it is not silently enabled or substituted for the default840 pairs."]
    lines += [
        "", "All native/JS Dawn values, raw samples, startup/shader spans and identities are in `summary.json`/`formal-perf-v2`.",
        "", "### Approval/blocker handoff",
        "",
        "*200: controlled X-first fused VS accumulation gives0/0. Backend arithmetic order, not a version delta; no production shader adaptation.",
        "*249/257: authored2D max-anisotropy4 controls pass; exact bgfx sampler integration is policy-deferred and needs approval.",
        "*256: production GPU mip correction passes. Private view-layout adaptation keeps scene views high and mip prelude/environment views disjoint; review this embedding change.",
        "*Five proposed C99 inspection queries remain unimplemented pending ABI review; see `evidence/projection-milestones.json` for all34 milestone mappings.",
        "*Scene247: original source in both pins is plane-first, unlike baked baseline. Fifteen balanced GPU-replay pairs are separate diagnostics, not live timing.",
        "*No live baseline GPU/latency or exact first-correct-frame association: all complete acceptance remains blocked or rejected.",
        "", "### Changed BabylonNative files", "",
    ]
    lines += [f"* `{path}`" for path in changed]
    lines += ["", "Reproduce with `continuation-v3/commands.ps1`; no commands edit the bblitec checkout.", ""]
    text = "\n".join(lines)
    (destination / "SUMMARY.md").write_text(text, encoding="utf-8")
    readme = Path(__file__).with_name("README.md")
    coverage_marker = "## Continuation v3 executed coverage"
    if coverage_marker not in readme.read_text(encoding="utf-8"):
        table_start = text.index("|Scene|Native MAD|")
        table_end = text.index("### Approval/blocker handoff")
        with readme.open("a", encoding="utf-8") as file:
            file.write("\n\n" + coverage_marker + "\n\n")
            file.write("Startup tags are not exact GPU correct-frame association. All full acceptance "
                       "remains unqualified; the staging option is isolated, not the default.\n\n")
            file.write(text[table_start:table_end].rstrip() + "\n")
    main = evidence / "SUMMARY.md"
    if "## Continuation after 5680aaed — v3 executed evidence" not in main.read_text(encoding="utf-8"):
        with main.open("a", encoding="utf-8") as file:
            file.write("\n\n" + text)
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    collect(args.build.resolve(), args.evidence.resolve())
