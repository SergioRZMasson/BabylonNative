import json
import math
from pathlib import Path
from collections import Counter
import sys

root = Path(sys.argv[1]).resolve()
tag = sys.argv[2] if len(sys.argv) > 2 else "v2"
proofs = {}
normal_pixels = [(128, 230, 204), (128, 26, 204), (230, 128, 204), (26, 128, 204)]
occlusion_pixels = [32, 224, 96, 160]
emissive_pixels = [(64, 128, 192), (192, 64, 128), (128, 192, 64), (192, 128, 64)]


def byte(value):
    return max(0, min(255, int(math.floor(value * 255 + .5))))


def srgb(value):
    value /= 255
    return value / 12.92 if value <= .04045 else ((value + .055) / 1.055) ** 2.4


def encoded_normal(pixel, scale, transform):
    n = [(pixel[0] / 255 * 2 - 1) * scale,
         (pixel[1] / 255 * 2 - 1) * scale, pixel[2] / 255 * 2 - 1]
    length = math.sqrt(sum(value * value for value in n))
    rgb = [byte(n[i] / length * transform[i] * .5 + .5) for i in range(3)]
    return tuple(reversed(rgb)) + (255,)


def histogram(path):
    data = path.read_bytes()
    assert len(data) == 1280 * 720 * 4
    counts = Counter(tuple(data[i:i + 4]) for i in range(0, len(data), 4))
    # Interior texel plateaus, not MSAA edge mixtures or the clear color.
    background = counts.most_common(1)[0][0]
    return {pixel: count for pixel, count in counts.items()
            if pixel != background and count >= 10000}


for mode in ("normal", "occlusion", "emissive", "emissive-alpha"):
    folder = root / ("oracle-" + tag + "-" + mode)
    inputs = json.loads((root / ("oracle-" + tag + "-" + mode + "-inputs.json")).read_text())
    events = [json.loads(line) for line in (folder / "events.jsonl").read_text().splitlines()]
    assert len(events) == len(inputs["fixtures"])
    values = []
    for index, name in enumerate(inputs["fixtures"]):
        assert events[index]["success"] and events[index]["visible"] and events[index]["draws"] == 1
        if mode == "normal":
            scale = 0 if name == "normal-scale-0" else 1 if name == "normal-scale-1" else .375
            # Importer reflects X. These +Z planes face away from the source camera at -Z.
            # Mirrored X changes T; authored w=-1 changes B; the Y-rotated back test faces it.
            transform = ((-1, -1, -1) if name in ("normal-cotangent", "normal-mirrored-node")
                         else (1, 1, -1) if name in ("normal-handedness-minus", "normal-double-sided-back")
                         else (1, -1, -1))
            expected = [encoded_normal(pixel, scale, transform) for pixel in normal_pixels]
            if name == "normal-uv1":
                expected = expected[-1:]
            if name == "normal-cotangent-degenerate-uv":
                expected = [(0, 128, 128, 255)]
        elif mode == "occlusion":
            strength = (0 if name == "occlusion-strength-0"
                        else 1 if name == "occlusion-strength-1" else .625)
            expected = [(byte(1 - strength + strength * value / 255),) * 3 + (255,)
                        for value in occlusion_pixels]
            if name == "occlusion-uv1":
                expected = expected[-1:]
        elif mode == "emissive":
            factors = ((0, 0, 0) if name in ("emissive-factor-zero", "emissive-factor-missing")
                       else (1, 1, 1) if name == "emissive-factor-one" else (.25, .5, .75))
            expected = [tuple(reversed([byte(srgb(pixel[i]) * factors[i]) for i in range(3)])) + (255,)
                        for pixel in emissive_pixels]
            if name == "emissive-uv1":
                expected = expected[-1:]
            if name == "emissive-factor-only":
                expected = [tuple(reversed([byte(value) for value in factors])) + (255,)]
        else:
            expected = [(value,) * 3 + (255,) for value in (31, 79, 167, 223)]
        expected = set(expected)
        actual = histogram(folder / ("drop-" + str(index) + ".bgra"))
        assert actual, (mode, name, "no interior material values")
        for pixel in actual:
            assert any(max(abs(a - b) for a, b in zip(pixel, candidate)) <= 1
                       for candidate in expected), (mode, name, pixel, expected)
        for pixel in expected:
            assert any(max(abs(a - b) for a, b in zip(pixel, candidate)) <= 1
                       for candidate in actual), (mode, name, pixel, actual)
        values.append({"fixture": name, "expectedBGRA": sorted(expected),
                       "actualInteriorBGRA": [{"pixel": pixel, "count": count}
                                              for pixel, count in actual.items()],
                       "checked": True})
    teardown = json.loads((folder / "teardown.json").read_text())
    assert teardown["runtimeDecoderAllocatorLiveBytes"] == 0
    proofs[mode] = values

horizon_file = root / ("oracle-" + tag + "-horizon") / "drop-0.bgra"
if horizon_file.exists():
    data = horizon_file.read_bytes()
    radius = math.sqrt(8) * 1.5
    horizon = []
    for x in (569, 710):
        for y in (289, 430):
            world_x = (2 * (x + .5) / 1280 - 1) * radius * (1280 / 720) * math.tan(.4)
            world_y = (1 - 2 * (y + .5) / 720) * radius * math.tan(.4)
            uv_x = (1 - world_x) / 2
            uv_y = (1 + world_y) / 2
            pixel = normal_pixels[int(uv_y * 2) * 2 + int(uv_x * 2)]
            n = [pixel[0] / 255 * 2 - 1, -(pixel[1] / 255 * 2 - 1),
                 -(pixel[2] / 255 * 2 - 1)]
            length = math.sqrt(sum(value * value for value in n))
            n = [value / length for value in n]
            d = [world_x, world_y, radius]
            length = math.sqrt(sum(value * value for value in d))
            d = [value / length for value in d]
            dot = sum(d[i] * n[i] for i in range(3))
            reflected_z = d[2] - 2 * dot * n[2]
            temporary = max(0, min(1, 1 - 1.1 * reflected_z))
            expected = byte(temporary * temporary)
            actual = tuple(data[(y * 1280 + x) * 4:(y * 1280 + x) * 4 + 4])
            assert all(abs(channel - expected) <= 1 for channel in actual[:3]), (x, y, actual, expected)
            assert expected < 250, "Horizon oracle must detect a hardcoded1 fallback"
            horizon.append({"pixelXY": [x, y], "expectedGrey": expected, "actualBGRA": actual})
    proofs["sourceIblHorizonOcclusion"] = horizon

(root / ("numeric-material-" + tag + ".json")).write_text(json.dumps({
    "actualGpu": True, "productionMaterialBodyOnlyFinalOutputEncoded": True,
    "maximumEncodedUnormTolerance": 1,
    "toleranceMeaning": "Float32 arithmetic and RGBA8 UNORM rounding versus independent double arithmetic, not a changed scene35 framebuffer gate.",
    "normalBasis": "Pinned source authored tangent/cotangent expressions; signed map RGB, scale, raw w, importer X mirror, ordinary mirrored node, source double-sided face correction.",
    "occlusion": "Linear red channel and mix(1,r,strength), independent UV.",
    "emissive": "IEC sRGB RGB only, linear alpha, default zero and explicit factors, independent UV and factor without image.",
    "proofs": proofs,
}, indent=2) + "\n")
print("Actual GPU asymmetric normals/scale/basis/UV, occlusion/strength/UV, emissive factors/sRGB/UV/linear alpha passed")
