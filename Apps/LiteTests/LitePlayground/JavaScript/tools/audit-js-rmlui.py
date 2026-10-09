"""Audit the application-only JS projection and qualify its raw frame receipt."""

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--receipt", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[5]
    javascript = root / "Apps" / "LiteTests" / "LitePlayground" / "JavaScript"
    bundle_directory = args.build / "Apps" / "LiteTests" / "LitePlayground" / "JavaScript"
    coverage = json.loads((bundle_directory / "coverage.json").read_text())
    require(coverage["sourcePin"] == "2e064d88ec7422af946f8ec7f089ac6519f99295",
            "Original source pin changed.")
    require(not coverage["generatedEngineIncluded"] and not coverage["bblitecRuntimeIncluded"],
            "Bundler admits a generated engine.")
    require(len(coverage["originalDemoFiles"]) == 27, "Original 27-file demo slice changed.")
    for relative, expected in coverage["originalDemoFiles"].items():
        file = javascript / relative
        require(sha(file) == expected, f"Bundle/source hash mismatch: {relative}")
        git_path = file.relative_to(root).as_posix()
        original = subprocess.check_output(["git", "-C", str(root), "show",
                                            f"3d2a7df01b8482778b31bec4711713e34c99dab1:{git_path}"])
        require(file.read_bytes().replace(b"\r\n", b"\n") == original.replace(b"\r\n", b"\n"),
                f"Original user source edited since qualified parent snapshot: {relative}")
    header = root / "Core" / "LiteLayer" / "Include" / "babylon_lite.h"
    functions = set(re.findall(r"bl_Status\s+(bl_\w+)\s*\(", header.read_text()))
    require(len(functions) == 162, "Reviewed 162-function additive C99 snapshot changed.")
    original_header = subprocess.check_output([
        "git", "-C", str(root), "show",
        "2d94a54df5c1b721a88cdb5c316acc216b3ba60e:Core/LiteLayer/Include/babylon_lite.h"
    ]).decode()
    normalize = lambda value: re.sub(r"\s+", " ", value).strip()
    declarations = lambda text: {
        match.group(1): normalize(match.group(2))
        for match in re.finditer(r"bl_Status\s+(bl_\w+)\s*\((.*?)\)\s*;", text, re.S)
    }
    old_declarations = declarations(original_header)
    current_declarations = declarations(header.read_text())
    camera_ground_parent = subprocess.check_output([
        "git", "-C", str(root), "show",
        "7cae3ad5d69ca83cffad32847b4135ca5fecdce5:Core/LiteLayer/Include/babylon_lite.h"
    ]).decode()
    parent_declarations = declarations(camera_ground_parent)
    require(len(parent_declarations) == 160, "Approved pre-Ground snapshot changed.")
    for name, arguments in parent_declarations.items():
        require(current_declarations.get(name) == arguments, f"Pre-Ground signature changed: {name}")
    require(len(old_declarations) == 122, "Legacy parent ABI snapshot changed.")
    for name, arguments in old_declarations.items():
        require(current_declarations.get(name) == arguments, f"Legacy signature changed: {name}")
    plugin_files = list((root / "Plugins" / "LiteJSBinding" / "Source").glob("*.cpp"))
    host_files = list((root / "Apps" / "LiteTests" / "LitePlayground" / "Source").glob("*.cpp"))
    plugin = "\n".join(file.read_text() for file in plugin_files)
    host = "\n".join(file.read_text() for file in host_files)
    plugin_reach = functions & set(re.findall(r"\bbl_\w+\b", plugin))
    host_reach = functions & set(re.findall(r"\bbl_\w+\b", host))
    missing = functions - plugin_reach - host_reach
    require(not missing, f"Current C99 entrypoints not routed: {sorted(missing)}")
    require(len({name for name in functions if "Ui" in name}) == 24, "UI ABI snapshot changed.")
    cache = (args.build / "CMakeCache.txt").read_text()
    require("BABYLON_LITE_ENABLE_UI:BOOL=ON" in cache, "Qualification must use UI ON.")
    rml_patch_file = args.build / "RmlUiBoxShadowPatch.json"
    rml_patch = json.loads(rml_patch_file.read_text())
    require(rml_patch["verified"] and
            rml_patch["pin"] == "b7b4a0688262832eacf3b9abb41f8bbe73868af8",
            "RmlUI archive/renderer-scoped patch identity did not qualify.")
    for relative, expected in zip(rml_patch["files"], rml_patch["after"], strict=True):
        data = (Path(rml_patch["source"]) / relative).read_bytes().replace(b"\r\n", b"\n")
        require(hashlib.sha256(data).hexdigest() == expected,
                f"RmlUI cache patch postcondition changed: {relative}")
    executable = args.build / "Apps" / "LiteTests" / "LitePlayground" / "LitePlayground.exe"
    require(executable.exists(), "Build executable is missing.")
    result = {
        "schema": "lite-js-rmlui-audit-v1",
        "sourcePin": coverage["sourcePin"],
        "originalUserSourcesUnedited": True,
        "originalUserSourceFiles": coverage["originalDemoFiles"],
        "baselineParentCommit": "3d2a7df01b8482778b31bec4711713e34c99dab1",
        "binary": str(executable.resolve()),
        "binarySha256": sha(executable),
        "cmakeCacheSha256": sha(args.build / "CMakeCache.txt"),
        "publicHeaderSha256": sha(header),
        "coverageSha256": sha(bundle_directory / "coverage.json"),
        "rmlUiSourceAndPatch": rml_patch,
        "rmlUiPatchReceiptSha256": sha(rml_patch_file),
        "publicC99Functions": sorted(functions),
        "bindingDirectReach": sorted(plugin_reach),
        "hostReach": sorted(host_reach),
        "unroutedFunctions": sorted(missing),
        "scope": "C99 symbol routing/source identity; not semantic proof of 1814 package exports",
        "bundleInputs": coverage["bundles"],
        "formalPerformanceComparisonExecuted": False,
    }
    if args.receipt:
        receipt = json.loads(args.receipt.read_text())
        require(receipt["schema"] == "lite-js-rmlui-frames-v1", "Unknown frame receipt.")
        require(receipt["result"] == 0 and receipt["retainedUi"], "Protocol run did not pass UI ON.")
        require(not receipt["physicalInputAccepted"] and not receipt["measuredCaptures"],
                "Protocol permits input/capture contamination.")
        samples = receipt["samples"]
        require(len(samples) == receipt["measureFrames"] == receipt["sampleCount"],
                "Missing measured samples.")
        require([item["frame"] for item in samples] ==
                list(range(receipt["warmupFrames"] + 1,
                           receipt["warmupFrames"] + receipt["measureFrames"] + 1)),
                "Sample frame sequence is incomplete.")
        gpu_ids = set()
        for sample in samples:
            require(sample["nativeExcludingCallbacksMs"] >= -1e-6, "Callback timing exceeded frame.")
            for name in ("uiUpdateMs", "uiRenderMs", "presentMs", "hostApiFrameMs",
                         "completeHostWallMs", "processCpuCompleteHostMs", "osThreadCpuMs"):
                require(sample[name] >= 0, f"Negative {name}.")
            gpu = sample["gpu"]
            if gpu is not None:
                require(gpu["frameId"] not in gpu_ids, "Duplicate GPU query ID.")
                gpu_ids.add(gpu["frameId"])
                require(isinstance(gpu["begin"], str) and isinstance(gpu["end"], str),
                        "64-bit GPU ticks must remain exact decimal strings.")
                require(int(gpu["end"]) > int(gpu["begin"]) and gpu["frequency"] > 0,
                        "Invalid GPU query timestamps/frequency.")
        for before, after in zip(receipt["uiBefore"], receipt["uiAfter"], strict=True):
            for name in ("geometryCompileCount", "textureCreateCount", "uploadedBytes"):
                require(before[name] == after[name], f"Retained UI steady-state {name} changed.")
        result["protocolReceipt"] = {
            "path": str(args.receipt.resolve()),
            "sha256": sha(args.receipt),
            "sampleCount": len(samples),
            "uniqueGpuQueryCount": len(gpu_ids),
            "retainedUiResourcePlateau": True,
        }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"JS/RmlUI audit passed: 27 original files, 162 routed C99 functions, "
          f"{len(plugin_reach)} directly referenced by binding; no whole-package/performance claim.")


if __name__ == "__main__":
    main()
