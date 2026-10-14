"""Independent arithmetic over unmodified production WGSL's actual GPU color AND alpha."""
import json
from pathlib import Path
import struct
import sys

fixtures = Path(sys.argv[1])
captures = Path(sys.argv[2])
receipt = Path(sys.argv[3])
background = [153, 102, 51, 64]  # BGRA8 clear (.2,.4,.6,.25).
points = [(20, 20), (44, 20), (20, 44), (44, 44)]
image_alphas = [255, 128, 127, 0]  # Root X reflection and +Y world/up versus image top-down.
checks = []


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def pixels(name, locations=points):
    data = (captures / (name + ".bgra")).read_bytes()
    assert len(data) == 64 * 64 * 4
    return [list(data[(y * 64 + x) * 4:(y * 64 + x) * 4 + 4]) for x, y in locations]


def near(actual, expected, label, tolerance=2):
    errors = [abs(a - b) for a, b in zip(actual, expected)]
    assert max(errors) <= tolerance, (label, actual, expected, errors)
    checks.append({"check": label, "actualBgra": actual, "expectedBgra": expected,
                   "maxErrorLevels": max(errors)})


def over(source, destination, alpha):
    return [round(source[i] * alpha + destination[i] * (1 - alpha)) for i in range(3)] + [
        round(255 * alpha + destination[3] * (1 - alpha))]


cases = json.loads((fixtures / "alpha-cases.json").read_text())
for case in cases:
    name = case["name"]
    state = json.loads((captures / (name + ".json")).read_text())
    imported = state["materials"][0]
    assert bool(imported["blend"]) == (case["mode"] == "BLEND"), name
    assert bool(imported["mask"]) == (case["mode"] == "MASK"), name
    assert imported["cutoff"] == float(f'{case["cutoff"]:.6g}'), name
    effective_alpha = 1 if case["mode"] == "OPAQUE" or case["imageAlpha"] is not None else case["factor"]
    assert imported["alpha"] == float(f'{effective_alpha:.6g}'), name
    assert state["draws"] == 1 and state["trackedTeardownBytes"] == 0
    ref = ("opaque-mirrored-reference" if name in (
               "mask-mirrored", "blend-mirrored", "opaque-mirrored-reference") else
           "opaque-backface-reference" if name in (
               "mask-backface", "blend-backface", "opaque-backface-reference") else
           "opaque-maps-reference" if case["maps"] else
           "opaque-image-reference" if case["imageAlpha"] is not None else
           "opaque-factor-reference")
    actual = pixels(name)
    reference = pixels(ref)
    # Mirroring/back-facing rotation swaps the X-selected authored alpha.
    alphas = [128, 255, 0, 127] if name in (
        "mask-mirrored", "mask-backface", "blend-mirrored", "blend-backface") else image_alphas
    for index, (a, r) in enumerate(zip(actual, reference)):
        alpha = f32(case["factor"])
        if case["imageAlpha"] is not None:
            alpha = f32(alpha * f32(alphas[index] / 255))
        if case["mode"] == "OPAQUE":
            expected = r[:3] + [255]
        elif case["mode"] == "MASK":
            expected = background if alpha < f32(case["cutoff"]) else r[:3] + [255]
        else:
            expected = over(r, background, alpha)
        near(a, expected, name + "/quadrant-" + str(index))

# Ordered overlapping colors use independently rendered opaque production references,
# then the normative source-over arithmetic. These are not substituted/proxy shaders.
near_color = pixels("overlap-reference-near", [(32, 32)])[0]
far_color = pixels("overlap-reference-far", [(32, 32)])[0]
normal_overlap = over(near_color, over(far_color, background, .5), .25)
for name in ("overlap-near-first", "overlap-far-first"):
    near(pixels(name, [(32, 32)])[0], normal_overlap, name)
assert (captures / "overlap-near-first.bgra").read_bytes() == (
    captures / "overlap-far-first.bgra").read_bytes(), "submission order changed ordered GPU output"
no_write = normal_overlap
near(pixels("blend-no-depth-write", [(32, 32)])[0], no_write,
     "behind-geometry submitted second survives transparent depth test (no depth write)")
mixed_background = pixels("mixed-background-reference", [(32, 32)])[0]
near(pixels("mixed-phases", [(32, 32)])[0],
     over(near_color, over(far_color, mixed_background, .5), .25), "mixed-opaque-mask-blend-phases")
near(pixels("opaque-depth-blocker", [(32, 32)])[0], mixed_background,
     "opaque depth blocks both transparent layers")
near(pixels("mask-depth-pass", [(32, 32)])[0], near_color,
     "surviving MASK writes depth before a farther opaque surface")
near(pixels("mask-depth-discard", [(32, 32)])[0], far_color,
     "discarded MASK writes neither color alpha nor depth")
for name, expected in (
        ("equal-depth-near-first", over(far_color, over(near_color, background, .25), .5)),
        ("equal-depth-far-first", normal_overlap),
        ("equal-depth-orders", normal_overlap)):
    near(pixels(name, [(32, 32)])[0], expected, name + "/order-then-stable-tie")
for name in ("mask-instances", "blend-instances"):
    state = json.loads((captures / (name + ".json")).read_text())
    assert state["draws"] == 1 and state["trackedTeardownBytes"] == 0
    data = (captures / (name + ".bgra")).read_bytes()
    assert any(list(data[i:i + 4]) != background for i in range(0, len(data), 4)), name
for mode in ("mask", "blend"):
    for face in ("front", "back"):
        name = mode + "-single-sided-" + face
        reference = pixels("opaque-single-sided-" + face, [(32, 32)])[0]
        near(pixels(name, [(32, 32)])[0], reference, name + "/preserved-culling")
assert pixels("opaque-single-sided-back", [(32, 32)])[0] == background
assert pixels("opaque-single-sided-front", [(32, 32)])[0] != background
result = json.loads((captures / "result.json").read_text())
receipt.write_text(json.dumps({"passed": True, "actualAlphaFramebuffer": True,
                               "proxyShader": False, "gpu": result,
                               "numericChecks": checks}, indent=2) + "\n")
print("Production GPU alpha numeric checks passed:", len(checks))
