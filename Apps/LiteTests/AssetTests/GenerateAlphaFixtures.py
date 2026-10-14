"""Labeled synthetic alpha inputs; never regenerates the unchanged real sample assets."""
import copy
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
import GenerateMaterialFixtures as source

output = Path(sys.argv[1]).resolve()
document = copy.deepcopy(source.document)
material = document["materials"][0]
for role in ("normalTexture", "occlusionTexture", "emissiveTexture"):
    material.pop(role)
material["emissiveFactor"] = [.4, .1, .2]
material["pbrMetallicRoughness"].pop("baseColorTexture")
material["pbrMetallicRoughness"].pop("metallicRoughnessTexture")
material["pbrMetallicRoughness"]["baseColorFactor"] = [1, 1, 1, 1]
pixels = bytes(v for a in (0, 127, 128, 255) for v in (90, 170, 210, a))
image_index = len(document["images"])
document["images"].append({"bufferView": source.view(source.png(pixels)),
                           "mimeType": "image/png"})
document["textures"].append({"source": image_index, "sampler": 0})
document["buffers"][0]["byteLength"] = len(source.binary)
cases = []


def emit(name, doc):
    doc["bufferViews"] = copy.deepcopy(source.views)
    doc["buffers"][0]["byteLength"] = len(source.binary)
    source.save(name, doc)


def save(name, mode=None, alpha=1, cutoff=None, textured=False, maps=False, transform=None):
    d = copy.deepcopy(document)
    m = d["materials"][0]
    if mode is not None:
        m["alphaMode"] = mode
    if cutoff is not None:
        m["alphaCutoff"] = cutoff
    m["pbrMetallicRoughness"]["baseColorFactor"][3] = alpha
    if textured:
        m["pbrMetallicRoughness"]["baseColorTexture"] = {"index": image_index}
    if maps:
        for key in ("normalTexture", "occlusionTexture", "emissiveTexture"):
            m[key] = copy.deepcopy(source.document["materials"][0][key])
        m["pbrMetallicRoughness"]["metallicRoughnessTexture"] = {"index": 1}
        del d["meshes"][0]["primitives"][0]["attributes"]["TANGENT"]
    if transform:
        d["nodes"][0].update(transform)
    emit(name, d)
    cases.append({"name": name, "mode": mode or "OPAQUE", "factor": alpha,
                  "cutoff": .5 if cutoff is None else cutoff,
                  "imageAlpha": [0, 127, 128, 255] if textured else None,
                  "maps": maps})


save("opaque-default-zero", alpha=0)
save("opaque-factor-reference")
save("opaque-image-zero", "OPAQUE", 0, 2, True)
save("opaque-image-reference", "OPAQUE", 1, None, True)
for alpha, name in ((0, "zero"), (.4999, "below"), (.5, "equal"),
                    (.5001, "above"), (1, "one")):
    save("mask-factor-" + name, "MASK", alpha)
    save("blend-factor-" + name, "BLEND", alpha)
save("mask-zero-cutoff", "MASK", 0, 0)
save("mask-above-one-cutoff", "MASK", 1, 1.001)
save("mask-custom-equal", "MASK", .123456, .123456)
save("mask-image-default", "MASK", 1, None, True)
save("mask-image-equal", "MASK", 1, 128 / 255, True)
save("mask-image-factor-equal", "MASK", .5, 64 / 255, True)
save("mask-image-factor-below", "MASK", .4999, 64 / 255, True)
save("blend-image-factor", "BLEND", .375, 2, True)
save("mask-maps", "MASK", 1, .5, True, True)
save("opaque-maps-reference", "OPAQUE", 1, None, True, True)
save("blend-maps", "BLEND", .625, None, True, True)
save("mask-mirrored", "MASK", 1, .5, True, True, {"scale": [-1, 1, 1]})
save("mask-backface", "MASK", 1, .5, True, True, {"rotation": [0, 1, 0, 0]})
save("opaque-mirrored-reference", "OPAQUE", 1, None, True, True, {"scale": [-1, 1, 1]})
save("opaque-backface-reference", "OPAQUE", 1, None, True, True, {"rotation": [0, 1, 0, 0]})
save("blend-mirrored", "BLEND", .375, None, True, True, {"scale": [-1, 1, 1]})
save("blend-backface", "BLEND", .625, None, True, True, {"rotation": [0, 1, 0, 0]})
for mode in ("MASK", "BLEND", "OPAQUE"):
    for face in ("front", "back"):
        d = copy.deepcopy(document)
        d["materials"][0]["doubleSided"] = False
        d["materials"][0]["alphaMode"] = mode
        if face == "front":
            d["nodes"][0]["rotation"] = [0, 1, 0, 0]
        emit(mode.lower() + "-single-sided-" + face, d)
for mode in ("MASK", "BLEND"):
    d = copy.deepcopy(document)
    d["materials"][0]["alphaMode"] = mode
    d["extensionsUsed"] = ["EXT_mesh_gpu_instancing"]
    d["extensionsRequired"] = ["EXT_mesh_gpu_instancing"]
    d["nodes"][0]["extensions"] = {"EXT_mesh_gpu_instancing": {"attributes": {"TRANSLATION": 8}}}
    emit(mode.lower() + "-instances", d)

# Near-first submission deliberately contradicts back-to-front transparent depth.
for name, reverse, mixed in (("overlap-near-first", False, False),
                              ("overlap-far-first", True, False),
                              ("mixed-phases", False, True)):
    d = copy.deepcopy(document)
    d["materials"] = []
    d["meshes"] = []
    d["nodes"] = []
    for i, (z, color, alpha, mode) in enumerate((
            (-.2, [.5, .04, .01], .25, "BLEND"),
            (.2, [.01, .04, .5], .5, "BLEND"),
            (.5, [.01, .5, .04], 0, "MASK"),
            (.6, [.1, .1, .1], 0, "OPAQUE"))[:4 if mixed else 2]):
        m = copy.deepcopy(material)
        m["emissiveFactor"] = color
        m["alphaMode"] = mode
        m["pbrMetallicRoughness"]["baseColorFactor"][3] = alpha
        d["materials"].append(m)
        mesh = copy.deepcopy(document["meshes"][0])
        mesh["primitives"][0]["material"] = i
        d["meshes"].append(mesh)
        d["nodes"].append({"mesh": i, "translation": [0, 0, z]})
    d["scenes"][0]["nodes"] = list(reversed(range(len(d["nodes"])))) if reverse else list(range(len(d["nodes"])))
    emit(name, d)
    if name == "overlap-near-first":
        for suffix, nodes in (("near-first", [0, 1]), ("far-first", [1, 0])):
            tied = copy.deepcopy(d)
            tied["nodes"][0]["translation"][2] = 0
            tied["nodes"][1]["translation"][2] = 0
            tied["scenes"][0]["nodes"] = nodes
            emit("equal-depth-" + suffix, tied)
        emit("equal-depth-orders", tied)
        for index, suffix in ((0, "near"), (1, "far")):
            ref = copy.deepcopy(d)
            ref["scenes"][0]["nodes"] = [index]
            ref["materials"][index]["alphaMode"] = "OPAQUE"
            emit("overlap-reference-" + suffix, ref)
        probe = copy.deepcopy(d)
        for index, offset in ((0, .4), (1, -.4)):
            p = list(source.positions)
            for component in range(2, len(p), 3):
                p[component] = offset
            accessor = {"bufferView": source.view(struct.pack("<12f", *p)),
                        "componentType": 5126, "count": 4, "type": "VEC3"}
            probe["meshes"][index]["primitives"][0]["attributes"]["POSITION"] = len(probe["accessors"])
            probe["accessors"].append(accessor)
        probe["buffers"][0]["byteLength"] = len(source.binary)
        emit("blend-no-depth-write", probe)
        for alpha, suffix in ((.5, "pass"), (.4999, "discard")):
            probe = copy.deepcopy(d)
            probe["materials"][0]["alphaMode"] = "MASK"
            probe["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"][3] = alpha
            probe["materials"][1]["alphaMode"] = "OPAQUE"
            emit("mask-depth-" + suffix, probe)
    if mixed:
        ref = copy.deepcopy(d)
        ref["scenes"][0]["nodes"] = [3]
        emit("mixed-background-reference", ref)
        probe = copy.deepcopy(d)
        probe["nodes"][3]["translation"][2] = -.5
        emit("opaque-depth-blocker", probe)

document["buffers"][0]["byteLength"] = len(source.binary)
for key, value, name in (("alphaMode", "UNKNOWN", "bad-mode"),
                         ("alphaCutoff", -.01, "bad-cutoff"),
                         ("alphaCutoff", "0.5", "bad-cutoff-type")):
    d = copy.deepcopy(document)
    d["materials"][0][key] = value
    emit(name, d)
d = copy.deepcopy(document)
d["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"][3] = 1.01
emit("bad-factor-alpha", d)
(output / "alpha-cases.json").write_text(json.dumps(cases, indent=2) + "\n")
(output / "alpha-purpose.json").write_text(json.dumps({
    "syntheticUnitInputs": True,
    "realModelRegeneration": False,
    "factorPrecision": "0.4999/0.5/0.5001 and explicit equal cutoffs through uniforms, not RGBA8.",
    "imageAlpha": "Authored synthetic PNG alpha bytes0/127/128/255; RGB is sRGB, alpha linear.",
    "productionShaderOutputsModified": False,
    "depthEvidence": "A geometrically nearer transparent plane is submitted before a farther one "
                     "because their node translations deliberately contradict vertex depths.",
    "ordering": "Opaque/MASK before BLEND; depth before order; equal-depth order and stable ties.",
    "maps": "Asymmetric normal/occlusion/emissive plus derivative normals and flipped faces.",
}, indent=2) + "\n")
