import json
from pathlib import Path
import struct
import sys
import zlib

output = Path(sys.argv[1]).resolve()
output.mkdir(parents=True, exist_ok=True)
png_magic = b"\x89PNG\r\n\x1a\n"


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


pixels = bytes([255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255])
png = png_magic + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(b"\x00" + pixels[:8] + b"\x00" + pixels[8:])) + chunk(b"IEND", b"")
vertices = []
for triangle in range(2):
    for x, y in ((0, 0), (1, 0), (0, 1)):
        vertices.append(struct.pack("<10f", x + triangle * 1.5, y, 0, 0, 0, 1, .75, .75, .25, .25))
binary = bytearray(b"".join(vertices))
views = [{"buffer": 0, "byteOffset": 0, "byteLength": len(binary), "byteStride": 40}]
indices = struct.pack("<6H", 0, 1, 2, 0, 1, 2)
views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(indices)})
binary.extend(indices)
views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(png)})
binary.extend(png)
accessors = []
for triangle in range(2):
    for offset, shape in ((0, "VEC3"), (12, "VEC3"), (24, "VEC2"), (32, "VEC2")):
        accessors.append({"bufferView": 0, "byteOffset": triangle * 120 + offset,
                          "componentType": 5126, "count": 3, "type": shape})
    accessors.append({"bufferView": 1, "byteOffset": triangle * 6,
                      "componentType": 5123, "count": 3, "type": "SCALAR"})
primitives = []
for i in (0, 5):
    primitives.append({"attributes": {"POSITION": i, "NORMAL": i + 1,
                                     "TEXCOORD_0": i + 2, "TEXCOORD_1": i + 3},
                       "indices": i + 4, "material": 0})
document = {"asset": {"version": "2.0"}, "buffers": [{"byteLength": len(binary)}],
            "bufferViews": views, "accessors": accessors, "meshes": [{"primitives": primitives}],
            "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0,
            "images": [{"bufferView": 2, "mimeType": "image/png"}],
            "textures": [{"source": 0, "sampler": 0}],
            "samplers": [{"magFilter": 9728, "minFilter": 9728, "wrapS": 33071, "wrapT": 33648}],
            "materials": [{"doubleSided": True, "pbrMetallicRoughness": {"baseColorTexture": {"index": 0, "texCoord": 1},
                                                    "baseColorFactor": [.5, 1, 1, 1],
                                                    "metallicFactor": 0}}]}


def save(name, doc):
    encoded = json.dumps(doc, separators=(",", ":")).encode()
    encoded += b" " * (-len(encoded) % 4)
    payload = bytes(binary) + b"\x00" * (-len(binary) % 4)
    data = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(encoded) + 8 + len(payload))
    data += struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
    data += struct.pack("<II", len(payload), 0x004E4942) + payload
    (output / name).write_bytes(data)


save("fixture-uv1-interleaved-two-primitives.glb", document)
document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["texCoord"] = 0
save("fixture-uv0-interleaved-two-primitives.glb", document)
document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["texCoord"] = 1
for primitive in document["meshes"][0]["primitives"]:
    del primitive["attributes"]["TEXCOORD_1"]
save("fixture-required-uv1-missing.glb", document)
document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["texCoord"] = 0
for primitive in document["meshes"][0]["primitives"]:
    del primitive["attributes"]["NORMAL"]
    del primitive["indices"]
save("fixture-nonindexed-generated-normals.glb", document)
(output / "fixture-purpose.json").write_text(json.dumps({
    "syntheticUnitInputs": True, "notRegeneratedRealModelEvidence": True,
    "purpose": "Interleaved UV0/UV1 selection, original factors, multiple primitives, nearest/clamp/mirror/no-mip sampler and native missing-normal/index conversion",
}, indent=2) + "\n")
