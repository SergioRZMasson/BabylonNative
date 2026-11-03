"""Persist approved-policy M0 results; retain all earlier batches and expose remaining failures."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess

from harness import digest, write
from collect_evidence import ctest_summary


def collect(build, evidence):
    output = evidence / "approved-final-v4"
    output.mkdir(parents=True, exist_ok=True)
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
                p = build / "formal-perf-final" / language / backend / str(scene) / "summary.json"
                lanes[backend] = json.loads(p.read_text())
                if lanes[backend]["pairs"] != 15:
                    raise ValueError(f"Incomplete lane: {p}")
            row[language] = {"parity": parity, "performance": lanes}
        scenes.append(row)
    files = subprocess.run(["git", "diff", "--name-only"], capture_output=True,
                           text=True, check=True).stdout.splitlines()
    files += subprocess.run(["git", "ls-files", "--others", "--exclude-standard"],
                            capture_output=True, text=True, check=True).stdout.splitlines()
    summary = {
        "baseCommit": "7412ea7b", "changedFiles": files,
        "sourceSha256": {p: digest(Path(p)) for p in files},
        "publicAbiAdditions": [
            "bl_getContainerMeshes", "bl_getSceneMeshes", "bl_getSceneLights",
            "bl_getMeshBounds", "bl_getMaterialName"],
        "newPublicRecordsOrEnums": 0,
        "scenes": scenes,
        "stagingDefault": True,
        "acceptance": "steady-state mean/P95 upper paired95% ratio CI<=1, startup separate",
        "minecraft": json.loads((build / "minecraft-perf-final" / "summary.json").read_text()),
        "ctest": ctest_summary((build / "ctest-final.log").read_text()),
        "anisotropy": json.loads((build / "evidence" / "anisotropy-api-limit.json").read_text()),
        "max16Diagnostic": json.loads((build / "anisotropy16" / "parity.json").read_text()),
        "remainingFailures": [
            "Pinned bgfx has only effective1 or16; exact requested4 cannot be selected without a global-cap extension.",
            "No sampler/SDK override or vendor-source workaround was applied.",
            "Minecraft strict zero-regression qualification follows the recorded paired CI, not a tolerance.",
        ],
    }
    for directory in ("formal-perf-final", "minecraft-perf-final", "evidence", "legacy-hosts-final"):
        shutil.copytree(build / directory, output / directory, dirs_exist_ok=True)
    shutil.copytree(hosts / "parity", output / "parity", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("*.bgra"))
    for file in build.glob("*.log"):
        shutil.copy2(file, output / file.name)
    for name in ("initial-cache.cmake", "minecraft-cache.cmake"):
        shutil.copy2(build / name, output / name)
    for file in (build / "anisotropy16").glob("*"):
        if file.suffix in (".json", ".png", ".py"):
            (output / "anisotropy16").mkdir(exist_ok=True)
            shutil.copy2(file, output / "anisotropy16" / file.name)
    write(output / "summary.json", summary)

    def ratio(value):
        return f"{value['ratio']:.4f} [{value['ci95'][0]:.4f},{value['ci95'][1]:.4f}]"

    def gate(parity):
        status = "XFAIL" if parity.get("expectedFailure") else "P" if parity["pass"] else "F"
        return f"{status} {parity['fullMad']:.7g}/{parity['foregroundMad']:.7g}"

    lines = [
        "## Approved-policy M0 continuation after7412ea7b",
        "",
        "Changes remain uncommitted. Five approved additive C99 queries, no new public records/enums.",
        "Staging is enabled by default through bgfx's supported build switch; no vendor source edited.",
        "Scene200 is an expected calculated pixel failure, not a threshold relaxation or ignored process error.",
        f"Final CTest:{summary['ctest']['total'] - summary['ctest']['failed']}/{summary['ctest']['total']} "
        "pass; four failures are native/JS249/257. The five C99 queries and JS forwarding tests pass.",
        "All four requested Minecraft GPU suites and the native retained-UI10 suites pass.",
        "GPU/latency remain unqualified where unavailable and do not gate the approved frame-time verdict.",
        "",
        "**M0 is not complete:** exact4x anisotropy is not selectable through this pinned bgfx API.",
        "Its public reset flag selects1 or16 on D3D11. The16x control still fails scene249 "
        "(foregroundMAD .11610 vs .05), while the exact4x control passes(.00994).",
        "",
        "|Scene|Native full/foregroundMAD|JS full/foregroundMAD|Native mean ratio [CI]|Native P95 ratio [CI]|Native verdict|JS mean ratio [CI]|JS P95 ratio [CI]|JS verdict|",
        "|---|---|---|---|---|---|---|---|---|",
    ]
    for row in scenes:
        n = row["Native"]["performance"]["sdl_gpu"]
        j = row["Js"]["performance"]["sdl_gpu"]
        lines.append(f"|{row['scene']}|{gate(row['Native']['parity'])}|{gate(row['Js']['parity'])}|"
                     f"{ratio(n['meanMs'])}|{ratio(n['p95Ms'])}|{n['verdict']}|"
                     f"{ratio(j['meanMs'])}|{ratio(j['p95Ms'])}|{j['verdict']}|")
    minecraft = summary["minecraft"]
    lines += [
        "", "### Minecraft regression",
        f"Paired runs:{minecraft['pairs']}; complete-host mean new/old:{ratio(minecraft['meanMs'])}; "
        f"P95:{ratio(minecraft['p95Ms'])}; verdict:**{minecraft['verdict']}**.",
        "The mean upper CI is below1; the P95 CI still straddles1. This is no detected regression, "
        "not a confirmed strict mean-and-P95 no-regression pass.",
        "Seed1337/radius6/all169chunks, retained UI,1280x720/MSAA4,120warmup+2000measured; "
        "normal V8 GC retained. Raw source/binary/script hashes are stored in each pair.",
        "Full180, Replay240, NativeInputSaveLoad, CombinedGpu1380 and the native retained-UI10 suites "
        "are recorded in the accompanying CTest logs.",
        "", "### Reproduction and files",
        "See `evidence/commands-final.ps1` for executed build/test/measurement commands.",
        "All56 final lanes have15 pairs; startup, Native/Dawn and JS/Dawn values are in `summary.json`.",
        "",
    ]
    lines += [f"* `{p}`" for p in files]
    text = "\n".join(lines).rstrip() + "\n"
    (output / "SUMMARY.md").write_text(text, encoding="utf-8")
    main = evidence / "SUMMARY.md"
    marker = "## Approved-policy M0 continuation after7412ea7b"
    if marker not in main.read_text(encoding="utf-8"):
        with main.open("a", encoding="utf-8") as file:
            file.write("\n\n" + text)
    shutil.copy2(build / "evidence" / "commands-final.ps1", output / "commands-final.ps1")
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    collect(args.build.resolve(), args.evidence.resolve())
