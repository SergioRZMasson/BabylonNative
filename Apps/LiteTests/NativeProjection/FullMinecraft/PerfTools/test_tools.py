import hashlib
import json
import random
import re
from pathlib import Path
import unittest
from unittest.mock import patch

import prepare


NATIVE = Path(__file__).resolve().parents[5]
BUILD = NATIVE / "build/lite-c99/perf-deep-dive"
FROZEN_INPUTS = BUILD / "HistoricalSources"
BASELINE_COMMIT = "7d9aadb02c63e9e4b08c2f4f4b6cc5122e7ed81f"


def frozen_input(record):
    index = json.loads((FROZEN_INPUTS / "manifest.json").read_text())
    if index["schemaVersion"] != 1 or index["nativeBaselineCommit"] != BASELINE_COMMIT:
        raise ValueError("Unexpected historical source baseline.")
    matches = [entry for entry in index["records"]
               if entry["source"] == record["source"] and
               entry["sourceSha256"] == record["sourceSha256"]]
    if len(matches) != 1:
        raise ValueError(f"Missing unique frozen input: {record['source']}")
    snapshot = (FROZEN_INPUTS / matches[0]["snapshot"]).resolve()
    if not snapshot.is_relative_to(FROZEN_INPUTS.resolve()):
        raise ValueError("Historical source snapshot must remain inside its archive.")
    content = snapshot.read_bytes()
    if hashlib.sha256(content).hexdigest() != record["sourceSha256"]:
        raise ValueError(f"Historical input hash changed: {snapshot}")
    return content


class OverlayTests(unittest.TestCase):
    def test_all_recorded_frozen_sources_and_overlays_are_unchanged(self):
        # Live Core/adapter files may evolve; this suite protects the measured historical inputs.
        measured_files = json.loads((BUILD / "Matched15/manifest.json").read_text())["sourceFiles"]
        for manifest in (BUILD / "Native/PerfOverlay/overlay-manifest.json",
                         BUILD / "SdlOverlay/overlay-manifest.json",
                         BUILD / "Validation/PerfOverlay/overlay-manifest.json"):
            if str(manifest) in measured_files:
                self.assertEqual(hashlib.sha256(manifest.read_bytes()).hexdigest(),
                                 measured_files[str(manifest)])
            records = json.loads(manifest.read_text())
            self.assertGreater(len(records), 5)
            for record in records:
                self.assertEqual(hashlib.sha256(
                    frozen_input(record)).hexdigest(), record["sourceSha256"])
                self.assertEqual(hashlib.sha256(
                    Path(record["overlay"]).read_bytes()).hexdigest(), record["overlaySha256"])

    def test_anchor_refusals_are_explicit(self):
        with self.assertRaisesRegex(ValueError, "Expected 1 anchors"):
            prepare.change("unrelated", "required", "replacement")

    def test_historical_qualified_gpu_profile_is_unchanged(self):
        records = json.loads((BUILD / "Native/PerfOverlay/overlay-manifest.json").read_text())
        source = str(NATIVE / "Apps/LiteTests/NativeProjection/FullMinecraft/Source/NativeHost.cpp")
        record = next(record for record in records if record["source"] == source)
        text = frozen_input(record).decode("utf-8")
        self.assertIn("init.profile = true;", text)
        self.assertNotIn("LITE_PERF", text)

    def test_frozen_measurement_binary_hashes_are_unchanged(self):
        for suite in ("Matched15", "Renderer5"):
            manifest = json.loads((BUILD / suite / "manifest.json").read_text())
            for variant in manifest["variants"]:
                self.assertEqual(hashlib.sha256(
                    Path(variant["executable"]).read_bytes()).hexdigest(),
                    manifest["binaryHashes"][variant["name"]])
        validation = json.loads((BUILD / "validation-provenance.json").read_text())
        self.assertEqual(hashlib.sha256(
            (BUILD / "Validation/LiteMinecraftNative.exe").read_bytes()).hexdigest(),
            validation["validationBinarySha256"])

    def test_missing_historical_input_is_not_replaced_by_live_source(self):
        with self.assertRaisesRegex(ValueError, "Missing unique frozen input"):
            frozen_input({"source": str(Path(__file__)), "sourceSha256": "0" * 64})

    def test_historical_input_never_reads_mutable_live_core(self):
        records = json.loads((BUILD / "Native/PerfOverlay/overlay-manifest.json").read_text())
        record = next(record for record in records
                      if record["source"].endswith(r"\Core\LiteLayer\Source\Engine.cpp"))
        live_source = Path(record["source"]).resolve()
        original_read = Path.read_bytes

        def guarded_read(path):
            if path.resolve() == live_source:
                raise AssertionError("Historical provenance must not read mutable live Core.")
            return original_read(path)

        with patch.object(Path, "read_bytes", guarded_read):
            self.assertEqual(hashlib.sha256(
                frozen_input(record)).hexdigest(), record["sourceSha256"])

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
