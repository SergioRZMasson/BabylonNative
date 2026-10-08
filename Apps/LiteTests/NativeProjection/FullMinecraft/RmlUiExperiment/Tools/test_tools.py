import importlib.util
import hashlib
import json
import pathlib
import subprocess
import unittest
import sys

sys.dont_write_bytecode = True

APP = pathlib.Path(__file__).resolve().parents[1]
REPOSITORY = APP.parents[4]
BUILD = REPOSITORY / "build" / "lite-c99" / "rmlui-minecraft" / "NativeToolTests"
spec = importlib.util.spec_from_file_location("platform_adapter", APP / "Tools" / "prepare-platform.py")
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


class HostToolTests(unittest.TestCase):
    def test_platform_has_no_gdi_backing(self):
        adapter.prepare(APP.parent / "Source" / "Platform.cpp", BUILD / "BindingData")
        text = (BUILD / "BindingData" / "PlatformTransport.cpp").read_text()
        for forbidden in ["CreateDIBSection", "CreateCompatibleDC", "UpdateLayeredWindow",
                          "StretchDIBits", "DrawTextW", "LITE_MINECRAFT_NO_UI"]:
            self.assertNotIn(forbidden, text)
        self.assertIn("s_time = s_fixedClock ? s_time + deltaMs : Clock()", text)
        self.assertIn("MinecraftUi::Property", text)
        self.assertIn("MinecraftUi::Append", text)

    def test_platform_preparation_is_retained(self):
        path = BUILD / "BindingData" / "PlatformTransport.cpp"
        adapter.prepare(APP.parent / "Source" / "Platform.cpp", BUILD / "BindingData")
        initial = path.stat().st_mtime_ns
        adapter.prepare(APP.parent / "Source" / "Platform.cpp", BUILD / "BindingData")
        self.assertEqual(initial, path.stat().st_mtime_ns)

    def test_canonical_copy_and_css_authority(self):
        common = BUILD.parent / "Common"
        subprocess.run(["node", str(APP / "Tools" / "prepare.mjs"),
                        str(common / "Generated"), str(BUILD)], check=True, capture_output=True)
        record = json.loads((BUILD / "canonical-inputs.json").read_text())
        self.assertEqual(len(record["records"]), 27)
        self.assertFalse(record["userCppEdited"])
        self.assertFalse(record["worldObserverInjected"])
        self.assertFalse(record["engineProfilingTagsRemoved"])
        self.assertEqual(len(record["cssBoundaryRestorations"]), 2)
        for item in record["records"]:
            original = common / "Generated" / item["file"]
            local = BUILD / "UserSources" / item["file"]
            self.assertEqual(original.read_bytes(), local.read_bytes())
            self.assertEqual(hashlib.sha256(local.read_bytes()).hexdigest(), item["sha256"])

    def test_entry_glue_not_in_user_files(self):
        main = BUILD / "UserSources" / "main.cpp"
        if not main.exists():
            self.test_canonical_copy_and_css_authority()
        self.assertNotIn("MinecraftApplication", main.read_text())
        self.assertNotIn("SetWorldProbe", main.read_text())
        self.assertIn("main=MinecraftApplication", (APP / "CMakeLists.txt").read_text())


if __name__ == "__main__":
    unittest.main()
