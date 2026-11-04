"""Verify the frozen audit and prepare read-only raw URI mappings and user-code sources."""

import argparse
import hashlib
import json
from pathlib import Path

M0_SCENES = (159, 161, 163, 168, 200, 247, 248, 249, 256, 257, 258, 259, 260, 266)
M1_SCENES = (16, 43, 162, 165, 201, 204, 213, 268, 270, 274, 278, 279)
SCENES = M0_SCENES + M1_SCENES
PIN = "286525f8041dd9adc72b2c9962e8bff4d9aeb764"


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verified(path, expected):
    path = Path(path)
    if not expected or sha(path) != expected:
        raise ValueError(f"Stale/missing input: {path}")
    return path


def prepare(corpus, audit, output):
    audit = Path(audit)
    corpus = Path(corpus)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    records = json.loads((audit / "scenes.json").read_text(encoding="utf-8"))["scenes"]
    result = []
    for number in SCENES:
        scene = next(x for x in records if x["id"] == f"scene{number}")
        source = verified(scene["source"]["path"], scene["source"]["sha256"])
        registry = scene["bblitec"]["registry"]
        build = scene["bblitec"]["localBuild"]
        executable = verified(build["executable"], build["executableSha256"])
        # Preserve relative imports; never bundle or link a generated engine.
        source_root = corpus / "corpus" / "babylon-lite" / "lab" / "lite" / "src"
        graph = scene["sourceGraph"]
        files = graph if isinstance(graph, list) else graph.get("files", [])
        inputs = [scene["source"]]
        for item in files:
            if isinstance(item, dict) and "path" in item and item["path"].endswith(".ts"):
                inputs.append(item)
        copied = []
        for item in inputs:
            path = Path(item["path"])
            if not path.is_relative_to(source_root):
                raise ValueError(f"Non-user-code dependency: {path}")
            verified(path, item["sha256"])
            relative = path.relative_to(source_root)
            destination = output / "source" / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            if not destination.exists() or destination.read_bytes() != path.read_bytes():
                destination.write_bytes(path.read_bytes())
            copied.append({"path": str(path), "sha256": sha(path)})
        mappings = {}
        resources = []

        def add(uri, record):
            path = verified(record["path"], record["sha256"])
            if uri in mappings and mappings[uri] != str(path):
                if sha(mappings[uri]) != sha(path):
                    raise ValueError(f"Conflicting URI: {uri}")
            mappings[uri] = str(path)
            resources.append({"uri": uri, "path": str(path), "sha256": sha(path)})
            public_roots = (
                f"https://raw.githubusercontent.com/BabylonJS/Babylon-Lite/{PIN}/lab/public/",
                f"https://raw.githubusercontent.com/BabylonJS/Babylon-Lite/{PIN}/packages/babylon-lite/assets/",
            )
            for root in public_roots:
                if uri.startswith(root):
                    mappings["/" + uri[len(root):]] = str(path)

        for asset in scene["assets"]:
            raw = asset.get("rawSource")
            if not raw:
                raise ValueError(f"Missing original raw asset: {asset['source']}")
            add(asset["source"], raw)
            for resource in asset.get("rawResourceGraph", []):
                add(resource["uri"], resource)
        (output / f"scene{number}.tsv").write_text(
            "".join(f"{uri}\t{path}\n" for uri, path in sorted(mappings.items())), encoding="utf-8"
        )
        golden = corpus / registry["parity"]["reference"]["path"]
        if not golden.is_file():
            raise ValueError(f"Missing Chromium golden: {golden}")
        result.append({
            "id": number, "source": str(source), "sourceSha256": sha(source),
            "sourceInputs": copied, "workloadPin": PIN,
            "golden": str(golden), "goldenSha256": sha(golden),
            "width": registry.get("width", 1280), "height": registry.get("height", 720),
            "parity": registry["parity"], "pose": scene["bblitec"]["pose"],
            "executable": str(executable), "executableSha256": sha(executable),
            "buildStamp": json.loads(Path(build["generatedBuildInputs"]).read_text())["stamp"],
            "resources": resources,
            "packagedAssets": [{"path": a["path"], "sha256": a["sha256"]} for a in scene["assets"]],
        })
    (output / "scenes.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Verified {len(result)} scenes, raw resources, Chromium goldens and bblitec executables.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--audit", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    prepare(args.corpus, args.audit, args.output)
