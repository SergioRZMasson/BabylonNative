"""Labeled synthetic geometry; independent byte/numeric oracle, never real-model regeneration."""
import copy
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import zlib


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def png(pixel):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data)))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(b"\0" + bytes(pixel))) + chunk(b"IEND", b""))


class Asset:
    def __init__(self):
        self.binary = bytearray()
        self.doc = {"asset": {"version": "2.0"}, "buffers": [{"byteLength": 0}],
                    "bufferViews": [], "accessors": [], "meshes": [], "nodes": [],
                    "scenes": [{"nodes": []}], "scene": 0}

    def view(self, data, stride=None):
        self.binary.extend(b"\0" * (-len(self.binary) % 4))
        view = {"buffer": 0, "byteOffset": len(self.binary), "byteLength": len(data)}
        if stride is not None:
            view["byteStride"] = stride
        self.doc["bufferViews"].append(view)
        self.binary.extend(data)
        return len(self.doc["bufferViews"]) - 1

    def accessor(self, values, shape, kind=5126, normalized=False, sparse=False,
                 zero_base=False, overlay_type=5121):
        components = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[shape]
        width, fmt = {5121: (1, "B"), 5123: (2, "H"), 5125: (4, "I"),
                      5126: (4, "f")}[kind]
        count = len(values) // components
        value = {"componentType": kind, "count": count, "type": shape}
        if normalized:
            value["normalized"] = True
        packed = struct.pack("<" + fmt * len(values), *values)
        if not zero_base:
            stride = (components * width + 3) & ~3
            if shape != "SCALAR" and components * width != stride:
                data = b"".join(packed[i * components * width:(i + 1) * components * width] +
                                b"\xe7" * (stride - components * width) for i in range(count))
                value["bufferView"] = self.view(data, stride)
            else:
                value["bufferView"] = self.view(packed)
        if sparse:
            replaced = list(range(count)) if zero_base else [0, count - 1]
            index_fmt = {5121: "B", 5123: "H", 5125: "I"}[overlay_type]
            index_bytes = struct.pack("<" + index_fmt * len(replaced), *replaced)
            replacement = b"".join(packed[i * components * width:(i + 1) * components * width]
                                   for i in replaced)
            value["sparse"] = {
                "count": len(replaced),
                "indices": {"bufferView": self.view(index_bytes), "componentType": overlay_type},
                "values": {"bufferView": self.view(replacement)}
            }
        self.doc["accessors"].append(value)
        return len(self.doc["accessors"]) - 1

    def image(self, pixel):
        self.doc.setdefault("samplers", [{"magFilter": 9728, "minFilter": 9728}])
        images = self.doc.setdefault("images", [])
        images.append({"bufferView": self.view(png(pixel)), "mimeType": "image/png"})
        textures = self.doc.setdefault("textures", [])
        textures.append({"source": len(images) - 1, "sampler": 0})
        return len(textures) - 1


def resolve(doc, binary, index):
    a = doc["accessors"][index]
    count = a["count"]
    comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
    width, fmt = {5121: (1, "B"), 5123: (2, "H"), 5125: (4, "I"), 5126: (4, "f")}[a["componentType"]]
    result = [0] * (count * comps)
    if "bufferView" in a:
        v = doc["bufferViews"][a["bufferView"]]
        stride = v.get("byteStride", comps * width)
        offset = v.get("byteOffset", 0) + a.get("byteOffset", 0)
        for i in range(count):
            result[i * comps:(i + 1) * comps] = struct.unpack_from(
                "<" + fmt * comps, binary, offset + i * stride)
    if "sparse" in a:
        s = a["sparse"]
        iv = doc["bufferViews"][s["indices"]["bufferView"]]
        vv = doc["bufferViews"][s["values"]["bufferView"]]
        iw, infmt = {5121: (1, "B"), 5123: (2, "H"), 5125: (4, "I")}[s["indices"]["componentType"]]
        io = iv.get("byteOffset", 0) + s["indices"].get("byteOffset", 0)
        vo = vv.get("byteOffset", 0) + s["values"].get("byteOffset", 0)
        for i in range(s["count"]):
            target = struct.unpack_from("<" + infmt, binary, io + i * iw)[0]
            result[target * comps:(target + 1) * comps] = struct.unpack_from(
                "<" + fmt * comps, binary, vo + i * comps * width)
    if a.get("normalized", False):
        reciprocal = 1 / (255 if width == 1 else 65535)
        result = [f32(v * reciprocal) for v in result]
    return result


def triangle_list(mode, indices):
    if mode == 4:
        return list(indices)
    if mode == 5:
        return [indices[k] for i in range(len(indices) - 2)
                for k in (i, i + 1 + i % 2, i + 2 - i % 2)]
    return [indices[k] for i in range(len(indices) - 2) for k in (i + 1, i + 2, 0)]


def generated_normals(positions, indices):
    normals = [0.0] * len(positions)
    for face in range(0, len(indices), 3):
        corners = indices[face:face + 3]
        a, b, c = [positions[i * 3:i * 3 + 3] for i in corners]
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
             u[0] * v[1] - u[1] * v[0]]
        for corner in corners:
            for j in range(3):
                normals[corner * 3 + j] = f32(normals[corner * 3 + j] + n[j])
    for i in range(0, len(normals), 3):
        length = math.sqrt(sum(v * v for v in normals[i:i + 3]))
        if length:
            normals[i:i + 3] = [f32(v / length) for v in normals[i:i + 3]]
    return normals


def geometry_oracle(doc, binary, primitive):
    attrs = primitive["attributes"]
    streams = {name: resolve(doc, binary, index) for name, index in attrs.items()}
    vertices = len(streams["POSITION"]) // 3
    source_indices = resolve(doc, binary, primitive["indices"]) if "indices" in primitive else list(range(vertices))
    indices = triangle_list(primitive.get("mode", 4), source_indices)
    if "NORMAL" not in streams:
        streams["NORMAL"] = generated_normals(streams["POSITION"], indices)
    if "TEXCOORD_0" not in streams:
        streams["TEXCOORD_0"] = [0.0] * (vertices * 2)
    if "COLOR_0" in streams and doc["accessors"][attrs["COLOR_0"]]["type"] == "VEC3":
        rgb = streams["COLOR_0"]
        streams["COLOR_0"] = [v for i in range(vertices) for v in (*rgb[i * 3:i * 3 + 3], 1.0)]
    return {"streams": streams, "indices": indices, "flatNormal": "NORMAL" not in attrs}


def model(kind=5126, rgb=False, sparse=False, zero=False, mode=4, indexed=True,
          normal=True, mapped=False, alpha="OPAQUE", color_alpha=.5, image_alpha=128,
          cutoff=.5, mirror=False, folded=False, index_type=5123, overlay_type=5121):
    asset = Asset()
    positions = [-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, .6 if folded else 0]
    uvs = [0, 0, 1, 0, 1, 1, 0, 1]
    if mode == 5 and not indexed:
        order = [0, 1, 3, 2]
        positions = [positions[i * 3 + j] for i in order for j in range(3)]
        uvs = [uvs[i * 2 + j] for i in order for j in range(2)]
    p = asset.accessor(positions, "VEC3", sparse=sparse, zero_base=zero,
                       overlay_type=overlay_type)
    asset.doc["accessors"][p]["min"] = [-1, -1, 0]
    asset.doc["accessors"][p]["max"] = [1, 1, f32(.6) if folded else 0]
    attributes = {"POSITION": p, "TEXCOORD_0": asset.accessor(
        uvs, "VEC2", sparse=sparse, zero_base=zero, overlay_type=overlay_type)}
    if normal:
        attributes["NORMAL"] = asset.accessor(
            [0, 0, 1] * 4, "VEC3", sparse=sparse, zero_base=zero, overlay_type=overlay_type)
    colors = [.25, .5, .75] + ([] if rgb else [color_alpha])
    if kind != 5126:
        maximum = 255 if kind == 5121 else 65535
        colors = [int(math.floor(v * maximum + .5)) for v in colors]
    attributes["COLOR_0"] = asset.accessor(
        colors * 4, "VEC3" if rgb else "VEC4", kind, kind != 5126, sparse, zero, overlay_type)
    if mapped:
        attributes["TANGENT"] = asset.accessor([1, 0, 0, -1] * 4, "VEC4")
        attributes["TEXCOORD_1"] = asset.accessor(uvs, "VEC2", sparse=sparse, zero_base=zero)
    primitive = {"attributes": attributes, "material": 0, "mode": mode}
    if indexed:
        connectivity = [0, 1, 2, 0, 2, 3] if mode == 4 else [0, 1, 3, 2] if mode == 5 else [0, 1, 2, 3]
        primitive["indices"] = asset.accessor(connectivity, "SCALAR", index_type,
                                              sparse=sparse, zero_base=zero,
                                              overlay_type=overlay_type)
    material = {"doubleSided": True, "alphaMode": alpha, "alphaCutoff": cutoff,
                "pbrMetallicRoughness": {
                    "metallicFactor": 0, "roughnessFactor": 0,
                    "baseColorFactor": [.5, .75, .25, .8],
                    "baseColorTexture": {"index": asset.image([128, 192, 64, image_alpha])}}}
    if mapped:
        material["normalTexture"] = {"index": asset.image([128, 128, 255, 255]), "texCoord": 1}
    asset.doc["materials"] = [material]
    asset.doc["meshes"] = [{"primitives": [primitive]}]
    asset.doc["nodes"] = [{"mesh": 0, "scale": [-1 if mirror else 1, 1, 1]}]
    asset.doc["scenes"][0]["nodes"] = [0]
    return asset


def write_glb(path, doc, binary):
    doc["buffers"][0]["byteLength"] = len(binary)
    js = json.dumps(doc, separators=(",", ":"), allow_nan=False).encode()
    js += b" " * (-len(js) % 4)
    payload = bytes(binary) + b"\0" * (-len(binary) % 4)
    content = struct.pack("<III", 0x46546c67, 2, 28 + len(js) + len(payload))
    content += struct.pack("<II", len(js), 0x4e4f534a) + js
    content += struct.pack("<II", len(payload), 0x004e4942) + payload
    path.write_bytes(content)
    return hashlib.sha256(content).hexdigest()


def main(output, real=None):
    output.mkdir(parents=True, exist_ok=True)
    cases = []

    def emit(name, asset, **labels):
        oracles = [geometry_oracle(asset.doc, asset.binary, primitive)
                   for mesh in asset.doc["meshes"] for primitive in mesh["primitives"]]
        colors = oracles[0]["streams"].get("COLOR_0", [1, 1, 1, 1])[:4]
        entry = {"name": name, "synthetic": True, "expected": "BL_OK",
                 "geometry": oracles, "color": colors, "document": asset.doc, **labels}
        entry["sha256"] = write_glb(output / (name + ".glb"), asset.doc, asset.binary)
        cases.append(entry)

    for kind, fmt in ((5126, "float"), (5121, "unorm8"), (5123, "unorm16")):
        for rgb in (False, True):
            emit("color-" + fmt + ("-rgb" if rgb else "-rgba"), model(kind=kind, rgb=rgb),
                 pixelOracle=True)
    for mode in ("OPAQUE", "MASK", "BLEND"):
        emit("color-alpha-" + mode.lower(), model(alpha=mode, cutoff=.2), pixelOracle=True)
    emit("color-mask-discard", model(alpha="MASK", cutoff=.201), pixelOracle=True)
    emit("color-opaque-zero", model(color_alpha=0, image_alpha=0), pixelOracle=True)
    for value, name in ((.4999, "below"), (.5, "equal"), (.5001, "above")):
        a = model(alpha="MASK", color_alpha=value, image_alpha=255)
        a.doc["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"][3] = 1
        emit("color-mask-" + name, a, pixelOracle=True)
    for overlay_type, name in ((5121, "u8"), (5123, "u16"), (5125, "u32")):
        emit("sparse-base-" + name, model(sparse=True, overlay_type=overlay_type), pixelOracle=True)
        emit("sparse-zero-" + name, model(kind=5123, sparse=True, zero=True,
                                        alpha="BLEND", overlay_type=overlay_type), pixelOracle=True)
    for mode, name in ((5, "strip"), (6, "fan")):
        for kind, kind_name in ((5121, "u8"), (5123, "u16"), (5125, "u32")):
            emit(name + "-" + kind_name, model(mode=mode, index_type=kind), pixelOracle=True)
        emit(name + "-nonindexed", model(mode=mode, indexed=False), pixelOracle=True)
        emit(name + "-sparse", model(mode=mode, sparse=True), pixelOracle=True)
        emit(name + "-mirrored", model(mode=mode, mirror=True), pixelOracle=True)
        for mirror in (False, True):
            for front in (False, True):
                a = model(mode=mode, mirror=mirror)
                a.doc["materials"][0]["doubleSided"] = False
                if front:
                    a.doc["nodes"][0]["rotation"] = [0, 1, 0, 0]
                emit(name + "-single-" + ("front" if front else "back") +
                     ("-mirror" if mirror else ""), a, pixelOracle=True,
                     culled=not front)
    for mode, name, connectivity in (
            (5, "strip-degenerates", [0, 1, 1, 2, 3]),
            (4, "list-degenerates-reference", [0, 1, 1, 1, 2, 1, 1, 2, 3])):
        a = model(mode=mode)
        p = a.doc["meshes"][0]["primitives"][0]
        p["indices"] = a.accessor(connectivity, "SCALAR", 5123)
        emit(name, a, degenerates=True)
    a = model(rgb=True, sparse=True, zero=True)
    c = a.doc["accessors"][a.doc["meshes"][0]["primitives"][0]["attributes"]["COLOR_0"]]
    c["sparse"]["count"] = 2
    c["sparse"]["indices"]["bufferView"] = a.view(bytes([0, 3]))
    c["sparse"]["values"]["bufferView"] = a.view(struct.pack("<6f", *([.25, .5, .75] * 2)))
    emit("sparse-partial-zero-rgb", a, fetchProbe=True)
    a = model(kind=5121, rgb=True, sparse=True)
    attrs = a.doc["meshes"][0]["primitives"][0]["attributes"]
    streams = {key: resolve(a.doc, a.binary, attrs[key]) for key in attrs}
    packed = bytearray()
    for i in range(4):
        packed.extend(struct.pack("<8f", *(streams["POSITION"][i*3:i*3+3] +
                      streams["NORMAL"][i*3:i*3+3] + streams["TEXCOORD_0"][i*2:i*2+2])))
        packed.extend(bytes(round(v * 255) for v in streams["COLOR_0"][i*3:i*3+3]))
        packed.append(0xe7)
    shared_view = a.view(packed, 36)
    for key, offset in (("POSITION", 0), ("NORMAL", 12), ("TEXCOORD_0", 24), ("COLOR_0", 32)):
        a.doc["accessors"][attrs[key]]["bufferView"] = shared_view
        a.doc["accessors"][attrs[key]]["byteOffset"] = offset
    emit("interleaved-all-sparse", a, pixelOracle=True)
    a = model()
    attrs = a.doc["meshes"][0]["primitives"][0]["attributes"]
    a.doc["accessors"][attrs["POSITION"]]["normalized"] = False
    p = a.doc["meshes"][0]["primitives"][0]
    p["indices"] = a.accessor([0, 1, 2, 0, 2, 3], "SCALAR", 5125)
    a.doc["accessors"][p["indices"]]["normalized"] = False
    a.doc["bufferViews"][a.doc["accessors"][p["indices"]]["bufferView"]]["target"] = 34962
    emit("explicit-normalized-false-target-hint", a, pixelOracle=True)
    for mirror in (False, True):
        suffix = "-mirror" if mirror else ""
        for normal in (False, True):
            emit("folded-" + ("authored" if normal else "flat") + suffix,
                 model(normal=normal, folded=True, mirror=mirror), flatProbe=True)
        for tangents in (False, True):
            a = model(normal=False, mapped=True, folded=True, mirror=mirror)
            if not tangents:
                del a.doc["meshes"][0]["primitives"][0]["attributes"]["TANGENT"]
            emit("folded-map-" + ("tangent" if tangents else "fallback") + suffix, a,
                 flatProbe=True)
    for reversed_order in (False, True):
        a = model()
        primitive = a.doc["meshes"][0]["primitives"][0]
        variants = []
        for normal in (True, False):
            for color in (True, False):
                p = copy.deepcopy(primitive)
                if not normal:
                    del p["attributes"]["NORMAL"]
                if not color:
                    del p["attributes"]["COLOR_0"]
                variants.append(p)
        a.doc["meshes"] = [{"primitives": [p]} for p in variants]
        a.doc["nodes"] = [{"mesh": i, "translation": [.65 if i % 2 == 0 else -.65,
                              .65 if i < 2 else -.65, 0], "scale": [.35, .35, .35]}
                           for i in range(4)]
        a.doc["scenes"][0]["nodes"] = list(reversed(range(4))) if reversed_order else list(range(4))
        emit("shared-four-variants" + ("-reverse" if reversed_order else ""), a, shared=True)
    a = model(kind=5123, mapped=True, sparse=True)
    attrs = a.doc["meshes"][0]["primitives"][0]["attributes"]
    for key, kind in (("TEXCOORD_0", 5121), ("TEXCOORD_1", 5123)):
        values = resolve(a.doc, a.binary, attrs[key])
        maximum = 255 if kind == 5121 else 65535
        attrs[key] = a.accessor([round(v * maximum) for v in values], "VEC2", kind,
                                normalized=True, sparse=True, zero_base=True)
    translation = a.accessor([0, 0, 0], "VEC3", sparse=True, zero_base=True)
    a.doc["extensionsUsed"] = ["EXT_mesh_gpu_instancing"]
    a.doc["extensionsRequired"] = ["EXT_mesh_gpu_instancing"]
    a.doc["nodes"][0]["extensions"] = {"EXT_mesh_gpu_instancing": {
        "attributes": {"TRANSLATION": translation}}}
    emit("all-inputs-instanced-sparse", a, pixelOracle=True)
    a = model()
    translation = a.accessor([0, 0, 0], "VEC3")
    view = a.doc["bufferViews"][a.doc["accessors"][translation]["bufferView"]]
    view["byteStride"] = 16
    a.doc["extensionsUsed"] = ["EXT_mesh_gpu_instancing"]
    a.doc["extensionsRequired"] = ["EXT_mesh_gpu_instancing"]
    a.doc["nodes"][0]["extensions"] = {"EXT_mesh_gpu_instancing": {
        "attributes": {"TRANSLATION": translation}}}
    emit("stride16-count1-instance12-view", a, pixelOracle=True,
         expectedInstanceMatrices=[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
    bad = copy.deepcopy(a)
    bad.doc["accessors"][translation]["count"] = 2
    sha = write_glb(output / "bad-stride16-count2-instance12-view.glb", bad.doc, bad.binary)
    cases.append({"name": "bad-stride16-count2-instance12-view", "synthetic": True,
                  "sha256": sha, "expected": "BL_INVALID_ARGUMENT"})
    a = model()
    unused = a.accessor([1.25, -2.5, 3.75], "VEC3")
    a.doc["bufferViews"][a.doc["accessors"][unused]["bufferView"]]["byteStride"] = 16
    emit("stride16-count1-unused12-view", a, pixelOracle=True)
    a = model()
    position = a.doc["meshes"][0]["primitives"][0]["attributes"]["POSITION"]
    values = resolve(a.doc, a.binary, position)
    interleaved = b"".join(struct.pack("<3f", *values[i*3:i*3+3]) +
                            (b"\xe7" * 4 if i < 3 else b"") for i in range(4))
    a.doc["accessors"][position]["bufferView"] = a.view(interleaved, 16)
    emit("stride16-four-positions60-view-no-tail-padding", a, pixelOracle=True)
    a = Asset()
    position = a.accessor([0, 0, 0] * 3, "VEC3", zero_base=True)
    a.doc["accessors"][position]["min"] = [-99, -99, -99]
    a.doc["accessors"][position]["max"] = [99, 99, 99]
    a.doc["meshes"] = [{"primitives": [{"attributes": {"POSITION": position}}]}]
    a.doc["nodes"] = [{"mesh": 0}]
    a.doc["scenes"][0]["nodes"] = [0]
    a.view(b"\0\0\0\0")
    emit("implicit-zero-position-bounds-exception", a, degenerateOnly=True)
    for name, mutate in (
        ("bad-sparse-duplicate", lambda a: a.binary.__setitem__(
            a.doc["bufferViews"][a.doc["accessors"][0]["sparse"]["indices"]["bufferView"]]["byteOffset"] + 1, 0)),
        ("bad-sparse-count", lambda a: a.doc["accessors"][0]["sparse"].__setitem__("count", 5)),
        ("bad-sparse-target", lambda a: a.doc["bufferViews"][
            a.doc["accessors"][0]["sparse"]["values"]["bufferView"]].__setitem__("target", 34962)),
        ("bad-sparse-stride", lambda a: a.doc["bufferViews"][
            a.doc["accessors"][0]["sparse"]["values"]["bufferView"]].__setitem__("byteStride", 16)),
        ("bad-missing-base-offset", lambda a: a.doc["accessors"][0].__setitem__("byteOffset", 0)),
        ("bad-final-bounds", lambda a: a.doc["accessors"][0].__setitem__("max", [0, 0, 0])),
        ("bad-position-bound-presence", lambda a: a.doc["accessors"][0].pop("min")),
        ("bad-color-range", lambda a: struct.pack_into("<f", a.binary,
            a.doc["bufferViews"][a.doc["accessors"][a.doc["meshes"][0]["primitives"][0]
                ["attributes"]["COLOR_0"]]["bufferView"]]["byteOffset"] + 16, 1.1)),
        ("unsupported-lines", lambda a: a.doc["meshes"][0]["primitives"][0].__setitem__("mode", 1))):
        a = model(sparse=True, zero=(name == "bad-missing-base-offset"))
        mutate(a)
        sha = write_glb(output / (name + ".glb"), a.doc, a.binary)
        cases.append({"name": name, "synthetic": True, "sha256": sha,
                      "expected": "BL_UNSUPPORTED" if name == "unsupported-lines" else "BL_INVALID_ARGUMENT"})
    if real:
        source = json.loads((real / "SimpleSparseAccessor.gltf").read_text(encoding="utf-8"))
        binary = (real / "SimpleSparseAccessor.bin").read_bytes()
        a = Asset()
        a.doc = copy.deepcopy(source)
        del a.doc["buffers"][0]["uri"]
        a.binary = bytearray(binary)
        emit("SimpleSparseAccessor-container-adaptation", a, synthetic=False,
             licensedReference=True,
             adaptation="Only BIN URI removal, JSON container encoding and GLB padding; original numeric data untouched.",
             originalGltfSha256=hashlib.sha256((real / "SimpleSparseAccessor.gltf").read_bytes()).hexdigest(),
             originalBinSha256=hashlib.sha256(binary).hexdigest())
    lighting = output / "Lighting"
    lighting.mkdir(exist_ok=True)
    encoded = png([0, 0, 0, 255])
    keys = ("x", "y", "z", "xx", "yy", "zz", "yz", "zx", "xy")
    irradiance = {key: [1, 1, 1] if key in ("xx", "yy", "zz") else [0, 0, 0] for key in keys}
    env = {"version": 1, "width": 1, "irradiance": irradiance,
           "specular": {"mipmaps": [{"position": i * len(encoded), "length": len(encoded)} for i in range(6)]}}
    (lighting / "environmentSpecular.env").write_bytes(
        bytes.fromhex("86168796f6d69636") + json.dumps(env, separators=(",", ":")).encode() +
        b"\0" + encoded * 6)
    (lighting / "brdf-lut.png").write_bytes(encoded)
    (output / "geometry-cases.json").write_text(json.dumps({
        "syntheticFixturesOnly": True, "independentByteNumericOracle": True,
        "productionPixelLighting": "Unit constant irradiance; black BRDF/specular. RGB oracle includes actual PBR dielectric diffuse, exposure/tone/gamma/contrast.",
        "cases": cases}, indent=2) + "\n", encoding="utf-8")
    print("Generated", len(cases), "labeled geometry cases")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None)
