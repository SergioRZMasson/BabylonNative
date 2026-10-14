"""Independent SpecGloss equations over real production framebuffer bytes."""

import json
import math
from pathlib import Path
import struct
import sys
import zlib


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def half(value):
    return struct.unpack("<e", struct.pack("<e", value))[0]


def linear(value):
    return value / 12.92 if value <= .04045 else ((value + .055) / 1.055) ** 2.4


def byte(value):
    return min(255, max(0, math.floor(value * 255 + .5)))


def view(document, binary, index):
    info = document["bufferViews"][index]
    start = info.get("byteOffset", 0)
    return binary[start:start + info["byteLength"]]


def png(data):
    width, height = struct.unpack_from(">II", data, 16)
    offset = 8
    compressed = bytearray()
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        if data[offset + 4:offset + 8] == b"IDAT":
            compressed.extend(data[offset + 8:offset + 8 + size])
        offset += size + 12
    rows = zlib.decompress(compressed)
    pixels = bytearray()
    for y in range(height):
        start = y * (width * 4 + 1)
        assert rows[start] == 0, "This oracle accepts only labeled synthetic filter0 PNGs"
        pixels.extend(rows[start + 1:start + 1 + width * 4])
    return width, height, pixels


def sample(document, binary, primitive, info):
    if info is None:
        return [1, 1, 1, 1]
    accessor = document["accessors"][primitive["attributes"][
        "TEXCOORD_" + str(info.get("texCoord", 0))]]
    u, v = struct.unpack_from("<2f", view(document, binary, accessor["bufferView"]),
                              accessor.get("byteOffset", 0))
    image = document["images"][document["textures"][info["index"]]["source"]]
    width, height, pixels = png(view(document, binary, image["bufferView"]))
    x = min(width - 1, int(u * width))
    y = min(height - 1, int(v * height))
    return [value / 255 for value in pixels[(y * width + x) * 4:(y * width + x + 1) * 4]]


def color(document, binary, primitive):
    index = primitive["attributes"].get("COLOR_0")
    if index is None:
        return [1, 1, 1, 1]
    accessor = document["accessors"][index]
    data = view(document, binary, accessor["bufferView"])
    if accessor["componentType"] == 5121:
        return [f32(value / 255) for value in data[:4]]
    return list(struct.unpack_from("<4f", data, accessor.get("byteOffset", 0)))


def expected(document, binary):
    material = document["materials"][0]
    extension = material["extensions"]["KHR_materials_pbrSpecularGlossiness"]
    primitive = document["meshes"][0]["primitives"][0]
    dmap = sample(document, binary, primitive, extension.get("diffuseTexture"))
    smap = sample(document, binary, primitive, extension.get("specularGlossinessTexture"))
    vertex = color(document, binary, primitive)
    diffuse = [f32(value) for value in extension.get("diffuseFactor", [1, 1, 1, 1])]
    specular = [f32(value) for value in extension.get("specularFactor", [1, 1, 1])]
    gloss = f32(extension.get("glossinessFactor", 1)) * smap[3]
    f0 = [specular[i] * linear(smap[i]) for i in range(3)]
    albedo = [diffuse[i] * linear(dmap[i]) * vertex[i] * (1 - max(f0)) for i in range(3)]
    coverage = f32(f32(f32(dmap[3]) * diffuse[3]) * vertex[3])
    mode = material.get("alphaMode", "OPAQUE")
    background = [51, 102, 153, 64]
    if mode == "MASK" and coverage < f32(material.get("alphaCutoff", .5)):
        return background
    occlusion = 1
    if "occlusionTexture" in material:
        info = material["occlusionTexture"]
        occlusion = 1 + (sample(document, binary, primitive, info)[0] - 1) * info.get("strength", 1)
    emissive = material.get("emissiveFactor", [0, 0, 0])
    emap = sample(document, binary, primitive, material.get("emissiveTexture"))
    emissive = [f32(emissive[i]) * linear(emap[i]) for i in range(3)]
    roughness = 1 - gloss
    alpha_g = roughness * roughness + .0005
    # Both source environment and BRDF images are encoded RGBD, as in the pinned Lite loader.
    brdf = [half((value / 255) ** 2.2) for value in (51, 179)]
    cube = [half((value / 255) ** 2.2 / (128 / 255)) for value in (100, 140, 180)]
    irradiance = [.0625, .09375, .125]
    final = []
    for i in range(3):
        reflectance = (1 - f0[i]) * brdf[0] + f0[i] * brdf[1]
        energy = 1 + f0[i] * (1 / max(brdf[1], .001) - 1)
        radiance = cube[i] * (1 - alpha_g) + irradiance[i] * alpha_g
        result = irradiance[i] * albedo[i] * occlusion + radiance * reflectance * energy + emissive[i]
        result = max(0, min(1, (1 - 2 ** (-1.590579 * result * .8)) ** (1 / 2.2)))
        high = result * result * (3 - 2 * result)
        result = result * .8 + high * .2
        if mode == "BLEND":
            result = result * coverage + background[i] / 255 * (1 - coverage)
        final.append(byte(result))
    final.append(byte(coverage + background[3] / 255 * (1 - coverage)) if mode == "BLEND" else 255)
    return final


def main(fixtures, output):
    selected = {
        "mapped-factors", "factor-only-precision", "empty-extension-defaults",
        "factor-boundary-zero", "factor-boundary-one",
        "missing-diffuseFactor", "missing-specularFactor", "missing-glossinessFactor",
        "missing-diffuseTexture", "missing-specularGlossinessTexture",
        "diffuseTexture-uv1", "diffuseTexture-explicit-uv0",
        "specularGlossinessTexture-uv1", "specularGlossinessTexture-explicit-uv0",
        "both-workflows-spec-gloss-wins", "opaque-mapped-color", "opaque-factor-color",
        "mask-mapped-color", "mask-factor-color", "blend-mapped-color", "blend-factor-color",
        "mask-default-cutoff-equal", "mask-default-cutoff-below", "mask-default-cutoff-above",
        "mask-explicit-zero-cutoff", "normalized-u8-color",
        "common-material-roles", "common-material-roles-uv1", "flat-normal-tangent-ignored",
        "mirrored-double-sided", "same-image-distinct-samplers",
        "allowed-additional-properties", "generic-texture-info-additional-properties",
    }
    checks = []
    for name in sorted(selected):
        data = (fixtures / (name + ".glb")).read_bytes()
        size = struct.unpack_from("<I", data, 12)[0]
        document = json.loads(data[20:20 + size])
        binary = data[28 + size:]
        predicted = expected(document, binary)
        pixels = (output / name / "capture.bgra").read_bytes()
        assert len(pixels) == 64 * 64 * 4
        state = json.loads((output / name / "state.json").read_text())
        assert not state["productionWGSLModified"]
        for x, y in ((31, 31), (32, 32)):
            b, g, r, a = pixels[(y * 64 + x) * 4:(y * 64 + x + 1) * 4]
            actual = [r, g, b, a]
            error = [abs(actual[i] - predicted[i]) for i in range(4)]
            check = {"case": name, "pixel": [x, y], "actual": actual,
                     "expected": predicted, "channelError": error, "tolerance": 2}
            checks.append(check)
            assert max(error) <= 2, check
    assert len(checks) >= 60
    (output / "numeric-specular-glossiness.json").write_text(
        json.dumps({"independentSourceEquationChecks": checks,
                    "productionCompilerOutputModified": False}, indent=2) + "\n", encoding="utf-8")
    print(f"SpecGloss independent production RGBA equations: {len(checks)} checks passed")


if __name__ == "__main__":
    main(Path(sys.argv[1]), Path(sys.argv[2]))
