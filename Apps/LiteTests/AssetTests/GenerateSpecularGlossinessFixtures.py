"""Original synthetic inputs only; never reads, rewrites or derives user assets."""

import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib


SG = "KHR_materials_pbrSpecularGlossiness"
output = Path(sys.argv[1]).resolve()
output.mkdir(parents=True, exist_ok=True)
binary = bytearray()
views = []
cases = []


def png(width, height, pixels):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data)))
    rows = b"".join(b"\0" + bytes(pixels[y * width * 4:(y + 1) * width * 4])
                    for y in range(height))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def view(data):
    binary.extend(b"\0" * (-len(binary) % 4))
    views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(data)})
    binary.extend(data)
    return len(views) - 1


streams = [
    ([-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0], "VEC3", 4),
    ([0, 0, 1] * 4, "VEC3", 4), ([.25, .25] * 4, "VEC2", 4),
    ([.75, .75] * 4, "VEC2", 4), ([1, 0, 0, 1] * 4, "VEC4", 4),
    ([.5, .75, .25, .5] * 4, "VEC4", 4), ([-2, 0, 0, 2, 0, 0], "VEC3", 2),
    ([.25, .25, .75, .25, .75, .75, .25, .75], "VEC2", 4),
]
accessors = [{"bufferView": view(struct.pack("<" + "f" * len(values), *values)),
              "componentType": 5126, "count": count, "type": shape}
             for values, shape, count in streams]
accessors[0].update(min=[-1, -1, 0], max=[1, 1, 0])
accessors.append({"bufferView": view(struct.pack("<6H", 0, 1, 2, 0, 2, 3)),
                  "componentType": 5123, "count": 6, "type": "SCALAR"})
accessors.append({"bufferView": view(bytes([128, 192, 64, 128] * 4)),
                  "componentType": 5121, "normalized": True, "count": 4, "type": "VEC4"})
pixels = [
    [64, 128, 192, 64, 192, 64, 128, 128, 128, 192, 64, 192, 224, 96, 32, 255],
    [32, 160, 224, 64, 224, 96, 32, 128, 160, 32, 96, 192, 96, 224, 160, 255],
    [128, 128, 255, 17, 191, 128, 238, 37, 128, 191, 238, 113, 64, 128, 238, 229],
    [32, 73, 211, 11, 96, 181, 23, 59, 160, 17, 139, 151, 224, 251, 67, 239],
    [16, 64, 128, 23, 128, 16, 64, 79, 64, 128, 16, 157, 192, 96, 32, 241],
    [255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 64, 255, 255, 0, 0],
]
images = [{"bufferView": view(png(2, 2, values)), "mimeType": "image/png"} for values in pixels]
material = {"doubleSided": True, "extensions": {SG: {
    "diffuseFactor": [.375, .625, .875, .7312345678901234],
    "specularFactor": [.125, .375, .625], "glossinessFactor": .6875,
    "diffuseTexture": {"index": 0}, "specularGlossinessTexture": {"index": 1}}}}
primitive = {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2,
                            "TEXCOORD_1": 3, "TANGENT": 4}, "indices": 8, "material": 0}
base = {"asset": {"version": "2.0", "generator": "synthetic-spec-gloss-contract-fixtures-v1"},
        "extensionsUsed": [SG], "extensionsRequired": [SG],
        "buffers": [{"byteLength": len(binary)}], "bufferViews": views, "accessors": accessors,
        "images": images, "textures": [{"source": i, "sampler": 0} for i in range(6)],
        "samplers": [{"minFilter": 9728, "magFilter": 9728, "wrapS": 33071, "wrapT": 33071}],
        "materials": [material], "meshes": [{"primitives": [primitive]}],
        "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0}


def changed():
    return copy.deepcopy(base)


def extension(document):
    return document["materials"][0]["extensions"][SG]


def factor_only(document):
    extension(document).pop("diffuseTexture", None)
    extension(document).pop("specularGlossinessTexture", None)
    for key in ("images", "textures", "samplers"):
        document.pop(key, None)
    return document


def save(name, document, parser="BL_OK", core="BL_OK", no_bin=False, text=None):
    encoded = (json.dumps(document, separators=(",", ":"), allow_nan=False)
               if text is None else text).encode()
    encoded += b" " * (-len(encoded) % 4)
    payload = bytes(binary) + b"\0" * (-len(binary) % 4)
    length = 20 + len(encoded) if no_bin else 28 + len(encoded) + len(payload)
    data = struct.pack("<5I", 0x46546c67, 2, length, len(encoded), 0x4e4f534a) + encoded
    if not no_bin:
        data += struct.pack("<2I", len(payload), 0x004e4942) + payload
    (output / (name + ".glb")).write_bytes(data)
    cases.append({"id": name, "file": name + ".glb", "syntheticOnly": True,
                  "sha256": hashlib.sha256(data).hexdigest(),
                  "expectedProvider5": parser, "expectedCore5": core})


save("mapped-factors", base)
save("factor-only-precision", factor_only(changed()))
d = factor_only(changed())
extension(d).clear()
save("empty-extension-defaults", d)
for label, value in (("zero", 0), ("one", 1)):
    d = factor_only(changed())
    extension(d).update(diffuseFactor=[value] * 4, specularFactor=[value] * 3,
                        glossinessFactor=value)
    save("factor-boundary-" + label, d)
for field in ("diffuseFactor", "specularFactor", "glossinessFactor"):
    d = changed()
    del extension(d)[field]
    save("missing-" + field, d)
for role in ("diffuseTexture", "specularGlossinessTexture"):
    d = changed()
    del extension(d)[role]
    save("missing-" + role, d)
    for channel, suffix in ((0, "-explicit-uv0"), (1, "-uv1")):
        d = changed()
        extension(d)[role]["texCoord"] = channel
        save(role + suffix, d)
    for label, info, status in (
        ("bad-index", {"index": 6}, "BL_INVALID_ARGUMENT"),
        ("unsupported-uv2", {"index": 0, "texCoord": 2}, "BL_UNSUPPORTED"),
        ("unsupported-transform", {"index": 0, "extensions": {
            "KHR_texture_transform": {"offset": [.125, .25]}}}, "BL_UNSUPPORTED"),
    ):
        d = changed()
        extension(d)[role] = info
        save(label + "-" + role, d, core=status)
    d = changed()
    extension(d)[role]["texCoord"] = 1
    del d["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_1"]
    save("missing-" + role + "-uv1", d, core="BL_UNSUPPORTED")
for mode in ("OPAQUE", "MASK", "BLEND"):
    for mapped in (True, False):
        d = changed() if mapped else factor_only(changed())
        d["materials"][0]["alphaMode"] = mode
        d["meshes"][0]["primitives"][0]["attributes"]["COLOR_0"] = 5
        save(mode.lower() + ("-mapped-color" if mapped else "-factor-color"), d)
for label, alpha in (("equal", .5), ("below", .4999), ("above", .5001)):
    d = factor_only(changed())
    extension(d)["diffuseFactor"][3] = alpha
    d["materials"][0]["alphaMode"] = "MASK"
    save("mask-default-cutoff-" + label, d)
d = changed()
d["materials"][0].update(alphaMode="MASK", alphaCutoff=0)
save("mask-explicit-zero-cutoff", d)
d = changed()
d["materials"][0]["pbrMetallicRoughness"] = {
    "baseColorFactor": [1, 0, 0, .125], "metallicFactor": .875,
    "roughnessFactor": .125, "baseColorTexture": {"index": 5}}
save("both-workflows-spec-gloss-wins", d)
d = changed()
d["materials"][0].update(normalTexture={"index": 2, "scale": .375},
                         occlusionTexture={"index": 3, "strength": .625},
                         emissiveTexture={"index": 4}, emissiveFactor=[.125, .25, .375])
save("common-material-roles", d)
for role in ("normalTexture", "occlusionTexture", "emissiveTexture"):
    d["materials"][0][role]["texCoord"] = 1
save("common-material-roles-uv1", d)
d = changed()
extension(d)["specularGlossinessTexture"]["index"] = 0
d["materials"][0].update(normalTexture={"index": 0}, occlusionTexture={"index": 0},
                         emissiveTexture={"index": 0}, emissiveFactor=[.125, .25, .375])
save("same-texture-all-roles", d)
d = changed()
d["samplers"].append({"minFilter": 9987, "magFilter": 9729, "wrapS": 10497, "wrapT": 33648})
d["textures"][1] = {"source": 0, "sampler": 1}
save("same-image-distinct-samplers", d)
d = changed()
d["meshes"][0]["primitives"][0]["attributes"]["COLOR_0"] = 9
save("normalized-u8-color", d)
d = changed()
d["materials"][0]["normalTexture"] = {"index": 2}
del d["meshes"][0]["primitives"][0]["attributes"]["NORMAL"]
save("flat-normal-tangent-ignored", d)
d = changed()
d["materials"][0]["normalTexture"] = {"index": 2}
del d["meshes"][0]["primitives"][0]["attributes"]["TANGENT"]
d["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"] = 7
save("normal-cotangent", d)
d = changed()
d["nodes"][0]["scale"] = [-1, 1, 1]
save("mirrored-double-sided", d)
d = changed()
d["extensionsUsed"].append("EXT_mesh_gpu_instancing")
d["extensionsRequired"].append("EXT_mesh_gpu_instancing")
d["nodes"][0]["extensions"] = {"EXT_mesh_gpu_instancing": {"attributes": {"TRANSLATION": 6}}}
save("instanced", d)
d = changed()
d["materials"][0]["normalTexture"] = {"index": 2}
plain = copy.deepcopy(primitive)
colored = copy.deepcopy(primitive)
colored["attributes"]["COLOR_0"] = 5
flat = copy.deepcopy(primitive)
flat["attributes"].pop("NORMAL")
d["meshes"] = [{"primitives": [p]} for p in (plain, colored, flat)]
d["nodes"] = [{"mesh": i, "translation": [(i - 1) * 3, 0, 0]} for i in range(3)]
d["scenes"][0]["nodes"] = [0, 1, 2]
save("shared-sg-material-three-geometry-variants", d)
d["materials"].append({"doubleSided": True, "pbrMetallicRoughness": {
    "baseColorTexture": {"index": 0}, "metallicRoughnessTexture": {"index": 1},
    "metallicFactor": .125, "roughnessFactor": .625}})
d["meshes"] += copy.deepcopy(d["meshes"])
for mesh in d["meshes"][3:]:
    mesh["primitives"][0]["material"] = 1
d["nodes"] = [{"mesh": i, "translation": [(i % 3 - 1) * 3, (i // 3) * 3, 0]} for i in range(6)]
d["scenes"][0]["nodes"] = list(range(6))
save("mixed-workflows-shared-geometry-variants", d)
for field, malformed in (("diffuseFactor", (None, [1, 1, 1], [1, 1, 1, True], "white")),
                         ("specularFactor", (None, [1, 1], [1, 1, "1"], 1)),
                         ("glossinessFactor", (None, True, "1", [1]))):
    for i, value in enumerate(malformed):
        d = changed()
        extension(d)[field] = value
        save("bad-schema-" + field + "-" + str(i), d, "BL_INVALID_ARGUMENT", "not-called")
    for label, value in (("negative", -.001), ("over-one", 1.001)):
        d = changed()
        extension(d)[field] = (value if field == "glossinessFactor"
                              else [value] + [1] * (3 if field == "diffuseFactor" else 2))
        save("bad-range-" + field + "-" + label, d, core="BL_INVALID_ARGUMENT")
for i, value in enumerate((None, [], True, "invalid")):
    d = changed()
    d["materials"][0]["extensions"][SG] = value
    save("bad-extension-object-" + str(i), d, "BL_INVALID_ARGUMENT", "not-called")
d = changed()
extension(d)["extras"] = {"synthetic": True}
extension(d)["futureNonRenderingProperty"] = {"arbitrary": "ignored"}
save("allowed-additional-properties", d)
d = changed()
extension(d)["diffuseTexture"]["scale"] = "irrelevant-additional-property"
extension(d)["specularGlossinessTexture"]["strength"] = {"irrelevant": True}
save("generic-texture-info-additional-properties", d)
for extension_name in ("MSFT_lod", "KHR_materials_clearcoat"):
    d = changed()
    d["extensionsUsed"].append(extension_name)
    save("unsupported-" + extension_name, d, core="BL_UNSUPPORTED")
d = changed()
d["extensionsUsed"] = ["KHR_materials_PBRSpecularGlossiness"]
d["extensionsRequired"] = []
save("noncanonical-used-canonical-payload", d, core="BL_UNSUPPORTED")
for required in (False, True):
    d = changed()
    d["extensionsUsed"] = []
    if not required:
        d["extensionsRequired"] = []
    save("bad-used-declaration-" + str(required), d, core="BL_INVALID_ARGUMENT")
d = changed()
d["skins"] = [{"joints": [0]}]
d["nodes"][0]["skin"] = 0
save("still-unsupported-skin", d, core="BL_UNSUPPORTED")
d = changed()
save("bad-nonfinite-number", d, "BL_INVALID_ARGUMENT", "not-called",
     text=json.dumps(d).replace('"glossinessFactor": 0.6875', '"glossinessFactor": 1e1000'))
d = {"asset": base["asset"], "extensionsUsed": [SG], "materials": [{
    "extensions": {SG: {"diffuseFactor": [.25, .5, .75, .5]}}}],
     "scenes": [{"nodes": []}], "scene": 0}
save("empty-no-bin-unused-sg", d, no_bin=True)

lighting = output / "lighting"
lighting.mkdir(exist_ok=True)
(lighting / "brdf-constant.png").write_bytes(png(2, 2, [51, 179, 0, 255] * 4))
gradient = [c for y in range(16) for x in range(16) for c in (16 + 8 * x, 64 + 4 * y, 0, 255)]
(lighting / "brdf-lut.png").write_bytes(png(16, 16, gradient))
for name in ("constant", "roughness-mips"):
    blob = bytearray()
    entries = []
    for mip in range(4):
        width = 8 >> mip
        pixel = ([100, 140, 180, 128] if name == "constant" else
                 ([200, 80, 40, 128], [80, 200, 40, 128],
                  [40, 80, 200, 128], [160, 120, 60, 128])[mip])
        for _ in range(6):
            encoded = png(width, width, pixel * (width * width))
            entries.append({"position": len(blob), "length": len(encoded)})
            blob.extend(encoded)
    polynomial = {key: ([.0625, .09375, .125] if key in ("xx", "yy", "zz") else [0, 0, 0])
                  for key in ("x", "y", "z", "xx", "yy", "zz", "yz", "zx", "xy")}
    manifest = {"version": 1, "width": 8, "imageType": "image/png",
                "irradiance": polynomial, "specular": {"mipmaps": entries}}
    data = bytes([0x86, 0x16, 0x87, 0x96, 0xf6, 0xd6, 0x96, 0x36])
    data += json.dumps(manifest, separators=(",", ":")).encode() + b"\0" + blob
    (lighting / (name + ".env")).write_bytes(data)
(output / "fixture-manifest.json").write_text(json.dumps({
    "allSynthetic": True, "derivedFromPrivateModels": False, "fixtureCount": len(cases),
    "fixtures": cases}, indent=2) + "\n", encoding="utf-8")
print(f"Generated {len(cases)} original synthetic SpecGloss fixtures")
