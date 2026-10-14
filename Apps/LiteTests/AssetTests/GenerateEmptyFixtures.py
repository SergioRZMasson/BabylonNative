"""Independent, synthetic GLB empty-scene correctness fixtures; no corpus inputs."""

import copy
import json
import struct
import sys
from pathlib import Path


def glb(document, binary=None):
    source = json.dumps(document, separators=(",", ":")).encode()
    source += b" " * (-len(source) % 4)
    chunks = struct.pack("<II", len(source), 0x4E4F534A) + source
    if binary is not None:
        binary += b"\0" * (-len(binary) % 4)
        chunks += struct.pack("<II", len(binary), 0x004E4942) + binary
    return struct.pack("<III", 0x46546C67, 2, 12 + len(chunks)) + chunks


def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    base = {"asset": {"version": "2.0", "generator": "synthetic-empty-v1"}}
    cases = []

    def emit(name, document, binary=None, status=0, selected=0, children=0, code=0):
        path = output / (name + ".glb")
        path.write_bytes(glb(document, binary))
        cases.append(dict(name=name, status=status, selectedPrimitives=selected,
                          rootChildren=children, sourceErrorCode=code, document=document))

    emit("empty-json-only", base)
    emit("empty-scene-zero", dict(base, scenes=[{}], scene=0))
    emit("empty-default-scene-zero", dict(base, scenes=[{}]))
    emit("empty-with-unreferenced-node", dict(base, nodes=[{"name": "unused"}]))
    camera = dict(base, cameras=[{"type": "perspective",
                                 "perspective": {"yfov": 0.8, "znear": 0.1}}],
                  nodes=[{"name": "synthetic-camera-node", "camera": 0,
                          "translation": [1, 2, 3], "rotation": [0, 0, 0, 1],
                          "scale": [2, 3, 4], "children": [1]},
                         {"name": "synthetic-child", "translation": [5, 6, 7]}],
                  scenes=[{"nodes": [0]}], scene=0)
    emit("camera-only-no-bin", camera, children=1)
    emit("empty-with-legal-bin", dict(base, buffers=[{"byteLength": 4}],
                                     scenes=[{}], scene=0), b"\x13\x27\x39\x51")
    emit("empty-no-buffer-with-bin", base, b"\x13\x27\x39\x51")
    positions = struct.pack("<9f", -0.8, -0.8, 0, 0.8, -0.8, 0, 0, 0.8, 0)
    triangle = dict(base, buffers=[{"byteLength": len(positions)}],
                    bufferViews=[{"buffer": 0, "byteLength": len(positions)}],
                    accessors=[{"bufferView": 0, "componentType": 5126, "count": 3,
                                "type": "VEC3", "min": [-0.8, -0.8, 0], "max": [0.8, 0.8, 0]}],
                    materials=[{"doubleSided": True, "pbrMetallicRoughness":
                                {"baseColorFactor": [0.9, 0.2, 0.1, 1], "metallicFactor": 0}}],
                    meshes=[{"name": "synthetic-triangle",
                             "primitives": [{"attributes": {"POSITION": 0}, "material": 0}]}],
                    nodes=[{"name": "synthetic-mesh-node", "mesh": 0}],
                    scenes=[{"nodes": [0]}, {}], scene=0)
    emit("accepted-triangle", triangle, positions, selected=1, children=1)
    selected_empty = copy.deepcopy(triangle)
    selected_empty["scene"] = 1
    emit("selected-empty-unused-mesh", selected_empty, positions)
    no_scene = copy.deepcopy(triangle)
    del no_scene["scenes"]
    del no_scene["scene"]
    emit("no-scene-unused-mesh", no_scene, positions)
    zero = dict(base, accessors=[{"componentType": 5126, "count": 3, "type": "VEC3",
                                 "min": [0, 0, 0], "max": [0, 0, 0]}],
                meshes=[{"primitives": [{"attributes": {"POSITION": 0}}]}],
                nodes=[{"mesh": 0}], scenes=[{"nodes": [0]}], scene=0)
    emit("implicit-zero-no-bin", zero, selected=1, children=1)
    emit("malformed-missing-bin", dict(base, buffers=[{"byteLength": 4}]), status=1, code=165)
    emit("malformed-short-bin", dict(base, buffers=[{"byteLength": 8}]), b"1234", status=1)
    emit("malformed-view-no-buffer", dict(base, bufferViews=[{"buffer": 0, "byteLength": 4}]),
         status=1)
    emit("malformed-node-mesh", dict(base, nodes=[{"mesh": 0}], scenes=[{"nodes": [0]}]),
         status=1)
    emit("malformed-camera-ref", dict(base, nodes=[{"camera": 0}], scenes=[{"nodes": [0]}]),
         status=1)
    emit("malformed-scene-ref", dict(base, scenes=[{}], scene=1), status=1)
    emit("malformed-scene-root", dict(base, scenes=[{"nodes": [0]}]), status=1)
    emit("malformed-accessor-view", dict(base, accessors=[{"bufferView": 0,
         "componentType": 5126, "count": 1, "type": "VEC3"}]), status=1)
    emit("unsupported-multiple-buffers", dict(base, buffers=[{"byteLength": 4},
         {"byteLength": 4}]), b"1234", status=8)
    emit("unsupported-buffer-uri", dict(base, buffers=[{"byteLength": 4,
         "uri": "synthetic-external.bin"}]), b"1234", status=8)
    (output / "manifest.json").write_text(json.dumps(cases, indent=2) + "\n", encoding="utf-8")
    sequence = ["accepted-triangle", "empty-json-only", "malformed-missing-bin",
                "camera-only-no-bin", "accepted-triangle", "selected-empty-unused-mesh",
                "malformed-node-mesh", "accepted-triangle", "empty-scene-zero",
                "no-scene-unused-mesh", "accepted-triangle"]
    (output / "visible-list.txt").write_text(
        "".join(str((output / (name + ".glb")).resolve()) + "\n" for name in sequence),
        encoding="utf-8")
    print(f"Generated {len(cases)} independent synthetic empty-scene fixtures")


if __name__ == "__main__":
    generate(Path(sys.argv[1]))
