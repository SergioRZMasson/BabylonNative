"""Refuse missing UI, stale evidence, private-world fabrication and unmatched timing samples."""
import json
import math
import pathlib
import sys
import re

receipt = pathlib.Path(sys.argv[1]).resolve()
run = json.loads(receipt.read_text())
assert run["uiEnabled"] and run["noUi"] is False
assert run["hudBackingRenders"] == 0
assert run["hudDraws"] > 0 and run["uiLiveGeometry"] > 0 and run["uiLiveTextures"] > 0
assert run["seed"] == 1337 and run["radius"] == 6
assert run["width"] == 1280 and run["height"] == 720 and run["sampleCount"] in (1, 4)
assert not run["vsync"]
assert run["privateWorldObserved"] is False and run["worldHash"] is None and run["chunks"] is None
assert run["materialCount"] == 8
if run["cpuSamplesMs"]:
    for field in ["cpuSamplesMs", "totalHostCpuSamplesMs", "coreCpuSamplesMs",
                  "rmlUpdateSamplesMs", "rmlRenderSamplesMs", "presenterSamplesMs",
                  "gpuSamplesMs", "gpuFrames", "drawSamples", "uiDrawSamples",
                  "uiGeometryCompileSamples", "uiTextureCreateSamples", "uiUploadSamples",
                  "gpuTimerFrequencies", "rendererSubmitSamplesMs", "bgfxWaitRenderSamplesMs",
                  "bgfxWaitSubmitSamplesMs"]:
        assert len(run[field]) == run["frames"], field
    assert all(value > 0 and math.isfinite(value) for value in run["cpuSamplesMs"])
    assert all(value > 0 for value in run["uiDrawSamples"])
    assert run["coreCpuSamplesMs"] != run["totalHostCpuSamplesMs"], "Core-only total substitution."
    unique = {}
    repeats = 0
    unavailable = 0
    for frame, value in zip(run["gpuFrames"], run["gpuSamplesMs"]):
        if value == -1:
            unavailable += 1
        elif frame in unique:
            repeats += 1
        else:
            assert value > 0 and math.isfinite(value)
            unique[frame] = value
    print(f"GPU observations: {len(unique)} unique, {repeats} repeats, {unavailable} unavailable.")
else:
    assert run["captures"] >= 2
    for frame in [1, run["frames"]]:
        assert (receipt.parent / f"frame{frame}.png").is_file()
    assert (receipt.parent / "dom.txt").is_file()
    assert run["validationOnlyGeometryObservation"]
    assert run["liveMeshes"] > 0
    dom = (receipt.parent / "dom.txt").read_text(encoding="utf-8")
    elements = []
    for match in re.finditer(r'^\d+ ([^\n ]+) text="((?:[^"\\]|\\.)*)"\n((?:  [^\n]*\n)*)',
                             dom, re.MULTILINE):
        properties = dict(line.strip().split(":", 1)
                          for line in match[3].splitlines() if ":" in line)
        elements.append({"tag": match[1], "text": match[2], "properties": properties})
    assert any(element["text"] == "60 FPS" for element in elements)
    icons = [element for element in elements
             if element["properties"].get("decorator", "").startswith('image("minecraft-image-')]
    assert len(icons) == 10
    selected = [element for element in icons
                if element["properties"].get("transform") == "scale(1.12)"]
    assert len(selected) == 1 and selected[0]["properties"]["border-color"] == "#fff"
    if run["inputEvents"]:
        assert run["inputEvents"] in (37, 41), "Unexpected legacy Win32 replay protocol."
        assert selected[0]["properties"]["decorator"] == 'image("minecraft-image-1" cover)'
        assert any(element["tag"] == "pre" and "Voxel Sandbox" in element["text"] and
                   element["properties"].get("display") == "block" for element in elements)
        assert any("Click to play" in element["text"] and
                   element["properties"].get("display") == "none" for element in elements)
        if run["audioOutputAvailable"]:
            assert run["audioNonzeroSamples"] > 0
    else:
        assert selected[0]["properties"]["decorator"] == 'image("minecraft-image-0" cover)'
    if run["fileDialogs"]:
        assert run["fileDialogs"] == 2 and run["inputEvents"] == 41
        world = json.loads((receipt.parent / "world.voxelsave.json").read_text())
        assert world["seed"] == 1337 and world["v"] == 1
        assert set(world) == {"v", "seed", "time", "player", "edits"}
        assert any(element["text"] == "World loaded" for element in elements)
        if run["frames"] >= 500:
            assert any(element["text"] == "World loaded" and
                       element["properties"].get("opacity") == "0" for element in elements)
    if run["uiOnlyUnderwaterTestOverride"]:
        assert any("box-shadow" in element["properties"] and
                   element["properties"].get("opacity") == "1" for element in elements)
    if run["inputEvents"] == 0 and run["frames"] in (180, 1380):
        assert run["liveMeshes"] == (397 if run["frames"] == 180 else 403)
if len(sys.argv) > 2:
    reference = json.loads(pathlib.Path(sys.argv[2]).read_text())
    assert run["frames"] == reference["frame"] + 1
    assert run["liveMeshes"] == reference["scene"]["meshCount"]
    for field in ["position", "target"]:
        actual = run["cameraPosition" if field == "position" else "cameraTarget"]
        assert all(abs(first - second) < 1e-10
                   for first, second in zip(actual, reference["camera"][field])), field
print("Real retained UI, canonical workload, combined captures/timing guards passed.")
