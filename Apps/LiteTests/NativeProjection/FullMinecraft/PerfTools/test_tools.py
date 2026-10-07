import importlib.util
import json
import random
import re
from pathlib import Path
import unittest

import prepare


NATIVE = Path(__file__).resolve().parents[5]
COMPILER = Path(r"E:\Github\bblitec-native-babylon-layer")
BUILD = NATIVE / "build/lite-c99/perf-deep-dive"


class OverlayTests(unittest.TestCase):
    def test_all_recorded_sources_are_still_unchanged(self):
        for manifest in (BUILD / "Native/PerfOverlay/overlay-manifest.json",
                         BUILD / "SdlOverlay/overlay-manifest.json"):
            records = json.loads(manifest.read_text())
            self.assertGreater(len(records), 5)
            for record in records:
                self.assertEqual(prepare.hashlib.sha256(
                    Path(record["source"]).read_bytes()).hexdigest(), record["sourceSha256"])
                self.assertEqual(prepare.hashlib.sha256(
                    Path(record["overlay"]).read_bytes()).hexdigest(), record["overlaySha256"])

    def test_anchor_refusals_are_explicit(self):
        with self.assertRaisesRegex(ValueError, "Expected 1 anchors"):
            prepare.change("unrelated", "required", "replacement")

    def test_qualified_gpu_profile_is_unchanged(self):
        source = NATIVE / "Apps/LiteTests/NativeProjection/FullMinecraft/Source/NativeHost.cpp"
        self.assertIn("init.profile = true;", source.read_text())
        self.assertNotIn("LITE_PERF", source.read_text())

    def test_core_overlays_keep_original_algorithm_default(self):
        text = (BUILD / "Native/PerfOverlay/Core/Source/Engine.cpp").read_text()
        self.assertIn("lite_perf_grouping_cache()", text)
        self.assertIn("for (size_t j = 0; j < s->memberCount; ++j)", text)
        self.assertIn("qsort(draws, count, sizeof(*draws), drawCompare)", text)

    def test_native_controls_are_opt_in(self):
        text = (BUILD / "Native/PerfOverlay/Host/NativeHost.cpp").read_text()
        self.assertIn("if (!LitePerf::gcOff)", text)
        self.assertIn("if (!LitePerf::hudOff)", text)
        self.assertIn("if (LitePerf::queue3)", text)
        guarded = (BUILD / "Validation/PerfOverlay/Host/NativeHost.cpp").read_text()
        self.assertIn("Draw-state hashing belongs outside benchmarks.", guarded)

    def test_exact_draw_guard_qualification(self):
        folder = BUILD / "GuardQualification180"
        summary = json.loads((folder / "summary.json").read_text())
        self.assertTrue(summary["nativeStateExact"])
        self.assertTrue(summary["sdlSceneImageAndRenderJsonExact"])
        for candidate in ("regex", "retained", "grouping", "retained-grouping"):
            self.assertTrue(all(image["exact"] for image in
                                summary["comparisons"][candidate].values()))
            guard = json.loads((folder / candidate / "guard.json").read_text())
            self.assertEqual(len(guard["drawStateHashes"]), 180)

    def test_same_frame_grouping_preserves_hidden_and_explicit_orders(self):
        generator = random.Random(1337)
        for _ in range(200):
            materials = {index: generator.choice((True, False)) for index in range(8)}
            members = [
                {"material": generator.randrange(8), "disposed": generator.random() < 0.1,
                 "mesh": generator.random() > 0.1, "visible": generator.random() > 0.2,
                 "owned": generator.random() > 0.1,
                 "order": generator.choice((None, -10, 0, 50, 100, 200, 1000)),
                 "depth": generator.uniform(-1000, 1000)}
                for _ in range(generator.randrange(1, 90))
            ]
            draws = []
            for index, member in enumerate(members):
                if member["disposed"] or not all(member[key] for key in ("mesh", "visible", "owned")):
                    continue
                blending = materials[member["material"]]
                order = member["order"]
                draws.append({"material": member["material"], "transparent": blending,
                              "order": order if order is not None else 200 if blending else 100,
                              "group": index, "sequence": index, "depth": member["depth"]})
            reference, candidate = ([dict(draw) for draw in draws] for _ in range(2))

            def aggregate(draw):
                for index, member in enumerate(members):
                    if member["disposed"] or not member["mesh"]:
                        continue
                    if member["material"] != draw["material"]:
                        continue
                    order = member["order"] if member["order"] is not None else 100
                    draw["order"] = min(draw["order"], order)
                    draw["group"] = min(draw["group"], index)

            for draw in reference:
                if not draw["transparent"]:
                    aggregate(draw)
            for index, draw in enumerate(candidate):
                if draw["transparent"]:
                    continue
                previous = next((item for item in candidate[:index]
                                 if not item["transparent"] and
                                 item["material"] == draw["material"]), None)
                if previous is not None:
                    draw["order"], draw["group"] = previous["order"], previous["group"]
                else:
                    aggregate(draw)
            self.assertEqual(reference, candidate)

    def test_guarded_validation_has_same_frozen_core_algorithms(self):
        for name in ("Engine.cpp", "Nodes.cpp", "ShaderMaterial.cpp"):
            frozen = (BUILD / "Native/PerfOverlay/Core/Source" / name).read_text()
            guarded = (BUILD / "Validation/PerfOverlay/Core/Source" / name).read_text()
            guarded = re.sub(r"\s*lite_perf_guard\(.*?\);", "", guarded, flags=re.S)
            compact = lambda value: re.sub(r"\s+", "", value)
            self.assertEqual(compact(frozen), compact(guarded))

    def test_long_guard_and_images_are_verified(self):
        folder = BUILD / "GuardQualification1380"
        summary = json.loads((folder / "summary.json").read_text())
        self.assertTrue(summary["nativeStateExact"])
        for candidate in ("default", "retained-grouping"):
            guard = json.loads((folder / candidate / "guard.json").read_text())
            self.assertEqual(len(guard["drawStateHashes"]), 1380)
            self.assertTrue(all(image["exact"] for image in
                                summary["comparisons"][candidate].values()))


if __name__ == "__main__":
    unittest.main()
