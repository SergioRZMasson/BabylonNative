import unittest
import json
from pathlib import Path
import xml.etree.ElementTree as ET
from unittest.mock import patch

import numpy as np

from harness import benchmark, bootstrap_ratios, compare_rgb, summarize, baseline_parity
from inspect_capture import inspect
from collect_evidence import ctest_summary
from formal_benchmark import present_metrics, baseline_metrics
from prepare_memory_probe import relink
from harness import main as harness_main


class ParityTests(unittest.TestCase):
    def test_baseline_preflight_preserves_animated_registry_capture_frame(self):
        record = {"executable": "baseline.exe",
                  "pose": {"nativeEnvironment": {"BBLITE_SCREENSHOT_FRAME": "120"}}}
        with patch("harness.catalog", return_value=record), patch("harness.verify_inputs"), \
                patch("harness.write"), patch("harness.run", return_value={"exitCode": 1}) as run:
            baseline_parity("unused", 43, "sdl_gpu", "unused")
        environment = run.call_args.args[2]
        self.assertEqual(environment["BBLITE_SCREENSHOT_FRAME"], "120")
        self.assertEqual(environment["BBLITE_MAX_FRAMES"], "121")

    def test_foreground_uses_reference_euclidean_distance_strictly_greater(self):
        reference = np.array([[[0, 0, 0], [3, 4, 0], [4, 4, 0]]], dtype=np.uint8)
        actual = np.array([[[90, 90, 90], [3, 4, 0], [10, 4, 0]]], dtype=np.uint8)
        result = compare_rgb(actual, reference, [0, 0, 0], 5)
        self.assertEqual(result["foregroundPixels"], 1)
        self.assertEqual(result["foregroundMad"], 2)
        self.assertAlmostEqual(result["fullMad"], 92 / 3)

    def test_rejects_extent_mismatch_instead_of_cropping(self):
        with self.assertRaises(ValueError):
            compare_rgb(np.zeros((1, 1, 3)), np.zeros((2, 1, 3)), [0, 0, 0], 30)


class StatisticsTests(unittest.TestCase):
    def test_ratios_are_paired_not_pooled_frames(self):
        result = bootstrap_ratios([2, 20, 200], [1, 10, 100])
        self.assertEqual(result["pairs"], 3)
        self.assertAlmostEqual(result["ratio"], 2)
        self.assertTrue(all(abs(x - 2) < 1e-12 for x in result["ci95"]))

    def test_numerically_fast_runs_do_not_claim_qualified_acceptance(self):
        pairs = [{"native": {"meanMs": 1, "p95Ms": 2},
                  "baseline": {"meanMs": 2, "p95Ms": 4}} for _ in range(5)]
        result = summarize(pairs)
        self.assertEqual(result["timingOnlyVerdict"], "equal-or-faster")
        self.assertEqual(result["verdict"], "unqualified")
        self.assertTrue(result["acceptanceBlockers"])

    def test_missing_pairs_cannot_pass(self):
        self.assertEqual(summarize([{"error": "failed"}])["verdict"], "blocked")

    def test_child_failure_is_an_unsuccessful_batch(self):
        with patch("harness.Path.mkdir"), patch("harness.Path.exists", return_value=False), \
                patch("harness.write"), patch("harness.benchmark_pair", return_value={"error": "child failed"}):
            self.assertIs(benchmark("unused", "unused", [159], "sdl_gpu", 5, "unused"), False)


class CaptureTests(unittest.TestCase):
    def test_keep_targets_does_not_replace_live_binding_with_empty_array(self):
        root = ET.fromstring("""<rdc><chunks>
          <chunk name="ID3D11Device::CreateTexture2D">
            <struct name="Descriptor"><uint name="Width">1280</uint><uint name="Height">720</uint>
              <struct name="SampleDesc"><uint name="Count">4</uint></struct></struct>
            <ResourceId name="pTexture">10</ResourceId></chunk>
          <chunk name="ID3D11Device::CreateRenderTargetView">
            <ResourceId name="pResource">10</ResourceId><ResourceId name="pView">20</ResourceId></chunk>
          <chunk name="ID3D11DeviceContext::OMSetRenderTargets">
            <array name="ppRenderTargetViews"><ResourceId>20</ResourceId></array></chunk>
          <chunk name="ID3D11DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews">
            <enum name="NumRTVs" string="D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL">4294967295</enum>
            <array name="ppRenderTargetViews"/></chunk>
          <chunk name="ID3D11DeviceContext::DrawIndexed" chunkIndex="5">
            <uint name="IndexCount">6</uint></chunk>
        </chunks></rdc>""")
        draw = inspect(root)["draws"][0]
        self.assertEqual(draw["color"][0]["samples"], 4)
        self.assertEqual(draw["indices"], 6)


class EvidenceTests(unittest.TestCase):
    def test_expected_fp_failure_cannot_mask_process_failure_or_unexpected_pass(self):
        argv = ["harness.py", "parity", "--host", "host", "--data", "data",
                "--scene", "200", "--output", "output", "--expect-backend-fp-difference"]
        cases = [
            ({"pass": False, "process": {"exitCode": 0}, "fullMad": .3, "foregroundMad": .4}, 0),
            ({"pass": False, "process": {"exitCode": 2}}, 1),
            ({"pass": True, "process": {"exitCode": 0}, "fullMad": 0, "foregroundMad": 0}, 1),
        ]
        for result, expected in cases:
            with patch("sys.argv", argv), patch("harness.parity", return_value=result), \
                    patch("harness.write"):
                self.assertEqual(harness_main(), expected)

    def test_isolated_relink_redirects_outputs_without_overwriting_original_build(self):
        output = Path("isolated") / "probe.exe"
        original = ["link.exe", "/OUT:original.exe", "/IMPLIB:original.lib",
                    "/PDB:original.pdb", "Core/LiteLayer/LiteLayer.lib"]
        with patch("prepare_memory_probe.subprocess.run") as run:
            command = relink(original, Path("build"), output,
                             {"core\\litelayer\\litelayer.lib": Path("old") / "LiteLayer.lib"})
        self.assertIn("/out:" + str(output), command)
        self.assertIn("/implib:" + str(output.with_suffix(".lib")), command)
        self.assertIn("/pdb:" + str(output.with_suffix(".pdb")), command)
        self.assertIn(str(Path("old") / "LiteLayer.lib"), command)
        self.assertNotIn("/OUT:original.exe", command)
        run.assert_called_once()

    def test_setup_presents_are_not_reported_as_first_correct_source_frame(self):
        events = [
            {"sourceFrame": False, "result": 0, "syncInterval": 0, "endQpc": 20},
            {"sourceFrame": True, "result": -1, "syncInterval": 0, "endQpc": 30},
            {"sourceFrame": True, "result": 0, "syncInterval": 0, "endQpc": 50},
        ]
        value = {"presents": events, "processStartQpc": 10, "qpcFrequency": 1000,
                 "firstPresentMs": 10, "calibrationUncertaintyTicks": 1}
        with patch("pathlib.Path.read_text", return_value=json.dumps(value)):
            result = present_metrics(Path("unused"))
        self.assertEqual(result["firstAnyPresentMs"], 10)
        self.assertEqual(result["firstSourcePresentMs"], 40)

    def test_incomplete_baseline_samples_cannot_qualify(self):
        value = {"samplesMs": [1.0] * 1999, "warmup": 120, "measure": 2000}
        with patch("pathlib.Path.read_text", return_value=json.dumps(value)):
            with self.assertRaises(ValueError):
                baseline_metrics(Path("unused"))

    def test_ctest_summary_handles_both_success_formats_and_missing_evidence(self):
        self.assertEqual(ctest_summary("100% tests passed out of 20"), {"total": 20, "failed": 0})
        self.assertEqual(ctest_summary("73% tests passed, 8 tests failed out of 30"),
                         {"total": 30, "failed": 8})
        self.assertEqual(ctest_summary("not run"), {"total": None, "failed": None})


if __name__ == "__main__":
    unittest.main()
