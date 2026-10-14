import copy
import json
from pathlib import Path
import struct
import sys
import zlib

output = Path(sys.argv[1]).resolve()
output.mkdir(parents=True, exist_ok=True)


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def png(pixels):
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(b"\0" + pixels[:8] + b"\0" + pixels[8:])) +
            chunk(b"IEND", b""))


binary = bytearray()
views = []


def view(data):
    binary.extend(b"\0" * (-len(binary) % 4))
    views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(data)})
    binary.extend(data)
    return len(views) - 1


positions = [-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0]
streams = [
    (positions, "VEC3"),
    ([0, 0, 1] * 4, "VEC3"),
    ([0, 0, 1, 0, 1, 1, 0, 1], "VEC2"),
    ([1, 0, 0, 1] * 4, "VEC4"),
    ([.75, .75] * 4, "VEC2"),
]
accessors = []
for values, shape in streams:
    accessors.append({"bufferView": view(struct.pack("<" + "f" * len(values), *values)),
                      "componentType": 5126, "count": 4, "type": shape})
accessors.append({"bufferView": view(struct.pack("<6H", 0, 1, 2, 0, 2, 3)),
                  "componentType": 5123, "count": 6, "type": "SCALAR"})
accessors[0]["min"] = [-1, -1, 0]
accessors[0]["max"] = [1, 1, 0]
accessors.append({"bufferView": view(struct.pack("<16f", *([1, 0, 0, -1] * 4))),
                  "componentType": 5126, "count": 4, "type": "VEC4"})
accessors.append({"bufferView": view(struct.pack("<8f", *([0, 0] * 4))),
                  "componentType": 5126, "count": 4, "type": "VEC2"})
accessors.append({"bufferView": view(struct.pack("<6f", -2, 0, 0, 2, 0, 0)),
                  "componentType": 5126, "count": 2, "type": "VEC3"})
colors = [
    [64, 191, 255, 29, 191, 64, 255, 91, 64, 64, 255, 157, 191, 191, 255, 221],
    [32, 93, 255, 7] * 4,
    [128, 230, 204, 17, 128, 26, 204, 43, 230, 128, 204, 89, 26, 128, 204, 193],
    [32, 241, 181, 19, 224, 53, 71, 67, 96, 141, 37, 149, 160, 83, 213, 233],
    [64, 128, 192, 31, 192, 64, 128, 79, 128, 192, 64, 167, 192, 128, 64, 223],
]
images = [{"bufferView": view(png(bytes(pixels))), "mimeType": "image/png"} for pixels in colors]
material = {
    "doubleSided": True,
    "pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
                            "metallicRoughnessTexture": {"index": 1},
                            "metallicFactor": 0, "roughnessFactor": .75},
    "normalTexture": {"index": 2, "scale": .375},
    "occlusionTexture": {"index": 3, "strength": .625},
    "emissiveTexture": {"index": 4},
    "emissiveFactor": [.25, .5, .75],
}
document = {
    "asset": {"version": "2.0"}, "buffers": [{"byteLength": len(binary)}],
    "bufferViews": views, "accessors": accessors, "images": images,
    "textures": [{"source": i, "sampler": 0} for i in range(5)],
    "samplers": [{"minFilter": 9728, "magFilter": 9728, "wrapS": 33071, "wrapT": 33648}],
    "materials": [material],
    "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1,
                 "TEXCOORD_0": 2, "TANGENT": 3, "TEXCOORD_1": 4},
                 "indices": 5, "material": 0}]}],
    "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0,
}


def save(name, doc):
    encoded = json.dumps(doc, separators=(",", ":")).encode()
    encoded += b" " * (-len(encoded) % 4)
    payload = bytes(binary) + b"\0" * (-len(binary) % 4)
    data = struct.pack("<III", 0x46546C67, 2, 28 + len(encoded) + len(payload))
    data += struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
    data += struct.pack("<II", len(payload), 0x004E4942) + payload
    (output / (name + ".glb")).write_bytes(data)


save("roles-distinct", document)
for role in ("normal", "occlusion", "emissive"):
    d = copy.deepcopy(document)
    d["materials"][0][role + "Texture"]["texCoord"] = 1
    save(role + "-uv1", d)
for scale in (0, 1):
    d = copy.deepcopy(document)
    d["materials"][0]["normalTexture"]["scale"] = scale
    save("normal-scale-" + str(scale), d)
for strength in (0, 1):
    d = copy.deepcopy(document)
    d["materials"][0]["occlusionTexture"]["strength"] = strength
    save("occlusion-strength-" + str(strength), d)
for factor, name in (([0, 0, 0], "zero"), ([1, 1, 1], "one"), (None, "missing")):
    d = copy.deepcopy(document)
    if factor is None:
        del d["materials"][0]["emissiveFactor"]
    else:
        d["materials"][0]["emissiveFactor"] = factor
    save("emissive-factor-" + name, d)
d = copy.deepcopy(document)
del d["materials"][0]["emissiveTexture"]
save("emissive-factor-only", d)
d = copy.deepcopy(document)
for role in ("normal", "occlusion", "emissive"):
    d["materials"][0][role + "Texture"]["index"] = 0
d["materials"][0]["pbrMetallicRoughness"]["metallicRoughnessTexture"]["index"] = 0
save("roles-shared", d)
d = copy.deepcopy(document)
d["nodes"][0]["scale"] = [-1, 1, 1]
save("normal-mirrored-node", d)
d = copy.deepcopy(document)
d["nodes"][0]["rotation"] = [0, 1, 0, 0]
save("normal-double-sided-back", d)
d = copy.deepcopy(document)
del d["meshes"][0]["primitives"][0]["attributes"]["TANGENT"]
save("normal-cotangent", d)
d = copy.deepcopy(document)
del d["meshes"][0]["primitives"][0]["attributes"]["TANGENT"]
d["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"] = 7
save("normal-cotangent-degenerate-uv", d)
d = copy.deepcopy(document)
d["meshes"][0]["primitives"][0]["attributes"]["TANGENT"] = 6
save("normal-handedness-minus", d)
d = copy.deepcopy(document)
d["extensionsUsed"] = ["EXT_mesh_gpu_instancing"]
d["extensionsRequired"] = ["EXT_mesh_gpu_instancing"]
d["nodes"][0]["extensions"] = {"EXT_mesh_gpu_instancing": {"attributes": {"TRANSLATION": 8}}}
save("normal-instanced", d)
d = copy.deepcopy(document)
d["materials"][0]["occlusionTexture"]["strength"] = 1
save("emissive-alpha", d)
for role in ("normal", "occlusion", "emissive"):
    d = copy.deepcopy(document)
    d["materials"][0][role + "Texture"]["index"] = 5
    save("bad-" + role + "-index", d)
    d = copy.deepcopy(document)
    d["materials"][0][role + "Texture"]["texCoord"] = 1
    del d["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_1"]
    save("missing-" + role + "-uv1", d)
d = copy.deepcopy(document)
d["materials"][0]["normalTexture"]["scale"] = 0
d["materials"][0]["occlusionTexture"]["strength"] = 0
d["materials"][0]["emissiveFactor"] = [0, 0, 0]
save("all-effects-zero", d)
(output / "purpose.json").write_text(json.dumps({
    "syntheticUnitInputs": True, "realModelRegeneration": False,
    "normal": "Asymmetric signed tangent normals; authored handedness and cotangent variant.",
    "occlusion": "Asymmetric linear red channel and strengths0/.625/1.",
    "emissive": "Asymmetric sRGB RGB/linear alpha, factors0/missing/.25-.5-.75/1 and no texture.",
    "cache": "Same glTF texture referenced in five roles; sRGB/linear physical ownership must differ.",
}, indent=2) + "\n")
