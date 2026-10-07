"""Derive reproducible run-level tables from completed formal receipts."""
import argparse
import importlib.util
import json
from pathlib import Path
import statistics


def median(values):
    return statistics.median(values)


def mean(values):
    return statistics.mean(values)


def percent(value):
    return f"{value:+.2f}%"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-root", type=Path, required=True)
    parser.add_argument("--parent-harness", type=Path, required=True)
    parser.add_argument("--update-report", action="store_true")
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("benchmark_math", args.parent_harness)
    parent = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(parent)
    build = args.native_root / "build/lite-c99/perf-deep-dive"
    primary = json.loads((build / "Matched15/summary.json").read_text())
    renderer = json.loads((build / "Renderer5/summary.json").read_text())
    records = {}
    groups = {}
    for folder in ("Matched15", "Renderer5"):
        runs = json.loads((build / folder / "measurements.json").read_text())
        for run in runs:
            groups.setdefault((folder, run["name"]), []).append(run)
            if "performanceReceipt" in run:
                records.setdefault((folder, run["name"]), []).append(
                    json.loads(Path(run["performanceReceipt"]).read_text()))
    footprints = {}
    for (folder, name), runs in groups.items():
        native = [r for r in runs if r["backend"] == "bgfx D3D11"]
        if not native:
            continue
        for run in native:
            file = build / folder / f"{run['repetition']:02d}-{name}.receipt.json"
            receipt = json.loads(file.read_text())
            footprint = {key: receipt[key] for key in (
                "worldHash", "initialWorldHash", "cameraPosition", "cameraTarget", "timeOfDay",
                "chunks", "mobs", "draws", "materialCount", "drawSamples")}
            if name in footprints and footprints[name] != footprint:
                raise ValueError(f"{name}: repeated workload changed")
            footprints[name] = footprint
    reference = footprints["native-phases"]
    for name, footprint in footprints.items():
        if footprint != reference:
            raise ValueError(f"{name}: native workload changed relative to full control")
    derived = {
        "nativeAllFormalWorkloadFootprintsMatch": True,
        "nativeFrameCount": len(reference["drawSamples"]),
        "footprintWithoutDrawArray": {k: v for k, v in reference.items() if k != "drawSamples"},
        "exclusiveLedgerMedianRunMeansMs": {}, "threadCpuMedianRunMeansMs": {},
        "gc": {}, "gpu": {}, "p95": {},
        "hudOfNoncorePercent": {},
    }
    for (folder, name), receipts in records.items():
        label = f"{folder}/{name}"
        thread_means = {}
        for receipt in receipts:
            frame_count = 1200
            thread_groups = {"main": receipt["mainThreadCpuMs"] / frame_count}
            for thread in receipt["threads"]:
                if thread["id"] == receipt["mainThreadId"]:
                    continue
                key = thread["name"] or thread["startModule"] or "unnamed-unidentified"
                thread_groups[key] = thread_groups.get(key, 0) + thread["cpuMs"] / frame_count
            thread_groups["process"] = receipt["processCpuMs"] / frame_count
            thread_groups["endpointThreadSum"] = sum(t["cpuMs"] for t in receipt["threads"]) / frame_count
            for key, value in thread_groups.items():
                thread_means.setdefault(key, []).append(value)
        derived["threadCpuMedianRunMeansMs"][label] = {
            key: median(values) for key, values in thread_means.items()}
        if not receipts[0]["phases"]["total"]:
            continue
        outer = (["platform", "audio", "blFrame", "bgfxFrame", "hud", "gc"]
                 if receipts[0]["host"] == "native" else
                 ["acquire", "callbacks", "rmlUpdate", "synchronize", "encode", "submit", "finish"])
        ledger = {
            phase: median(mean(r["phases"][phase]) for r in receipts)
            for phase in outer}
        ledger["unaccounted"] = median(
            mean(r["phases"]["total"]) - sum(mean(r["phases"][phase]) for phase in outer)
            for r in receipts)
        ledger["total"] = median(mean(r["phases"]["total"]) for r in receipts)
        if receipts[0]["host"] == "native":
            ledger["blFrameMinusCallbacks"] = median(
                mean(r["phases"]["blFrame"]) - mean(r["phases"]["callbacks"]) for r in receipts)
            derived["hudOfNoncorePercent"][label] = median(
                100 * mean(r["phases"]["hud"]) /
                (mean(r["phases"]["total"]) - mean(r["phases"]["blFrame"]) -
                 mean(r["phases"]["bgfxFrame"])) for r in receipts)
        derived["exclusiveLedgerMedianRunMeansMs"][label] = ledger
        collection_means = []
        for receipt in receipts:
            actual = [value for value, count in zip(
                receipt["phases"]["gc"], receipt["counts"]["gcRuns"], strict=True) if count]
            if actual:
                collection_means.append(mean(actual))
        derived["gc"][label] = {
            "collectionsPerRunMedian": median(sum(r["counts"]["gcRuns"]) for r in receipts),
            "collectedNodesPerRunMedian": median(sum(r["counts"]["gcCollected"]) for r in receipts),
            "tracedNodesPerCollectionMedian": median(
                sum(r["counts"]["gcTracedNodes"]) / max(1, sum(r["counts"]["gcRuns"]))
                for r in receipts),
            "rootsPerCollectionMedian": median(
                sum(r["counts"]["gcRoots"]) / max(1, sum(r["counts"]["gcRuns"])) for r in receipts),
            "edgesPerCollectionMedian": median(
                sum(r["counts"]["gcEdges"]) / max(1, sum(r["counts"]["gcRuns"])) for r in receipts),
            "meanCollectionMsMedian": median(collection_means) if collection_means else None,
            "nodesBeginMedian": median(r["counts"]["nodesBefore"][0] for r in receipts),
            "nodesEndMedian": median(r["counts"]["nodesAfter"][-1] for r in receipts),
        }
    for (folder, name), runs in groups.items():
        label = f"{folder}/{name}"
        derived["p95"][label] = median(r["p95Ms"] for r in runs)
        queries = [r["gpu"] for r in runs if "gpu" in r]
        if queries:
            averages = [r["summary"]["averageMs"] for r in queries if r["summary"]]
            derived["gpu"][label] = {
                "distinctQueryFrames": sum(r["uniqueFrameQueries"] for r in queries),
                "repeatedIds": sum(r["repeatedFrameIds"] for r in queries),
                "unavailable": sum(r["unavailable"] for r in queries),
                "medianRunAverageMs": median(averages) if averages else None,
            }
    phase_comparisons = {}
    for before, after, phase in (
            ("native-phases", "native-grouping", "grouping"),
            ("native-phases", "native-grouping", "blFrame"),
            ("native-phases", "native-regex", "hudElements"),
            ("native-phases", "native-regex", "hud"),
            ("native-phases", "native-hud-retained", "hud"),
            ("native-phases", "native-gc-off", "gc"),
            ("sdl-phases", "native-phases", "callbacks")):
        a, b = records[("Matched15", before)], records[("Matched15", after)]
        phase_comparisons[f"{before} -> {after}: {phase}"] = parent.paired_comparison(
            [{"averageMs": mean(r["phases"][phase])} for r in a],
            [{"averageMs": mean(r["phases"][phase])} for r in b])
    derived["phaseComparisons"] = phase_comparisons
    derived["nativeCoreInstrumentationControls"] = {}
    for before, after in (("native-qualified", "native-off"), ("native-off", "native-phases")):
        a = groups[("Matched15", before)]
        b = groups[("Matched15", after)]
        derived["nativeCoreInstrumentationControls"][f"{before} -> {after}"] = (
            parent.paired_comparison(
                [{"averageMs": r["coreCpu"]["averageMs"]} for r in a],
                [{"averageMs": r["coreCpu"]["averageMs"]} for r in b]))
    (build / "derived.json").write_text(json.dumps(derived, indent=2))

    lines = [
        "**Executed:** 15 balanced primary rounds × 13 variants = 195 processes; "
        "five balanced renderer rounds × seven variants = 35 processes. "
        "All use 180/1,200 frames. Every native formal run has identical world/camera/"
        "time/mob/material and per-frame draw-count footprints.",
        "",
        "### Fresh full-host and ablation comparisons",
        "",
        "| Primary variant | Median run-average ms | Median run P95 ms | Scope |",
        "|---|---:|---:|---|",
    ]
    for name, value in primary["mediansOfRunAveragesMs"].items():
        scope = ("**NONPARITY** diagnostic" if name in ("native-hud-off", "native-gc-off") else
                 "Allocation-counter overhead" if "allocations" in name else
                 "Qualified unchanged executable" if "qualified" in name else
                 "Opt-in isolated diagnostic")
        lines.append(f"| {name} | {value:.6f} | {derived['p95'][f'Matched15/{name}']:.6f} | {scope} |")
    lines.extend(["", "Changes are mean paired per-run changes; CIs bootstrap the 15 run pairs, "
                  "not individual frames.", "",
                  "| Control → candidate | Change | Bootstrap 95% CI | 5% classification |",
                  "|---|---:|---:|---|"])
    for key, value in primary["comparisons"].items():
        ci = value["bootstrap95Percent"]
        lines.append(f"| {key} | {percent(value['meanPairedChangePercent'])} | "
                     f"[{percent(ci[0])}, {percent(ci[1])}] | {value['classification']} |")
    lines.extend(["", "Native core-bracket overhead is checked separately because "
                  "HUD-heavy totals can hide instrumentation cost:", "",
                  "| Native variant | Median original core-bracket ms/frame |",
                  "|---|---:|"])
    for name in ("native-qualified", "native-off", "native-phases"):
        value = median(r["coreCpu"]["averageMs"] for r in groups[("Matched15", name)])
        lines.append(f"| {name} | {value:.6f} |")
    lines.extend(["", "| Core instrumentation control | Paired change | 95% CI |",
                  "|---|---:|---:|"])
    for key, value in derived["nativeCoreInstrumentationControls"].items():
        ci = value["bootstrap95Percent"]
        lines.append(f"| {key} | {percent(value['meanPairedChangePercent'])} | "
                     f"[{percent(ci[0])}, {percent(ci[1])}] |")
    lines.extend(["", "### Native versus SDL phase ledger", "",
                  "Median of each run's mean, milliseconds/frame. Parentheses denote nested "
                  "spans; do not sum them into outer totals.", "",
                  "| Span | Native full | SDL full |",
                  "|---|---:|---:|"])
    n = primary["phaseMedianRunMeansMs"]["native-phases"]
    s = primary["phaseMedianRunMeansMs"]["sdl-phases"]
    native_ledger = derived["exclusiveLedgerMedianRunMeansMs"]["Matched15/native-phases"]
    sdl_ledger = derived["exclusiveLedgerMedianRunMeansMs"]["Matched15/sdl-phases"]
    rows = [
        ("Platform timers", n["platform"], None), ("Audio poll", n["audio"], None),
        ("bl_frame (includes callbacks)", n["blFrame"], None),
        ("(Original callbacks / camera update)", n["callbacks"], s["callbacks"]),
        ("(Native bl_frame minus callbacks)", native_ledger["blFrameMinusCallbacks"], None),
        ("(Scene setup)", n["sceneSetup"], None), ("(Member/world visit)", n["meshVisit"], None),
        ("(Material grouping)", n["grouping"], None), ("(Sort)", n["sorting"], None),
        ("(Material/uniform/bgfx enqueue)", n["drawEnqueue"], None),
        ("bgfx::frame handoff/wait", n["bgfxFrame"], None),
        ("Native HUD backing", n["hud"], None),
        ("(HUD setup/DIB clear)", n["hudSetup"], None),
        ("(HUD elements/text/icons/regex)", n["hudElements"], None),
        ("(HUD alpha scan)", n["hudAlpha"], None),
        ("(HUD backing memcpy)", n["hudCopy"], None),
        ("(HUD visible presentation check)", n["hudPresent"], None),
        ("(HUD DC/DIB disposal)", n["hudDispose"], None),
        ("User-language GC", n["gc"], s["gc"]),
        ("SDL acquire", None, s["acquire"]),
        ("SDL RmlUI layout/projection", None, s["rmlUpdate"]),
        ("SDL synchronize/plan/upload", None, s["synchronize"]),
        ("SDL encode (includes UI frame recording)", None, s["encode"]),
        ("SDL present (includes UI and driver submit)", None, s["submit"]),
        ("(SDL RmlUI GPU command recording)", None, s["rmlRender"]),
        ("(SDL actual driver command submission)", None, s["driverSubmit"]),
        ("SDL finish (includes GC/timers)", None, s["finish"]),
        ("Unaccounted original-total remainder", native_ledger["unaccounted"], sdl_ledger["unaccounted"]),
        ("Original total", n["total"], s["total"]),
    ]
    for name, native_value, sdl_value in rows:
        lines.append(f"| {name} | "
                     f"{f'{native_value:.6f}' if native_value is not None else '—'} | "
                     f"{f'{sdl_value:.6f}' if sdl_value is not None else '—'} |")
    lines.extend(["", "### Ranked native bottlenecks", "",
                  "| Rank | Owner / cost | ms/frame | % of native original total | Certainty |",
                  "|---:|---|---:|---:|---|"])
    ranked = [
        ("HUD backing rebuild", n["hud"], "Direct outer/subphase timing + removal/retention controls"),
        ("Engine material grouping", n["grouping"], "Direct phase + same-frame reuse control"),
        ("Material/uniform/bgfx draw enqueue", n["drawEnqueue"], "Direct aggregate; backend execution is elsewhere"),
        ("Generated application callbacks", n["callbacks"], "Direct callback boundary; includes forwarded setters"),
        ("Engine sorting", n["sorting"], "Direct phase; no sort-elimination control"),
        ("Member visitation/matrix evaluation", n["meshVisit"], "Direct phase + world call/rebuild counts"),
        ("bgfx frame handoff/backpressure", n["bgfxFrame"], "Direct phase; changes after HUD retention"),
        ("Platform timer dispatch", n["platform"], "Direct phase + callback/pending counts"),
        ("User cycle collector", n["gc"], "Direct phase + GC-off null/control + graph counts"),
    ]
    for rank, (name, value, certainty) in enumerate(sorted(ranked, key=lambda x: -x[1]), 1):
        lines.append(f"| {rank} | {name} | {value:.6f} | {100 * value / n['total']:.3f}% | {certainty} |")
    lines.extend(["", "The HUD is {:.2f}% of measured non-core time. It is not merely "
                  "desktop presentation: the hidden-window visibility/presentation check is "
                  "{:.6f} ms/frame. User GC is {:.6f} ms/frame, not the multi-millisecond "
                  "remainder.".format(derived["hudOfNoncorePercent"]["Matched15/native-phases"],
                                      n["hudPresent"], n["gc"]), "",
                  "### Phase-level controlled changes", "",
                  "| Control → candidate / phase | Change | 95% CI |",
                  "|---|---:|---:|"])
    for key, value in phase_comparisons.items():
        ci = value["bootstrap95Percent"]
        lines.append(f"| {key} | {percent(value['meanPairedChangePercent'])} | "
                     f"[{percent(ci[0])}, {percent(ci[1])}] |")
    lines.extend(["", "### Execution, renderer waits and GPU", "",
                  "| Primary variant | Total QPC ms | Whole-process CPU ms/frame | "
                  "Main execution ms/frame | Renderer execution ms/frame | "
                  "bgfx renderer-submit wall ms | waitRender ms | waitSubmit ms |",
                  "|---|---:|---:|---:|---:|---:|---:|---:|"])
    for name in ("sdl-phases", "native-phases", "native-hud-off", "native-hud-retained",
                 "native-regex", "native-grouping"):
        phase = primary["phaseMedianRunMeansMs"][name]
        threads = derived["threadCpuMedianRunMeansMs"][f"Matched15/{name}"]
        lines.append(f"| {name} | {phase['total']:.6f} | {threads['process']:.6f} | "
                     f"{threads['main']:.6f} | "
                     f"{f'{threads.get('bgfx - renderer backend thread', 0):.6f}' if name.startswith('native') else '—'} | "
                     f"{f'{phase.get('renderThreadWall', 0):.6f}' if name.startswith('native') else '—'} | "
                     f"{f'{phase.get('waitRender', 0):.6f}' if name.startswith('native') else '—'} | "
                     f"{f'{phase.get('waitSubmit', 0):.6f}' if name.startswith('native') else '—'} |")
    lines.extend(["", "Non-main CPU by endpoint thread start module/name "
                  "(zero groups omitted; raw receipts retain every thread):", "",
                  "| Variant | Other thread/module | CPU ms/frame |",
                  "|---|---|---:|"])
    for name in ("sdl-phases", "native-phases", "native-hud-retained"):
        threads = derived["threadCpuMedianRunMeansMs"][f"Matched15/{name}"]
        for key, value in threads.items():
            if key not in ("main", "process", "endpointThreadSum",
                           "bgfx - renderer backend thread") and value:
                lines.append(f"| {name} | {key} | {value:.6f} |")
    lines.extend(["", "| Native variant | GPU median run average ms | Distinct frame queries | "
                  "Repeated IDs | Unavailable |",
                  "|---|---:|---:|---:|---:|"])
    for name in ("native-phases", "native-hud-off", "native-hud-retained", "native-grouping"):
        gpu = derived["gpu"][f"Matched15/{name}"]
        lines.append(f"| {name} | {gpu['medianRunAverageMs']:.6f} | "
                     f"{gpu['distinctQueryFrames']} | {gpu['repeatedIds']} | {gpu['unavailable']} |")
    lines.extend(["", "### Renderer diagnostic batch (five run pairs)", "",
                  "| Variant | Median full-host ms/frame | Process CPU ms/frame | GPU query availability |",
                  "|---|---:|---:|---|"])
    for name, value in renderer["mediansOfRunAveragesMs"].items():
        gpu = derived["gpu"][f"Renderer5/{name}"]
        lines.append(f"| {name} | {value:.6f} | "
                     f"{renderer['processCpuMsPerFrame'][name]:.6f} | "
                     f"{gpu['distinctQueryFrames']} distinct; {gpu['unavailable']} unavailable |")
    lines.extend(["", "| Control → renderer candidate | Paired change | 95% CI |",
                  "|---|---:|---:|"])
    for key, value in renderer["comparisons"].items():
        ci = value["bootstrap95Percent"]
        lines.append(f"| {key} | {percent(value['meanPairedChangePercent'])} | "
                     f"[{percent(ci[0])}, {percent(ci[1])}] |")
    lines.extend(["", "### GC, allocation and scheduling observations", "",
                  "| Variant | Live nodes first → last | Collections/run | Collected nodes/run | "
                  "Roots / owning edges per collection | Mean actual collection ms | "
                  "Process private-byte growth |",
                  "|---|---:|---:|---:|---:|---:|---:|"])
    for name in ("sdl-phases", "native-phases", "native-gc-off", "native-hud-retained"):
        gc_data = derived["gc"][f"Matched15/{name}"]
        cost = gc_data["meanCollectionMsMedian"]
        lines.append(f"| {name} | {gc_data['nodesBeginMedian']:g} → {gc_data['nodesEndMedian']:g} | "
                     f"{gc_data['collectionsPerRunMedian']:g} | "
                     f"{gc_data['collectedNodesPerRunMedian']:g} | "
                     f"{gc_data['rootsPerCollectionMedian']:g} / {gc_data['edgesPerCollectionMedian']:g} | "
                     f"{f'{cost:.6f}' if cost is not None else 'disabled'} | "
                     f"{primary['privateByteGrowthMedian'][name]:g} B |")
    lines.extend(["", "| Allocation-counting variant | C++ new calls/frame | Requested bytes/frame | "
                  "C99 host allocations/frame | C99 requested bytes/frame |",
                  "|---|---:|---:|---:|---:|"])
    for name in ("sdl-allocations", "native-allocations"):
        count = primary["counterMedianRunMeans"][name]
        lines.append(f"| {name} | {count['newCount']:.3f} | {count['newBytes']:.1f} | "
                     f"{count['coreAllocations']:.6f} | {count['coreBytes']:.3f} |")
    lines.extend(["", "Per-frame medians of run mean counters (full phases):", "",
                  "| Counter | Native | SDL |", "|---|---:|---:|"])
    nc = primary["counterMedianRunMeans"]["native-phases"]
    sc = primary["counterMedianRunMeans"]["sdl-phases"]
    for key in ("draws", "renderItems", "primitiveTriangles", "vectorGets", "vectorSets",
                "uniformSets", "uniformSlotTests", "worldCalls", "worldRebuilds",
                "regexCompiles", "hudDraws", "coreAllocations", "coreBytes", "uploadBytes",
                "managedNew", "pendingTimers", "timerCallbacks", "rmlTreeChanges",
                "rmlFullProjections", "rmlTextUpdates"):
        native_only = {"primitiveTriangles", "vectorGets", "vectorSets", "uniformSets",
                       "uniformSlotTests", "worldCalls", "worldRebuilds", "regexCompiles",
                       "hudDraws", "coreAllocations", "coreBytes"}
        sdl_only = {"rmlTreeChanges", "rmlFullProjections", "rmlTextUpdates"}
        lines.append(f"| {key} | {'—' if key in sdl_only else f'{nc[key]:.6f}'} | "
                     f"{'—' if key in native_only else f'{sc[key]:.6f}'} |")
    lines.extend(["", "`uploadBytes` in native counts actual bgfx uniform bytes enqueued, not "
                  "vertex/index uploads; SDL counts its buffer upload batch. These are "
                  "different categories and must not be compared as equivalent traffic.",
                  "", "Receipts: `build\\lite-c99\\perf-deep-dive\\Matched15`, "
                  "`Renderer5`, `GuardQualification180`, `GuardQualification1380`; derived ledger/thread/GC/GPU statistics: "
                  "`derived.json`. All source/binary/config hashes and individual samples "
                  "remain in those directories."])
    text = "\n".join(lines)
    (build / "results-tables.txt").write_text(text, encoding="utf-8")
    if args.update_report:
        report = args.native_root / "Documentation/LitePerformanceInvestigation.md"
        document = report.read_text(encoding="utf-8")
        begin = "<!-- PERF_RESULTS_BEGIN -->"
        end = "<!-- PERF_RESULTS_END -->"
        head, rest = document.split(begin, 1)
        _, tail = rest.split(end, 1)
        report.write_text(head + begin + "\n" + text + "\n" + end + tail, encoding="utf-8")
    print(json.dumps(primary["mediansOfRunAveragesMs"], indent=2))
    print(json.dumps(renderer["mediansOfRunAveragesMs"], indent=2))


if __name__ == "__main__":
    main()
