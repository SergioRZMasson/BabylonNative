from pathlib import Path
import unittest

import prepare


SOURCE = Path(__file__).resolve().parents[5] / "Core/LiteLayer/Source"


class RewriteOverlayTests(unittest.TestCase):
    def test_current_modular_anchors_are_executed_and_stale_inputs_refused(self):
        transforms = {
            "Engine.cpp": prepare.rewritten_engine,
            "RenderOrder.cpp": prepare.rewritten_order,
            "ShaderMaterial.cpp": prepare.rewritten_shader_values,
            "ShaderPipeline.cpp": prepare.rewritten_shader_pipeline,
            "Nodes.cpp": prepare.nodes,
        }
        for name, transform in transforms.items():
            with self.subTest(name=name):
                original = (SOURCE / name).read_text()
                projected = transform(original)
                self.assertNotEqual(original, projected)
                self.assertIn('#include "PerfC.h"', projected)
                with self.assertRaisesRegex(ValueError, "Expected 1 anchors"):
                    transform("not the current source")

    def test_grouping_is_shipping_and_frame_local_not_an_experimental_branch(self):
        source = (SOURCE / "RenderOrder.cpp").read_text()
        projected = prepare.rewritten_order(source)
        self.assertNotIn("lite_perf_grouping_cache", projected)
        self.assertIn("memset(s->groupScratch", source)
        self.assertIn("materialId >> 32", source)
        self.assertEqual(projected.count("lite_perf_add(9, checkpoint)"), 2)
        self.assertEqual(projected.count("lite_perf_add(8, checkpoint)"), 1)
        self.assertLess(projected.index("lite_perf_add(9, checkpoint)"),
                        projected.index("L_Draw* draws = (L_Draw*)s->drawScratch"))
        self.assertLess(projected.index("lite_perf_add(8, checkpoint)"),
                        projected.rindex("lite_perf_add(9, checkpoint)"))

    def test_guard_covers_actual_sorted_draws_and_uniform_bytes(self):
        engine = prepare.rewritten_engine((SOURCE / "Engine.cpp").read_text())
        pipeline = prepare.rewritten_shader_pipeline((SOURCE / "ShaderPipeline.cpp").read_text())
        self.assertLess(engine.index("l_sortDraws(s, count)"),
                        engine.index("lite_perf_guard(&draws[i].order"))
        self.assertIn("lite_perf_guard(&draws[i].group", engine)
        self.assertLess(pipeline.index("lite_perf_guard(u->bytes"),
                        pipeline.index("bgfx::setUniform(u->handle"))


if __name__ == "__main__":
    unittest.main()
