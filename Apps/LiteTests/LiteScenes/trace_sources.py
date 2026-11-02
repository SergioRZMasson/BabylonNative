"""Record pinned source deltas and exact M0 projection scope, without changing either checkout."""

import argparse
import difflib
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen

from prepare import SCENES

PIN132 = "2e064d88ec7422af946f8ec7f089ac6519f99295"


def source_deltas(corpus, output, audit):
    results = []
    records = json.loads((audit / "scenes.json").read_text())["scenes"]
    entries = {}
    source_root = corpus / "corpus" / "babylon-lite"
    for number in SCENES:
        scene = next(x for x in records if x["id"] == f"scene{number}")
        for item in scene["sourceGraph"]:
            path = Path(item["path"])
            entries[path.relative_to(source_root).as_posix()] = number if path.stem == f"scene{number}" else path.stem
    for relative, number in sorted(entries.items()):
        url = f"https://raw.githubusercontent.com/BabylonJS/Babylon-Lite/{PIN132}/{relative}"
        old = (corpus / "corpus" / "babylon-lite" / relative).read_bytes()
        label = f"scene{number}" if isinstance(number, int) else number
        destination = output / f"{label}-1.32.ts"
        if not destination.exists():
            with urlopen(url, timeout=30) as response:
                destination.write_bytes(response.read())
        new = destination.read_bytes()
        before = old.decode().splitlines()
        after = new.decode().splitlines()
        delta = "\n".join(difflib.unified_diff(before, after, fromfile="Lite1.25", tofile="Lite1.32"))
        (output / f"{label}.diff").write_text(delta + "\n", encoding="utf-8")
        # The three custom-shader scenes only added the branded identity tag in 1.32.
        untagged = [line.replace("= wgsl`", "= `") for line in after
                    if line != 'import { wgsl } from "babylon-lite/shader/wgsl.js";']
        equivalent = before == after or before == untagged
        results.append({
            "scene": number, "relativeSource": relative, "url": url,
            "workloadSha256": hashlib.sha256(old).hexdigest(),
            "authoritySha256": hashlib.sha256(new).hexdigest(), "equalLF": before == after,
            "onlyIdentityWgslTagAdded": before != after and before == untagged,
            "reviewedEquivalent": equivalent,
            "adaptationsToWorkload": [],
        })
    (output / "source-deltas.json").write_text(json.dumps(results, indent=2) + "\n")
    if not all(x["reviewedEquivalent"] for x in results):
        raise ValueError("A scene has an unreviewed 1.32 source delta")


def projection_scope(milestones, output):
    milestone = next(x for x in json.loads(milestones.read_text())["milestones"] if x["id"] == "M0")
    types = {
        "ArcRotateCamera", "AssetContainer", "EngineContext", "FreeCamera", "GsShaderFragment",
        "LightBase", "Material", "Mesh", "SceneContext", "SceneNode", "ShaderMaterial",
        "Texture2D", "TransformNode",
    }
    forwarded = {
        "attachControl", "createShaderMaterial", "loadGltf", "loadEnvironment", "rebuildMaterial",
        "loadEnvironment@arg2.brdfUrl", "loadEnvironment@arg2.skipSkybox",
        "loadEnvironment@arg2.skipGround", "createEngine@arg1.useHighPrecisionMatrix",
    }
    notes = {
        "AssetContainer.animationGroups":
            "Added native clip identity, metadata, speed/loop and play/pause/stop/seek forwarding. "
            "No fabricated targetedAnimations, direct currentTime/isPlaying writes or manager/mixing support.",
        "AssetContainer.packets":
            "Source AssetContainer has xmpMetadata.packets, not a direct packets field. Native XMP loading has no C99 contract.",
        "AssetContainer.skeletons": "Native bone-control/skeleton identities have no C99 contract; GPU skinning is M3.",
        "EngineContext.useHighPrecisionMatrix":
            "Added creation-time getter. Mutations differing from creation explicitly reject; reviewed ABI has no setter.",
        "createEngine":
            "Existing C99 creation forwarding; unsupported floating-origin, limits and DPR now reject rather than silently disappear.",
        "createEngine@arg1.msaaSamples":
            "Accepted only when equal to the host's actual target request. Binding cannot silently recreate a borrowed swapchain.",
        "createEngine@arg1.requiredLimits": "Nonempty WebGPU device limits have no equivalent reviewed native capability/creation contract.",
        "createEngine@arg1.requiredLimits.maxColorAttachmentBytesPerSample":
            "No WebGPU device-limits contract; explicit UNSUPPORTED.",
        "createEngine@arg1.maxDevicePixelRatio":
            "Host supplies physical pixel dimensions; native DPR policy has no reviewed creation operation.",
        "getContainerMeshes":
            "No C99 node-kind/mesh enumeration query. Do not copy native membership/hierarchy algorithms into JS or inspect private Core records.",
        "Material.name": "No C99 material-name query/mutation; do not shadow independent engine state in JS.",
        "Mesh.boundMax": "No C99 bounds getter; no private-record access in the projection.",
        "Mesh.boundMin": "No C99 bounds getter; no private-record access in the projection.",
        "Mesh.morphTargets": "Native morph-target identities/deformation are M3, absent from the public ABI.",
        "Mesh.pickable": "Native picking flags/behavior have no public ABI; not a JS-only writable bag.",
        "SceneContext.animationGroups": "No C99 scene animation enumeration/manager mutation contract; container clips are now projected.",
        "SceneContext.imageProcessing": "Internal image processing exists, but public query/mutation is absent (M2).",
        "SceneContext.lights": "No C99 scene member enumeration/mutation API; JS must not invent a second scene list.",
        "SceneContext.meshes": "No C99 scene member enumeration/mutation API; JS must not invent a second scene list.",
        "enableMaterialUvTransform": "No C99 UV-transform enable/state/dirty operation (M2/M4).",
        "loadEnvironment@arg2.groundTextureUrl": "Already marshalled, but native ground construction explicitly rejects (M4).",
        "loadEnvironment@arg2.skyboxUrl": "Already marshalled, but native skybox construction explicitly rejects (M4).",
        "loadEnvironment@arg2.skyboxSize": "Already marshalled; skybox construction remains unsupported (M4).",
        "rebuildMaterial@arg2.rebuildFrameGraph":
            "Already marshalled; true explicitly rejects because native frame graphs are M6.",
    }
    results = []
    for api in milestone["apis"]["projection-completion"]:
        if api in types:
            status, note = "type-only", "Erased TypeScript interface/type, not a missing JavaScript runtime export."
        elif api == "setMeshVisible":
            status, note = "implemented", "Added original package alias of native setSubtreeVisible; forwarding test covers descendants."
        elif api in forwarded:
            status, note = "existing-scoped-forwarding", "Existing forwarding remains; per-feature native support is not implied."
        elif api.startswith("createCopyToTextureTask"):
            status, note = "blocked-native-contract", "Native copy/resolve/viewport frame-graph tasks have no C99 contract (M6)."
        elif api in notes:
            status = "partial" if api in {
                "AssetContainer.animationGroups", "EngineContext.useHighPrecisionMatrix",
                "createEngine", "createEngine@arg1.msaaSamples",
            } else "blocked-native-contract"
            note = notes[api]
        else:
            raise ValueError(f"Unclassified M0 API: {api}")
        results.append({"api": api, "status": status, "evidence": note})
    (output / "projection-coverage.json").write_text(json.dumps({
        "milestone": "M0", "publicAbiAdditions": [], "complete": False, "apis": results,
    }, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--milestones", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    audit = Path(json.loads(args.milestones.read_text())["source"])
    source_deltas(args.corpus, args.output, audit)
    projection_scope(args.milestones, args.output)
