"""Collect executed receipts and report missing gates explicitly; never infer a pass."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

from prepare import SCENES


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig")) if path.is_file() else None


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ratio(summary, key="meanMs"):
    if not summary or key not in summary:
        return "N/A"
    metric = summary[key]
    return f"{metric['ratio']:.3f} [{metric['ci95'][0]:.3f}, {metric['ci95'][1]:.3f}]"


def ctest_summary(log):
    match = re.search(r"(\d+)% tests passed(?:, (\d+) tests failed)? out of (\d+)", log)
    if not match:
        return {"total": None, "failed": None}
    return {"total": int(match[3]),
            "failed": int(match[2]) if match[2] is not None else (0 if match[1] == "100" else None)}


def collect(build, evidence):
    repo = Path(__file__).resolve().parents[3]
    app = build / "build" / "Apps" / "LiteTests" / "LiteScenes"
    evidence.mkdir(parents=True, exist_ok=True)
    for path in build.glob("*.log"):
        destination = evidence / "logs" / path.name
        destination.parent.mkdir(exist_ok=True)
        shutil.copy2(path, destination)
    shutil.copy2(build / "initial-cache.cmake", evidence / "initial-cache.cmake")
    shutil.copy2(build / "baseline-identity.json", evidence / "baseline-identity.json")
    for name in ("scenes.json", "bundles.json"):
        shutil.copy2(app / "data" / name, evidence / name)
    for source, destination in (
        (app / "parity", evidence / "parity"),
        (build / "baseline-parity", evidence / "baseline-parity"),
        (build / "legacy-hosts-final", evidence / "legacy-hosts"),
    ):
        if source.is_dir():
            shutil.copytree(source, destination, dirs_exist_ok=True)
    gpu = evidence / "gpu"
    gpu.mkdir(exist_ok=True)
    for path in (build / "gpu").glob("*"):
        if path.suffix in (".json", ".rdc", ".xml"):
            shutil.copy2(path, gpu / path.name)

    changes = subprocess.run(
        ["git", "--no-pager", "diff", "--name-only"], cwd=repo, capture_output=True,
        text=True, check=True,
    ).stdout.splitlines()
    untracked = subprocess.run(
        ["git", "ls-files", "--others", "--exclude-standard", "--",
         "Apps/LiteTests/LiteScenes"], cwd=repo, capture_output=True, text=True, check=True,
    ).stdout.splitlines()
    source_files = sorted(set(changes + untracked + ["Core/LiteLayer/Include/babylon_lite.h"]))
    source_identity = {
        "head": subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo, capture_output=True,
                               text=True, check=True).stdout.strip(),
        "files": [{"path": name, "sha256": sha(repo / name)} for name in source_files
                  if (repo / name).is_file()],
        "binaries": [{"path": str(path), "sha256": sha(path)}
                     for path in sorted(app.iterdir()) if path.suffix in (".exe", ".dll")],
    }
    machine = read(evidence / "machine.json") or {}
    source_identity["measurementBase"] = machine.get("head")
    if source_identity["measurementBase"]:
        source_identity["committedDeltaSinceMeasurementBase"] = subprocess.run(
            ["git", "--no-pager", "diff", "--name-only",
             source_identity["measurementBase"], source_identity["head"]],
            cwd=repo, capture_output=True, text=True, check=True,
        ).stdout.splitlines()
    (evidence / "source-identity.json").write_text(json.dumps(source_identity, indent=2) + "\n")

    scenes = []
    for number in SCENES:
        entry = {"scene": number, "startupRatio": None, "milestoneAccepted": False}
        for language, folder in (("native", "Native"), ("js", "Js")):
            p = read(app / "parity" / folder / str(number) / "parity.json")
            run = read(app / "parity" / folder / str(number) / "run.json")
            entry[language] = {
                "parity": p,
                "setupFromMainMs": run.get("setupFromMainMs") if run else None,
                "shaderCompileMs": run.get("shaderCompileMs") if run else None,
                "drawCalls": run.get("drawCalls") if run else None,
            }
        state = read(build / "gpu" / f"scene{number}-state.json")
        entry["actualGpuDrawState"] = state
        entry["performance"] = {
            backend: {language: read(evidence / "perf" / f"{language}-{backend}"
                                     / f"scene{number}" / "summary.json")
                      for language in ("native", "js")}
            for backend in ("sdl_gpu", "dawn")
        }
        scenes.append(entry)
    regression_path = build / "regressions-final.log"
    regression_log = regression_path.read_text() if regression_path.is_file() else ""
    regressions = ctest_summary(regression_log)
    projection_path = build / "minecraft-projection-tests.log"
    projection_log = projection_path.read_text() if projection_path.is_file() else ""
    projection = ctest_summary(projection_log)
    legacy = read(build / "legacy-hosts-final" / "results.json") or []
    legacy_passed = sum(x["exitCode"] == 0 for x in legacy)
    golden_passed = sum(
        bool(entry[language]["parity"] and entry[language]["parity"]["pass"])
        for entry in scenes for language in ("native", "js"))
    baseline_results = [
        read(build / "baseline-parity" / backend / f"scene{number}" / "parity.json")
        for backend in ("sdl_gpu", "dawn") for number in SCENES]
    baseline_passed = sum(bool(x and x["pass"]) for x in baseline_results)
    actual_msaa = sum(
        bool(entry["actualGpuDrawState"] and entry["actualGpuDrawState"]["draws"] and all(
            draw["color"] and all(c and c["samples"] == 4 for c in draw["color"])
            and draw["depth"] and draw["depth"]["samples"] == 4
            for draw in entry["actualGpuDrawState"]["draws"]))
        for entry in scenes)
    summary = {
        "milestone": "M0", "complete": False, "publicAbiAdditions": [],
        "source": source_identity["measurementBase"], "workingTreeHead": source_identity["head"],
        "committedDeltaSinceMeasurementBase": source_identity.get("committedDeltaSinceMeasurementBase"),
        "scenes": scenes,
        "regressions": regressions, "legacyHostSmokes": legacy,
        "nativeCppMinecraftProjection": projection,
        "batchGoldenPasses": golden_passed, "baselineGoldenPasses": baseline_passed,
        "actual4xAttachmentScenes": actual_msaa,
        "remainingGates": [
            "Four native/JS golden failures: scenes200/249/256/257.",
            "Thirty projection entries lack native public contracts/features; four remain partial.",
            "Baseline loop excludes events; full-host comparison is not matched.",
            "Actual first-present/startup ratios, baseline GPU and latency are unavailable.",
            "Anisotropy and mip execution placement differ; numerical timings are diagnostic only.",
        ],
        "rawMetricLegend": {
            "completeHostWallMs": "Candidate event pump through bgfx::frame; CPU-wall time, not GPU completion latency.",
            "coreAndCallbacksMs": "Candidate native/projection frame call elapsed time, excluding the event pump.",
            "renderThreadCpuMs": "bgfx renderer elapsed CPU-timeline interval, including waits; NOT consumed thread CPU.",
            "processCpuMs": "GetProcessTimes kernel+user CPU consumption across the whole process.",
            "gpu": "Deduplicated real bgfx GPU frame queries; unavailable/repeated queries are null.",
            "setupFromMainMs": "Main entry through setup; NOT process launch to first present.",
            "firstGpuCompletedFrameFromMainMs": "First-frame GPU completion upper bound from main; NOT actual presentation time.",
        },
    }
    (evidence / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    lines = [
        "# API coverage batch 1 / M0 — partial, not accepted",
        "",
        f"Measured code base: BabylonNative `{source_identity['measurementBase']}`, with uncommitted changes.",
        f"Final working-tree HEAD: `{source_identity['head']}`; intervening committed paths: "
        + ", ".join(source_identity.get("committedDeltaSinceMeasurementBase", [])) + ".",
        "No commits, pushes, GitHub posts, public ABI additions or private model publication.",
        "",
        "## Executed correctness",
        "",
        f"Existing LiteTests: {regressions['total']} suites, {regressions['failed']} failures "
        f"(unknown if null). Legacy host smokes: {legacy_passed}/{len(legacy)} passed.",
        f"Native C++ Minecraft projection: {projection['total']} suites, {projection['failed']} failures "
        "(same post-sync bgfx child sources and qualified shader compiler archives).",
        f"Additional batch golden gates: {golden_passed}/{len(SCENES)*2} passed.",
        f"Fresh bblitec SDL_GPU/Dawn golden gates: {baseline_passed}/{len(SCENES)*2} passed; "
        "successful preflights verified runtime build stamps.",
        f"Actual draw-bound native BGRA8/D32F attachments: {actual_msaa}/{len(SCENES)} scenes verified 4x.",
        "",
        "| Scene | Native MAD full/foreground | JS MAD full/foreground | Native/JS gate |",
        "|---|---:|---:|---|",
    ]
    for entry in scenes:
        cells = []
        gates = []
        for language in ("native", "js"):
            p = entry[language]["parity"]
            cells.append(f"{p['fullMad']:.9g} / {p['foregroundMad']:.9g}" if p and "fullMad" in p else "N/A")
            gates.append("PASS" if p and p["pass"] else "FAIL" if p else "UNEXECUTED")
        lines.append(f"| {entry['scene']} | {cells[0]} | {cells[1]} | {' / '.join(gates)} |")
    lines += [
        "", "## Diagnostic frame ratios", "",
        "Geometric mean paired run ratios [95% paired bootstrap CI], candidate / bblitec.",
        "Warmup 120, measured 2000; five pairs, expanded to fifteen when close/inconclusive.",
        "These are NOT accepted speedups: baseline scope/GPU/latency/first-present gates are missing.",
        "Every startup ratio is **N/A**. Shader-service time is available only for the candidate.",
    ]
    for backend in ("sdl_gpu", "dawn"):
        lines += ["", f"### {backend}", "",
                  "| Scene | Native mean ratio | Native P95 ratio | JS mean ratio | JS P95 ratio |",
                  "|---|---:|---:|---:|---:|"]
        for entry in scenes:
            perf = entry["performance"][backend]
            lines.append(
                f"| {entry['scene']} | {ratio(perf['native'])} | {ratio(perf['native'], 'p95Ms')} | "
                f"{ratio(perf['js'])} | {ratio(perf['js'], 'p95Ms')} |"
            )
    lines += [
        "", "## Startup observations (not actual first-present times)", "",
        "| Scene | Native setup / shader ms | JS setup / shader ms | Startup ratio |",
        "|---|---:|---:|---|",
    ]
    for entry in scenes:
        values = []
        for language in ("native", "js"):
            run = entry[language]
            values.append(f"{run['setupFromMainMs']:.3f} / {run['shaderCompileMs']:.3f}"
                          if run["setupFromMainMs"] is not None else "N/A")
        lines.append(f"| {entry['scene']} | {values[0]} | {values[1]} | N/A |")
    lines += [
        "", "## Limits and reproduction", "",
        "See `source-review/projection-coverage.json` for the disposition of all 57 M0 entries.",
        "The native contract is unchanged. No fake arrays/properties stand in for missing Core features.",
        "Observed sampler anisotropy is 1 versus source requests up to 4. Native asset mips still",
        "use the pre-existing CPU generator instead of original GPU render passes; this is not fixed or hidden.",
        "Scene247 retains TWO instanced batches (958/941) and a plane. Scene260 expands the source strip",
        "to six list indices under the existing importer contract. Native D3D11 and baseline D3D12",
        "compiler/queue differences and bblitec offline baked work remain explicit confounds.",
        "",
        "The strict gate remains upper paired mean AND P95 CIs <= 1.0, with parity/work/GPU/latency qualification.",
        "A diagnostic faster ratio does not close M0. Failed pixels have not been attributed to anisotropy alone.",
        "",
        "Exact executed configure settings are in `initial-cache.cmake`; commands, failures and style outputs",
        "are in `logs`. `parity`, `baseline-parity`, `gpu`, `legacy-hosts`, and `perf` contain actual receipts.",
        "Use `Apps\\LiteTests\\LiteScenes\\README.md` for the precise build, test and four-lane runner commands.",
        "Pre-sync evidence is separately labelled and is not mixed into final ratios.",
        "Superseded pre-listener-retention JS pairs are retained separately and excluded.",
        "",
    ]
    (evidence / "SUMMARY.md").write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True, help="Parent of the fresh build directory")
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    collect(args.build.resolve(), args.evidence.resolve())
