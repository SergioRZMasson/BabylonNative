import argparse
import hashlib
import json
import pathlib
import re
import shutil

parser = argparse.ArgumentParser()
parser.add_argument("checkout", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
args = parser.parse_args()
assert "no-ui-comparison" in args.output.parts
source = args.checkout / "native"
destination = args.output / "native"
destination.mkdir(parents=True, exist_ok=True)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def write(file, data):
    if isinstance(data, str):
        data = data.encode("utf-8")
    if not file.exists() or file.read_bytes() != data:
        file.write_bytes(data)

modified = {"src/pal_sdl_gpu.cpp", "src/pal_dawn.cpp", "src/pal_ui_rml.cpp",
            "src/pal_gpu_frame.cpp"}
records = []
for folder in ["src", "include", "shaders", "notices", "patches"]:
    for file in sorted((source / folder).rglob("*")):
        if not file.is_file():
            continue
        relative = file.relative_to(source)
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if relative.as_posix() not in modified:
            write(target, file.read_bytes())
        records.append({"file": relative.as_posix(), "sourceSha256": sha(file.read_bytes())})
for file in sorted(source.glob("*.cmake")):
    shutil.copyfile(file, destination / file.name)
for file in sorted(source.glob("*.json")):
    shutil.copyfile(file, destination / file.name)
shutil.copyfile(source / "CMakeLists.txt", destination / "CMakeLists.txt")
(args.output / "upstream").mkdir(parents=True, exist_ok=True)
for file in sorted((args.checkout / "upstream").glob("*.json")):
    shutil.copyfile(file, args.output / "upstream" / file.name)

# Preserve the real standard engine; gate only on-screen host UI operations.
patches = []
for name in ["pal_sdl_gpu.cpp", "pal_dawn.cpp"]:
    file = destination / "src" / name
    text = (source / "src" / name).read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    stack = []
    gates = set()
    for index, line in enumerate(lines):
        if re.match(r"\s*#\s*if(?:def|ndef)?\b", line):
            stack.append(index)
        elif re.match(r"\s*#\s*endif\b", line):
            stack.pop()
        if any(token in line for token in [
            "update_ui_rml_runtime(",
            "record_ui_rml_frame(",
            "render_sprite_ui_sdl_gpu_frame(",
            "render_sprite_ui_dawn_frame(",
            "UiSdlReadableSurface::present(",
            "if (ui_after_capture_copy)",
        ]):
            ui = next((opening for opening in reversed(stack)
                       if lines[opening].strip() == "#if BBLITE_HAS_UI && !BBLITE_WORKERS"), None)
            if ui is None:
                raise ValueError(f"Missing narrow native UI guard: {name}:{index + 1}")
            gates.add(ui)
    assert gates, name
    for index in sorted(gates):
        lines[index] = lines[index].rstrip("\r\n") + " && !BBLITE_NO_UI_COMPARISON\n"
        patches.append({"file": f"src/{name}", "guardLine": index + 1})
    write(file, "".join(lines))

# These calls must never occur in a no-UI frame, even if a future host misses a gate.
file = destination / "src" / "pal_ui_rml.cpp"
text = (source / "src" / "pal_ui_rml.cpp").read_text(encoding="utf-8")
text = '#include "NoUiGuard.h"\n' + text
for signature in [
    "void update_ui_rml_runtime(UiRmlRuntime& runtime, std::uint32_t width, std::uint32_t height) {",
    "const UiRenderFrame& record_ui_rml_frame(UiRmlRuntime& runtime, std::uint32_t width,\n"
    "                                         std::uint32_t height) {",
]:
    assert text.count(signature) == 1, signature
    text = text.replace(signature, signature + '\n    NoUiBaseline::RefuseOnscreenUi();', 1)
write(file, text)

guard = r'''#pragma once
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

namespace NoUiBaseline {
inline std::string Environment(const char* name) {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0) return {};
    const std::string result = value ? value : "";
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}
inline void RefuseOnscreenUi() {
#if BBLITE_NO_UI_COMPARISON
    throw std::runtime_error("No-UI baseline attempted on-screen layout/record work.");
#endif
}
inline void WriteReceipt(const std::vector<double>& samples, const char* backend,
                         const std::string& driver) {
    const auto path = Environment("BBLITE_NO_UI_RECEIPT");
    if (path.empty()) return;
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot write no-UI baseline receipt.");
    const auto warmup = Environment("BBLITE_BENCHMARK_WARMUP_FRAMES");
    const auto msaa = Environment("BBLITE_MSAA");
    out << std::setprecision(17)
        << "{\"noUi\":true,\"frames\":" << samples.size()
        << ",\"warmupFrames\":" << (!warmup.empty() ? std::stoul(warmup) : 0)
        << ",\"seed\":1337,\"radius\":6,\"width\":1280,\"height\":720,"
        << "\"sampleCount\":" << (msaa == "1" ? 1 : 4)
        << ",\"fixedDeltaMs\":" << (1000.0/60.0)
        << ",\"backend\":\"" << backend << "\",\"driver\":\"" << driver
        << "\",\"hudDraws\":0,\"uiVertices\":0,\"uiLayouts\":0,\"uiRecords\":0,"
        << "\"privateWorldObserved\":false,\"worldHash\":null,\"chunks\":null,"
        << "\"measurementScope\":\"Existing standard backend benchmark bracket, UI layout/"
           "record/composition explicitly compiled out; includes canonical user update, "
           "uploads/acquire/submit/present. SDL GC included; Dawn GC excluded. "
           "No timed application source edits.\",\"cpuSamplesMs\":[";
    for (std::size_t i=0; i<samples.size(); ++i) out << (i ? "," : "") << samples[i];
    out << "]}\n";
}
}
'''
write(destination / "src" / "NoUiGuard.h", guard)
file = destination / "src" / "pal_gpu_frame.cpp"
text = (source / "src" / "pal_gpu_frame.cpp").read_text(encoding="utf-8")
text = '#include "NoUiGuard.h"\n' + text
anchor = "void report_benchmark(std::vector<double> samples, const char* backend, const std::string& driver) {"
assert text.count(anchor) == 1
text = text.replace(anchor, anchor + "\n    NoUiBaseline::WriteReceipt(samples, backend, driver);", 1)
write(file, text)

file = destination / "CMakeLists.txt"
text = file.read_text(encoding="utf-8")
text += """
option(BBLITE_NO_UI_COMPARISON "Explicit temporary on-screen UI removal; retains canvas/assets/input/files." OFF)
if(NOT BBLITE_NO_UI_COMPARISON)
    message(FATAL_ERROR "Experimental overlay requires BBLITE_NO_UI_COMPARISON=ON.")
endif()
add_compile_definitions(BBLITE_NO_UI_COMPARISON=1)
"""
file.write_text(text, encoding="utf-8", newline="")
for record in records:
    record["overlaySha256"] = sha((destination / record["file"]).read_bytes())
(args.output / "baseline-overlay.json").write_text(json.dumps({
    "realStandardEnginePreserved": True,
    "engineAlgorithmEdits": [],
    "onScreenHostUiGates": patches,
    "atlasCanvasAndAssetBakeRemoved": False,
    "applicationCppModified": False,
    "standardSourceFiles": records,
}, indent=2) + "\n", encoding="utf-8")
print(f"Prepared real standard-engine overlay with {len(patches)} explicit no-UI host gates.")
