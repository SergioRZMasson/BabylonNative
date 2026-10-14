"""Independent arithmetic on real GPU BGRA, not CPU shadows or an empty-image acceptance."""
import json
import math
from pathlib import Path
import struct
import sys


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def byte(value):
    return min(255, max(0, math.floor(value * 255 + .5)))


def srgb(value):
    return value / 12.92 if value <= .04045 else ((value + .055) / 1.055) ** 2.4


def display(linear):
    tone = 1 - 2 ** (-1.590579 * linear * .96 * .8)
    value = min(1, max(0, tone ** (1 / 2.2)))
    high = value * value * (3 - 2 * value)
    return max(0, value * .8 + high * .2)


def rgba_expected(case, color=None):
    color = case["color"] if color is None else color
    material = case["document"]["materials"][0]
    factors = material["pbrMetallicRoughness"]["baseColorFactor"]
    image_alpha = 0 if case["name"] == "color-opaque-zero" else (
        255 if case["name"].startswith("color-mask-") and case["name"] not in
        ("color-mask-discard",) else 128)
    sample = [128, 192, 64]
    rgb = [display(srgb(sample[i] / 255) * f32(factors[i]) * color[i]) for i in range(3)]
    alpha = f32(f32(f32(image_alpha / 255) * f32(factors[3])) * color[3])
    mode = material.get("alphaMode", "OPAQUE")
    background = [51, 102, 153, 64]
    if case.get("culled", False) or (mode == "MASK" and alpha < f32(material.get("alphaCutoff", .5))):
        return background
    if mode != "BLEND":
        return [byte(v) for v in rgb] + [255]
    return [byte(rgb[i] * alpha + (background[i] / 255) * (1 - alpha)) for i in range(3)] + [
        byte(alpha + (background[3] / 255) * (1 - alpha))]


def main(root, fixtures, production="gpu-expanded", report_name="numeric-geometry.json"):
    cases = json.loads((fixtures / "geometry-cases.json").read_text(encoding="utf-8"))["cases"]
    checks = []

    def image(folder, name):
        if folder == "gpu-expanded":
            folder = production
        data = (root / folder / (name + ".bgra")).read_bytes()
        assert len(data) == 64 * 64 * 4
        return data

    def pixel(data, x, y):
        b, g, r, a = data[(y * 64 + x) * 4:(y * 64 + x) * 4 + 4]
        return [r, g, b, a]

    def check(label, actual, expected, tolerance):
        errors = [abs(a - b) for a, b in zip(actual, expected)]
        record = {"label": label, "actual": actual, "expected": expected,
                  "errors": errors, "tolerance": tolerance}
        checks.append(record)
        assert max(errors) <= tolerance, record

    for case in cases:
        if case["expected"] != "BL_OK":
            continue
        name = case["name"]
        data = image("gpu-expanded", name)
        state = json.loads((root / production / (name + ".json")).read_text())
        assert state["trackedTeardownBytes"] == 0 and state["cpuGeometryOracleByteExact"]
        assert not state["productionFragmentOutputModified"]
        assert state["compilerCalls"] == state["compilerReleases"]
        check(name + "/background", pixel(data, 0, 0), [51, 102, 153, 64], 0)
        if case.get("pixelOracle", False):
            check(name + "/productionRGBA", pixel(data, 32, 32), rgba_expected(case), 3)
        if case.get("shared", False):
            assert state["variantMask"] == 15
            for y in (16, 48):
                check(name + "/colored-" + str(y), pixel(data, 16, y), rgba_expected(case), 3)
                check(name + "/colorless-" + str(y), pixel(data, 48, y),
                      rgba_expected(case, [1, 1, 1, 1]), 3)
            assert state["colorLayoutToggleAndRestorationActualGpu"]
            changed = image("gpu-expanded", name + "-updated")
            restored = image("gpu-expanded", name + "-restored")
            for y in (16, 48):
                check(name + "/updated-colorless-" + str(y), pixel(changed, 16, y),
                      rgba_expected(case, [1, 1, 1, 1]), 3)
                check(name + "/updated-colored-" + str(y), pixel(changed, 48, y),
                      rgba_expected(case), 3)
            assert restored == data
        if case.get("degenerateOnly", False):
            assert data == bytes([153, 102, 51, 64]) * (64 * 64)
            assert state["draws"] == 1
            checks.append({"label": name + "/actual-degenerate-draw", "draws": 1,
                           "coverage": 0, "notSilentlyOmitted": True})
        if case.get("pixelOracle", False) and case["document"]["materials"][0]["alphaMode"] == "OPAQUE":
            fetched = image("gpu-fetch", name)
            expected = [byte(v) for v in case["color"]]
            if case.get("culled", False):
                expected = [51, 102, 153, 64]
            check(name + "/actualVertexColorFetch", pixel(fetched, 32, 32), expected, 1)
    for name in ("strip-u8", "strip-u16", "strip-u32", "strip-nonindexed", "strip-sparse",
                 "strip-mirrored", "fan-u8", "fan-u16", "fan-u32", "fan-nonindexed",
                 "fan-sparse", "fan-mirrored"):
        reference = image("gpu-expanded", "color-float-rgba")
        actual = image("gpu-expanded", name)
        assert actual == reference, "actual strip/fan differs from independent list " + name
        checks.append({"label": name + "/list-topology-winding", "allBGRABytesExact": True})
    assert image("gpu-expanded", "strip-degenerates") == image(
        "gpu-expanded", "list-degenerates-reference")
    checks.append({"label": "strip-degenerates/list-reference", "allBGRABytesExact": True})
    assert image("gpu-expanded", "shared-four-variants") == image(
        "gpu-expanded", "shared-four-variants-reverse")
    checks.append({"label": "shared-four-variants/reversed-source-order", "allBGRABytesExact": True})
    partial = pixel(image("gpu-fetch", "sparse-partial-zero-rgb"), 32, 32)
    source_x = -(.5 / 32) * 3 * math.tan(.4)
    weight = (1 - source_x) / 2
    check("partial-sparse-rgb/actual-interpolated-fetch-alpha1", partial,
          [byte(.25 * weight), byte(.5 * weight), byte(.75 * weight), 255], 1)
    face = [1.2, 1.2, -4]
    length = math.sqrt(sum(v * v for v in face))
    slope = [byte(.5 + .5 * v / length) for v in face] + [255]
    mirrored_slope = [255 - slope[0], slope[1], slope[2], 255]
    for suffix, flat_sample, slope_sample, expected_slope in (
            ("", (24, 40), (40, 24), slope),
            ("-mirror", (40, 40), (24, 24), mirrored_slope)):
        flat = image("gpu-geometric", "folded-flat" + suffix)
        authored = image("gpu-geometric", "folded-authored" + suffix)
        check("flat" + suffix + "/first-face", pixel(flat, *flat_sample), [128, 128, 0, 255], 1)
        check("flat" + suffix + "/noncoplanar-face", pixel(flat, *slope_sample), expected_slope, 1)
        check("authored" + suffix + "/normal-not-regenerated", pixel(authored, *slope_sample),
              [128, 128, 0, 255], 1)
        assert pixel(flat, *slope_sample) != pixel(authored, *slope_sample)
        for folder in ("gpu-geometric", "gpu-normal", "gpu-expanded"):
            fallback = image(folder, "folded-map-fallback" + suffix)
            tangent = image(folder, "folded-map-tangent" + suffix)
            assert fallback == tangent, "authored tangent affected missing-NORMAL " + folder + suffix
            checks.append({"label": folder + suffix + "/ignored-authored-tangent", "allBGRABytesExact": True})
        mapped = image("gpu-normal", "folded-map-fallback" + suffix)
        check("mapped-flat" + suffix + "/first-face-orientation", pixel(mapped, *flat_sample),
              [128, 128, 0, 255], 2)
        check("mapped-flat" + suffix + "/noncoplanar-face-orientation",
              pixel(mapped, *slope_sample), expected_slope, 2)
    report = {"independentActualGpuNumericChecks": len(checks), "checks": checks,
              "productionPbrOutputUnmodified": True,
              "diagnosticProbesSeparate": "normal/geometric/fetch replace final output only in test host",
              "rgbOracle": "IEC-sRGB image * F32 factor * linear vertex color once; dielectric diffuse under constant irradiance, exposure/tone/gamma/contrast; actual source-over RGBA",
              "floatAndNormalizedRGBAAndRGBAlpha1": True,
              "opaqueMaskBlendNoRegression": True, "sparseBaseAndZeroAndPartialColors": True,
              "actualVertexFetchAndInterleavedFormat": True, "stripFanMirrorsAndDegenerates": True,
              "flatSourcePresenceAndMappedTangentIgnore": True,
              "sharedFourVariantsAndReversedOrder": True}
    output = root / report_name
    assert not output.exists(), "Preserve accepted numeric receipt"
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Independent actual GPU geometry/color/alpha/winding/flat checks:", len(checks))


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(),
         sys.argv[3] if len(sys.argv) > 3 else "gpu-expanded",
         sys.argv[4] if len(sys.argv) > 4 else "numeric-geometry.json")
