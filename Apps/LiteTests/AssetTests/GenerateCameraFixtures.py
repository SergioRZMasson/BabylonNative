"""Camera-ignore policy fixtures; camera-free counterparts preserve every node/mesh transform."""
import copy
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
import GenerateGeometryFixtures as source

output = Path(sys.argv[1]).resolve()
output.mkdir(parents=True, exist_ok=True)
definitions = [
    {"type": "perspective", "perspective": {"yfov": .7, "znear": .1}},
    {"type": "orthographic", "orthographic": {"xmag": 2, "ymag": 1, "znear": 0, "zfar": 100}},
]
cases = []


def emit(name, asset, expected="BL_OK"):
    entry = {"name": name, "synthetic": True, "expected": expected}
    if expected == "BL_OK":
        entry["document"] = asset.doc
        entry["color"] = [.25, .5, .75, .5]
        entry["geometry"] = [source.geometry_oracle(asset.doc, asset.binary, p)
                             for mesh in asset.doc["meshes"] for p in mesh["primitives"]]
    entry["sha256"] = source.write_glb(output / (name + ".glb"), asset.doc, asset.binary)
    cases.append(entry)


for name in ("unused-both", "unused-wide-negative", "camera-mesh-same-node",
             "camera-parent-mesh-child", "camera-nested-matrix"):
    asset = source.model()
    asset.doc["cameras"] = copy.deepcopy(definitions)
    if name == "unused-wide-negative":
        asset.doc["cameras"][0]["perspective"]["yfov"] = 4
        asset.doc["cameras"][1]["orthographic"]["xmag"] = -2
        asset.doc["cameras"][1]["orthographic"]["ymag"] = -1
    if name == "camera-mesh-same-node":
        asset.doc["nodes"] = [
            {"mesh": 0, "camera": 1, "translation": [.1, -.15, .15], "scale": [.8, .85, 1],
             "children": [1]},
            {"translation": [.2, 0, 0], "children": [2]}, {"translation": [0, .2, 0]}
        ]
    elif name == "camera-parent-mesh-child":
        asset.doc["nodes"] = [
            {"camera": 0, "translation": [.2, .1, .1], "children": [1]},
            {"translation": [-.1, -.1, 0], "scale": [.8, .9, 1], "children": [2]},
            {"mesh": 0, "translation": [0, .1, .1]}
        ]
    elif name == "camera-nested-matrix":
        asset.doc["nodes"] = [
            {"translation": [.1, 0, 0], "children": [1]},
            {"camera": 1, "matrix": [.8, 0, 0, 0, .1, .9, 0, 0, 0, 0, 1, 0,
                                    .1, -.1, .1, 1], "children": [2]},
            {"children": [3]}, {"mesh": 0, "translation": [-.1, .2, 0]}
        ]
    emit("camera-" + name, asset)
    clean = copy.deepcopy(asset)
    clean.doc.pop("cameras")
    for node in clean.doc["nodes"]:
        node.pop("camera", None)
    emit("camera-free-" + name, clean)

for name, index in (("bad-camera-index", 2), ("bad-camera-without-definitions", 0)):
    asset = source.model()
    asset.doc["nodes"][0]["camera"] = index
    if name == "bad-camera-index":
        asset.doc["cameras"] = copy.deepcopy(definitions)
    emit(name, asset, "BL_INVALID_ARGUMENT")
asset = source.model()
asset.doc["cameras"] = [{"type": "perspective", "perspective": {"yfov": .7, "znear": "bad"}}]
emit("bad-camera-unsafe-number", asset, "BL_INVALID_ARGUMENT")
asset = source.model()
asset.doc["cameras"] = copy.deepcopy(definitions)
asset.doc["cameras"][1]["orthographic"]["xmag"] = 0
emit("bad-camera-zero-magnification", asset, "BL_INVALID_ARGUMENT")
(output / "geometry-cases.json").write_text(json.dumps({
    "cameraIgnorePolicy": True, "cases": cases}, indent=2) + "\n", encoding="utf-8")
print("Camera-ignore fixtures", len(cases))
