import argparse
import hashlib
import json
import pathlib
from PIL import Image, ImageChops, ImageStat

parser = argparse.ArgumentParser()
parser.add_argument("root", type=pathlib.Path)
parser.add_argument("--frames", type=int, default=180)
args = parser.parse_args()
root = args.root
frame = args.frames
captures = {
    "Native": root / "Native" / f"Qualification{frame}" / f"frame{frame}.png",
    **{name: root / "Qualification" / f"{name}{frame}" / f"frame{frame}.png"
       for name in ["SDL", "StockDawn", "CompatibleBgfx"]},
}
images = {name: Image.open(path).convert("RGB") for name, path in captures.items()}
native = json.loads((root / "Native" / f"Qualification{frame}" / "summary.json").read_text())
assert native["noUi"] and native["hudBackingRenders"] == 0 and native["uiVertices"] == 0
assert native["worldHash"] is None and native["chunks"] is None
assert native["materialCount"] == 8 and native["sceneCount"] == 1
assert native["draws"] > 0 and native["inputEvents"] == 0
assert native["geometryFactories"] > 0 and native["vertices"] > 0 and native["indices"] > 0
states = {}
for name in ["SDL", "StockDawn", "CompatibleBgfx"]:
    state = json.loads((root / "Qualification" / f"{name}{frame}" / "state.json").read_text())
    states[name] = state
    assert state["frame"] == frame
    assert len(state["meshes"]) == native["liveMeshes"]
    assert len(state["materials"]) == native["materialCount"]
    assert len(state["draws"]) == native["draws"]
result = {"nativeActualC99Observation": native, "baselineStateCounts": {},
          "pixelComparisons": {}, "crossBackendPixelIdentityClaim": False}
for name, state in states.items():
    result["baselineStateCounts"][name] = {
        "meshes": len(state["meshes"]), "materials": len(state["materials"]),
        "draws": len(state["draws"]), "camera": state["camera"], "viewport": state["viewport"],
    }
for name, image in images.items():
    assert image.size == (1280, 720)
    assert len(image.getcolors(image.width * image.height)) > 100
    delta = ImageChops.difference(image, images["Native"])
    result["pixelComparisons"][name] = {
        "rgbSha256": hashlib.sha256(image.tobytes()).hexdigest(),
        "nativeReferenceMeanAbsoluteRgbError": sum(ImageStat.Stat(delta).mean) / 3,
        "exactRgb": delta.getbbox() is None,
        "maxAbsoluteRgbError": max(pair[1] for pair in delta.getextrema()),
    }
(root / f"qualification-{frame}.json").write_text(json.dumps(result, indent=2) + "\n")
print("Four real no-UI frame qualifications passed; differences measured, no blanket pixel-parity claim.")
