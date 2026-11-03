"""Separate native feature milestones from genuinely missing M0 inspection forwarding."""

import argparse
import json
from pathlib import Path


def mapping(milestones, previous):
    source = json.loads(milestones.read_text())
    owners = {m["id"]: m["title"] for m in source["milestones"]}
    explicit = {
        "AssetContainer.animationGroups": ["M3"],
        "AssetContainer.packets": ["M5"],
        "AssetContainer.skeletons": ["M3"],
        "enableMaterialUvTransform": ["M2", "M4"],
        "EngineContext.useHighPrecisionMatrix": ["M0"],
        "getContainerMeshes": ["M0"],
        "Material.name": ["M0"],
        "Mesh.boundMax": ["M0"],
        "Mesh.boundMin": ["M0"],
        "Mesh.morphTargets": ["M3"],
        "Mesh.pickable": ["M6"],
        "SceneContext.animationGroups": ["M3"],
        "SceneContext.imageProcessing": ["M2"],
        "SceneContext.lights": ["M0"],
        "SceneContext.meshes": ["M0"],
        "loadEnvironment@arg2.groundTextureUrl": ["M4"],
        "loadEnvironment@arg2.skyboxUrl": ["M4"],
        "loadEnvironment@arg2.skyboxSize": ["M4"],
        "createEngine@arg1.requiredLimits": ["M6"],
        "createEngine@arg1.requiredLimits.maxColorAttachmentBytesPerSample": ["M6"],
        "createEngine@arg1.maxDevicePixelRatio": ["M13"],
        "rebuildMaterial@arg2.rebuildFrameGraph": ["M6"],
        "createEngine@arg1.msaaSamples": ["M0"],
        "createEngine": ["M0", "M1", "M6", "M13"],
    }
    results = []
    for item in json.loads(previous.read_text())["apis"]:
        if item["status"] not in ("partial", "blocked-native-contract"):
            continue
        api = item["api"]
        ids = ["M6"] if api.startswith("createCopyToTextureTask") else explicit[api]
        result = {
            "api": api, "owners": [{"id": x, "title": owners[x]} for x in ids],
            "previousEvidence": item["evidence"],
        }
        if api == "EngineContext.useHighPrecisionMatrix":
            result.update({
                "m0Status": "implemented-source-equivalent",
                "reason": "Original engine.ts reads the option once to select matrix storage. "
                          "The exposed engine field is then plain writable metadata; changing "
                          "it does not reallocate or change existing/new matrix storage.",
            })
        elif api in ("createEngine@arg1.msaaSamples", "createEngine"):
            result.update({
                "m0Status": "supported-native-embedding",
                "reason": "Creation/HPM and requested MSAA equal to the host-owned attachment "
                          "are forwarded. Dynamic surface configuration/LWR, MRT limits and "
                          "browser DPR are separate later-milestone work, not fake native options.",
            })
        elif ids == ["M0"]:
            result.update({
                "m0Status": "implemented-native-and-js",
                "reason": "Approved read-only C99 count/fill/bounds/name queries expose owned "
                          "native data. Projection forwards queries, with no private Core access "
                          "or independent JS collection state.",
            })
        else:
            result.update({"m0Status": "later-feature", "reason": "Not implemented by claiming a JS-only field."})
        results.append(result)
    if len(results) != 34:
        raise ValueError(f"Expected all 34 unresolved entries, got {len(results)}")
    return {
        "milestone": "M0", "milestoneSource": str(milestones), "entries": results,
        "proposedC99Queries": [
            "bl_getContainerMeshes(container, bl_Mesh* destination, size_t capacity, size_t* count)",
            "bl_getSceneMeshes(scene, bl_Mesh* destination, size_t capacity, size_t* count)",
            "bl_getSceneLights(scene, bl_Light* destination, size_t capacity, size_t* count)",
            "bl_getMeshBounds(mesh, bl_Vec3* minimum, bl_Vec3* maximum)",
            "bl_getMaterialName(material, bl_String* name)",
        ],
        "contractProposal": {
            "status": "approved-implemented-five-additive-queries",
            "newPublicRecordsOrEnums": 0,
            "ownership": "List queries copy typed identities into caller storage; name borrows "
                         "the existing immutable runtime-owned material string.",
            "lists": "NULL destination/capacity0 queries required count. Insufficient capacity "
                     "fails without partial writes. Scene duplicates/insertion order are "
                     "retained; container flattening follows original first-visit deduplication.",
            "validation": "Existing runtime, generation/type, thread and disposal checks apply; "
                          "failed calls leave outputs unchanged.",
        },
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--milestones", type=Path, required=True)
    parser.add_argument("--previous", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(mapping(args.milestones, args.previous), indent=2) + "\n")
