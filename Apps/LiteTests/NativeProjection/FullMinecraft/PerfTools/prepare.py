"""Create hash-recorded diagnostic overlays without modifying qualified source trees."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


TOOLS = Path(__file__).resolve().parent
RECORDS = []


def change(text, old, new, count=1):
    actual = text.count(old)
    if actual != count:
        raise ValueError(f"Expected {count} anchors, found {actual}: {old[:100]!r}")
    return text.replace(old, new)


def save(source, destination, transform=lambda value: value):
    original = source.read_text(encoding="utf-8")
    projected = transform(original)
    if destination.parent.name == "Source" and destination.parent.parent.name == "Core":
        if destination.suffix == ".cpp":
            style = destination.parent.parent / ".clang-format"
            projected = subprocess.run(
                [r"C:\Program Files\LLVM\bin\clang-format.exe", f"--style=file:{style}"],
                input=projected, capture_output=True, text=True, check=True).stdout
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_text(encoding="utf-8") != projected:
        destination.write_text(projected, encoding="utf-8")
    RECORDS.append({
        "source": str(source), "overlay": str(destination),
        "sourceSha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "overlaySha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
    })


def gc(text):
    text = '#include "Perf.h"\n' + text
    text = change(text, "    const auto nodes = registry.nodes;\n    gc::SharedNodes shared;",
                  "    LitePerf::CountEvent(LitePerf::GcRuns);\n"
                  "    LitePerf::CountEvent(LitePerf::GcTracedNodes, registry.nodes.size());\n"
                  "    const auto nodes = registry.nodes;\n    gc::SharedNodes shared;")
    text = change(text, "        if (node->owners() > node->incoming + 1)\n            mark.edge(node);",
                  "        LitePerf::CountEvent(LitePerf::GcEdges, node->incoming);\n"
                  "        if (node->owners() > node->incoming + 1) {\n"
                  "            LitePerf::CountEvent(LitePerf::GcRoots);\n"
                  "            mark.edge(node);\n        }")
    text = change(text, "    registry.allocations = 0;\n    registry.frames_since_collection = 0;",
                  "    LitePerf::CountEvent(LitePerf::GcCollected, collected);\n"
                  "    registry.allocations = 0;\n    registry.frames_since_collection = 0;")
    return text


def host(text, native):
    audio = native / "Apps/LiteTests/LitePlayground/Source/NativeAudio.h"
    text = text.replace('#include "../../../LitePlayground/Source/NativeAudio.h"',
                        f'#include "{audio.as_posix()}"')
    text = '#include "Perf.h"\n' + text
    text = change(text, "        int64_t previousFrame = Counter();",
                  "        LitePerf::Initialize(s_config.measured);\n"
                  "        int64_t previousFrame = Counter();")
    text = change(text, "            const auto totalBegin = Counter();",
                  "            if (rendered == s_config.warmup) { LitePerf::BeginRun(); }\n"
                  "            if (s_config.benchmark && rendered >= s_config.warmup)\n"
                  "            { LitePerf::BeginFrame(); }\n"
                  "            const auto totalBegin = Counter();")
    text = change(text, "            TickPlatform(frameDelta);",
                  "            const auto platformBegin = LitePerf::Tick();\n"
                  "            TickPlatform(frameDelta);\n"
                  "            LitePerf::Add(LitePerf::Platform, platformBegin);\n"
                  "            const auto audioBegin = LitePerf::Tick();")
    text = change(text, "            const auto coreBegin = Counter();",
                  "            LitePerf::Add(LitePerf::Audio, audioBegin);\n"
                  "            if (LitePerf::drawGuard) { LitePerf::drawHash = 1469598103934665603ull; }\n"
                  "            const auto coreBegin = Counter();")
    text = change(text, "            const uint32_t frame = bgfx::frame();",
                  "            LitePerf::Add(LitePerf::BlFrame, coreBegin);\n"
                  "            const auto bgfxBegin = LitePerf::Tick();\n"
                  "            const uint32_t frame = bgfx::frame();\n"
                  "            LitePerf::Add(LitePerf::BgfxFrame, bgfxBegin);")
    text = change(text, "            PresentHUD();\n            bbl::js::collect_at_frame_boundary();",
                  "            const auto hudBegin = LitePerf::Tick();\n"
                  "            if (!LitePerf::hudOff) { PresentHUD(); }\n"
                  "            LitePerf::Add(LitePerf::Hud, hudBegin);\n"
                  "            const auto nodesBefore = bbl::js::managed_node_count();\n"
                  "            const auto managedBefore = bbl::js::gc::registry.total_allocations;\n"
                  "            const auto gcBegin = LitePerf::Tick();\n"
                  "            if (!LitePerf::gcOff) { bbl::js::collect_at_frame_boundary(); }\n"
                  "            LitePerf::Add(LitePerf::Gc, gcBegin);\n"
                  "            LitePerf::CountEvent(LitePerf::NodesBefore, nodesBefore);\n"
                  "            LitePerf::CountEvent(LitePerf::NodesAfter, bbl::js::managed_node_count());\n"
                  "            LitePerf::CountEvent(LitePerf::ManagedNew,\n"
                  "                bbl::js::gc::registry.total_allocations - managedBefore);")
    # Managed allocation delta must cover callbacks, not just collection.
    text = text.replace("            const auto totalBegin = Counter();",
                        "            const auto managedFrameBegin = bbl::js::gc::registry.total_allocations;\n"
                        "            const auto totalBegin = Counter();")
    text = text.replace("            const auto managedBefore = bbl::js::gc::registry.total_allocations;\n", "")
    text = text.replace("bbl::js::gc::registry.total_allocations - managedBefore",
                        "bbl::js::gc::registry.total_allocations - managedFrameBegin")
    text = change(text, "                coreSamples.push_back(Milliseconds(coreBegin, coreEnd));",
                  "                const auto* perfGpu = bgfx::getStats();\n"
                  "                LitePerf::CountEvent(LitePerf::Draws, stats.drawCallCount);\n"
                  "                LitePerf::CountEvent(LitePerf::PrimitiveTriangles,\n"
                  "                    perfGpu->numPrims[bgfx::Topology::TriList]);\n"
                  "                if (LitePerf::enabled && perfGpu->cpuTimerFreq > 0)\n"
                  "                {\n"
                  "                    const double factor = 1000.0 / perfGpu->cpuTimerFreq;\n"
                  "                    LitePerf::phases[LitePerf::RenderThreadWall] =\n"
                  "                        (perfGpu->cpuTimeEnd - perfGpu->cpuTimeBegin) * factor;\n"
                  "                    LitePerf::phases[LitePerf::WaitRender] = perfGpu->waitRender * factor;\n"
                  "                    LitePerf::phases[LitePerf::WaitSubmit] = perfGpu->waitSubmit * factor;\n"
                  "                }\n"
                  "                LitePerf::EndFrame(Milliseconds(totalBegin, totalEnd));\n"
                  "                coreSamples.push_back(Milliseconds(coreBegin, coreEnd));")
    text = change(text, "            ++rendered;",
                  "            ++rendered;\n"
                  "            if (rendered == limit && s_config.benchmark) { LitePerf::EndRun(); }")
    text = change(text, "        bbl::Check(bl_stopEngine(engine.state->engine));",
                  "        LitePerf::Write(\"native\");\n"
                  "        bbl::Check(bl_stopEngine(engine.state->engine));")
    text = change(text, "            const auto totalEnd = Counter();",
                  "            const auto totalEnd = Counter();\n"
                  "            if (LitePerf::drawGuard) { LitePerf::drawHashes.push_back(LitePerf::drawHash); }")
    text = change(text, "        if (s_config.samples != 1 && s_config.samples != 4)",
                  "        if (LitePerf::drawGuard && s_config.benchmark)\n"
                  "        { throw std::runtime_error(\"Draw-state hashing belongs outside benchmarks.\"); }\n"
                  "        if (s_config.samples != 1 && s_config.samples != 4)")
    text = change(text, "        init.profile = true;", "        init.profile = !LitePerf::gpuOff;")
    text = change(text, "        bgfx::Init init{};",
                  "        if (LitePerf::singleThread) { bgfx::renderFrame(); }\n"
                  "        bgfx::Init init{};")
    text = change(text, "        init.callback = &s_graphics;",
                  "        if (LitePerf::queue3)\n"
                  "        {\n            init.swapChain.numBackBuffers = 3;\n"
                  "            init.swapChain.maxFrameLatency = 3;\n        }\n"
                  "        init.callback = &s_graphics;")
    text = change(text, "{ return _aligned_malloc(bytes, alignment); };",
                  "{\n            void* result = _aligned_malloc(bytes, alignment);\n"
                  "            if (result)\n            {\n"
                  "                ++LitePerf::coreAllocCount;\n"
                  "                LitePerf::coreAllocBytes += bytes;\n"
                  "                LitePerf::coreLiveBytes += bytes;\n"
                  "                LitePerf::corePeakBytes = std::max(LitePerf::corePeakBytes,\n"
                  "                    LitePerf::coreLiveBytes);\n"
                  "            }\n            return result;\n        };")
    text = change(text, "[](void*, void* memory, size_t, size_t)\n        { _aligned_free(memory); };",
                  "[](void*, void* memory, size_t bytes, size_t)\n"
                  "        {\n            if (memory)\n            {\n"
                  "                ++LitePerf::coreFreeCount;\n"
                  "                LitePerf::coreLiveBytes -= bytes;\n"
                  "            }\n            _aligned_free(memory);\n        };")
    return text


def client(text):
    text = '#include "Perf.h"\n' + text
    text = change(text, "            bl_Vec3 value{};\n            switch (property)",
                  "            LitePerf::CountEvent(LitePerf::VectorGets);\n"
                  "            bl_Vec3 value{};\n            switch (property)")
    text = change(text, "            switch (property)\n            {",
                  "            switch (property)\n            {", count=2)
    anchor = "                         bl_FreeCamera camera = {})\n        {\n            switch (property)"
    text = change(text, anchor, anchor.replace("            switch",
                  "            LitePerf::CountEvent(LitePerf::VectorSets);\n            switch"))
    text = change(text, "                    try\n                    {",
                  "                    LitePerf::Scope callbackScope(LitePerf::Callbacks);\n"
                  "                    try\n                    {")
    text = change(text, "        const auto& description = MaterialDescription(material.variant);",
                  "        LitePerf::CountEvent(LitePerf::UniformSets);\n"
                  "        const auto& description = MaterialDescription(material.variant);")
    text = change(text, "            const auto& slot = description.slots[index];",
                  "            LitePerf::CountEvent(LitePerf::UniformSlotTests);\n"
                  "            const auto& slot = description.slots[index];")
    return text


def platform(text):
    text = '#include "Perf.h"\n' + text
    # Exact structural snapshots, not a collision-prone hash. Includes all rendered source fields.
    text = change(text, "    void PresentHUD()\n    {",
                  "    void PresentHUD()\n    {\n"
                  "        static std::vector<std::tuple<std::string, std::string,\n"
                  "            std::map<std::string, std::string>, std::vector<uint32_t>>> previousHud;\n"
                  "        if (LitePerf::hudRetained)\n        {\n"
                  "            decltype(previousHud) current;\n"
                  "            current.reserve(s_elements.size());\n"
                  "            for (const auto& item : s_elements)\n"
                  "            { current.emplace_back(item.css, item.text, item.styles, item.children); }\n"
                  "            if (current == previousHud) { return; }\n"
                  "            previousHud = std::move(current);\n        }\n"
                  "        LitePerf::CountEvent(LitePerf::HudDraws);\n"
                  "        auto checkpoint = LitePerf::Tick();")
    text = change(text, "        int slot{};\n        for (const auto& element",
                  "        LitePerf::Add(LitePerf::HudSetup, checkpoint);\n"
                  "        checkpoint = LitePerf::Tick();\n"
                  "        int slot{};\n        for (const auto& element")
    text = change(text, '                const std::regex pattern("(?:url|image)\\\\(\\"([^\\"]+)\\"");',
                  '                static const std::regex savedPattern("(?:url|image)\\\\(\\"([^\\"]+)\\"");\n'
                  '                std::optional<std::regex> localPattern;\n'
                  '                if (!LitePerf::cachedRegex)\n'
                  '                {\n'
                  '                    localPattern.emplace("(?:url|image)\\\\(\\"([^\\"]+)\\"");\n'
                  '                    LitePerf::CountEvent(LitePerf::RegexCompiles);\n'
                  '                }\n'
                  '                const auto& pattern = localPattern ? *localPattern : savedPattern;')
    text = change(text, "        auto* pixels = static_cast<uint8_t*>(data);",
                  "        LitePerf::Add(LitePerf::HudElements, checkpoint);\n"
                  "        checkpoint = LitePerf::Tick();\n"
                  "        auto* pixels = static_cast<uint8_t*>(data);")
    text = change(text, "        std::memcpy(s_hud.data(), data, s_hud.size());",
                  "        LitePerf::Add(LitePerf::HudAlpha, checkpoint);\n"
                  "        checkpoint = LitePerf::Tick();\n"
                  "        std::memcpy(s_hud.data(), data, s_hud.size());\n"
                  "        LitePerf::Add(LitePerf::HudCopy, checkpoint);\n"
                  "        checkpoint = LitePerf::Tick();")
    text = change(text, "        SelectObject(dc, previous);",
                  "        LitePerf::Add(LitePerf::HudPresent, checkpoint);\n"
                  "        checkpoint = LitePerf::Tick();\n"
                  "        SelectObject(dc, previous);")
    text = change(text, "        DeleteDC(dc);",
                  "        DeleteDC(dc);\n        LitePerf::Add(LitePerf::HudDispose, checkpoint);")
    text = change(text, "        s_time = Clock();",
                  "        LitePerf::Scope timerScope(LitePerf::Timers);\n"
                  "        LitePerf::CountEvent(LitePerf::PendingTimers, s_timers.size());\n"
                  "        s_time = Clock();")
    text = change(text, "        for (auto& callback : due)\n        {\n            callback();",
                  "        for (auto& callback : due)\n        {\n"
                  "            LitePerf::CountEvent(LitePerf::TimerCallbacks);\n            callback();")
    return text


def engine(text):
    text = '#include "PerfC.h"\n' + text
    text = change(text, "    l_leaveDispatch(r);\n    bgfx::setViewMode",
                  "    l_leaveDispatch(r);\n    int64_t checkpoint = lite_perf_tick();\n"
                  "    bgfx::setViewMode")
    text = change(text, "    L_Draw* draws = (L_Draw*)s->drawScratch;",
                  "    lite_perf_add(7, checkpoint);\n"
                  "    checkpoint = lite_perf_tick();\n"
                  "    lite_perf_count(16, s->memberCount);\n"
                  "    L_Draw* draws = (L_Draw*)s->drawScratch;")
    text = change(text, "    for (size_t i = 0; i < count; ++i)\n    {\n        if (!draws[i].transparent)",
                  "    lite_perf_add(8, checkpoint);\n"
                  "    checkpoint = lite_perf_tick();\n"
                  "    for (size_t i = 0; i < count; ++i)\n    {\n        if (!draws[i].transparent)")
    text = change(text, "    if (status == BL_OK && count)\n    {\n        qsort",
                  "    lite_perf_add(9, checkpoint);\n"
                  "    checkpoint = lite_perf_tick();\n"
                  "    if (status == BL_OK && count)\n    {\n        qsort")
    text = change(text, "            double minimum = draws[i].order;",
                  "            if (lite_perf_grouping_cache())\n"
                  "            {\n                bool reused = false;\n"
                  "                for (size_t j = 0; j < i; ++j)\n"
                  "                {\n                    if (!draws[j].transparent &&\n"
                  "                        draws[j].material == draws[i].material)\n"
                  "                    {\n                        draws[i].order = draws[j].order;\n"
                  "                        draws[i].group = draws[j].group;\n"
                  "                        reused = true;\n                        break;\n"
                  "                    }\n                }\n"
                  "                if (reused)\n                {\n                    continue;\n"
                  "                }\n            }\n"
                  "            double minimum = draws[i].order;")
    text = change(text, "        bl_Vec3 position = {camera->world.values[12]",
                  "        lite_perf_add(10, checkpoint);\n"
                  "        checkpoint = lite_perf_tick();\n"
                  "        bl_Vec3 position = {camera->world.values[12]")
    text = change(text, "    return status;\n}\n\nstatic bl_Status render(",
                  "    lite_perf_add(11, checkpoint);\n"
                  "    return status;\n}\n\nstatic bl_Status render(")
    text = change(text, "            status = l_drawMaterial(r, e, draws[i].material, draws[i].mesh, &view, &projection,",
                  "            lite_perf_guard(&draws[i].mesh->node.record.id, sizeof(uint64_t));\n"
                  "            lite_perf_guard(&draws[i].material->record.id, sizeof(uint64_t));\n"
                  "            lite_perf_guard(&draws[i].mesh->node.world, sizeof(bl_Mat4));\n"
                  "            lite_perf_guard(&draws[i].mesh->geometry.vertices, sizeof(size_t));\n"
                  "            lite_perf_guard(&draws[i].mesh->geometry.indexCount, sizeof(size_t));\n"
                  "            lite_perf_guard(&draws[i].order, sizeof(double));\n"
                  "            lite_perf_guard(&draws[i].depth, sizeof(double));\n"
                  "            lite_perf_guard(&draws[i].group, sizeof(size_t));\n"
                  "            lite_perf_guard(&view, sizeof(view));\n"
                  "            lite_perf_guard(&projection, sizeof(projection));\n"
                  "            status = l_drawMaterial(r, e, draws[i].material, draws[i].mesh, &view, &projection,")
    return text


def nodes(text):
    text = '#include "PerfC.h"\n' + text
    text = change(text, "bl_Status l_world(bl_Runtime* r, L_Node* n)\n{",
                  "bl_Status l_world(bl_Runtime* r, L_Node* n)\n{\n    lite_perf_count(17, 1);")
    text = change(text, "    if (!n->dirty", "    if (!n->dirty")
    # Record rebuild decisions at the actual dirty/version gate.
    marker = "    bl_Mat4 m;\n    l_identity(&m);"
    if marker in text:
        text = change(text, marker, "    lite_perf_count(18, 1);\n" + marker)
    else:
        raise ValueError("Missing world rebuild anchor")
    return text


def rewritten_engine(text):
    text = '#include "PerfC.h"\n' + text
    text = change(text, "    l_leaveDispatch(r);\n    bgfx::setViewMode",
                  "    l_leaveDispatch(r);\n    int64_t checkpoint = lite_perf_tick();\n"
                  "    bgfx::setViewMode")
    text = change(text, "    L_TRY(l_collectDraws(r, s, &view, &count));",
                  "    lite_perf_add(7, checkpoint);\n"
                  "    lite_perf_count(16, s->memberCount);\n"
                  "    L_TRY(l_collectDraws(r, s, &view, &count));\n"
                  "    checkpoint = lite_perf_tick();")
    text = change(text, "        l_sortDraws(s, count);",
                  "        l_sortDraws(s, count);\n"
                  "        lite_perf_add(10, checkpoint);\n"
                  "        checkpoint = lite_perf_tick();")
    text = change(text, "    return status;\n}\n\nstatic bl_Status render(",
                  "    lite_perf_add(11, checkpoint);\n"
                  "    return status;\n}\n\nstatic bl_Status render(")
    text = change(text, "            status = l_drawMaterial(r, e, draws[i].material, draws[i].mesh, &view, &projection,",
                  "            lite_perf_guard(&draws[i].mesh->node.record.id, sizeof(uint64_t));\n"
                  "            lite_perf_guard(&draws[i].material->record.id, sizeof(uint64_t));\n"
                  "            lite_perf_guard(&draws[i].mesh->node.world, sizeof(bl_Mat4));\n"
                  "            lite_perf_guard(&draws[i].mesh->geometry.vertices, sizeof(size_t));\n"
                  "            lite_perf_guard(&draws[i].mesh->geometry.indexCount, sizeof(size_t));\n"
                  "            lite_perf_guard(&draws[i].order, sizeof(double));\n"
                  "            lite_perf_guard(&draws[i].depth, sizeof(double));\n"
                  "            lite_perf_guard(&draws[i].group, sizeof(size_t));\n"
                  "            lite_perf_guard(&view, sizeof(view));\n"
                  "            lite_perf_guard(&projection, sizeof(projection));\n"
                  "            status = l_drawMaterial(r, e, draws[i].material, draws[i].mesh, &view, &projection,")
    return text


def rewritten_order(text):
    text = '#include "PerfC.h"\n' + text
    text = change(text, "    L_TRY(reserveDraws(r, s));",
                  "    int64_t checkpoint = lite_perf_tick();\n"
                  "    L_TRY(reserveDraws(r, s));")
    text = change(text, "    L_Draw* draws = (L_Draw*)s->drawScratch;",
                  "    lite_perf_add(9, checkpoint);\n"
                  "    checkpoint = lite_perf_tick();\n"
                  "    L_Draw* draws = (L_Draw*)s->drawScratch;")
    text = change(text, "    for (size_t i = 0; i < count; ++i)",
                  "    lite_perf_add(8, checkpoint);\n"
                  "    checkpoint = lite_perf_tick();\n"
                  "    for (size_t i = 0; i < count; ++i)")
    text = change(text, "    *out = count;",
                  "    lite_perf_add(9, checkpoint);\n    *out = count;")
    return text


def rewritten_shader_values(text):
    text = '#include "PerfC.h"\n' + text
    return change(text, "    return setUniform(h, n, v.data, v.count, true);",
                  "    int64_t begin = lite_perf_tick();\n"
                  "    bl_Status status = setUniform(h, n, v.data, v.count, true);\n"
                  "    lite_perf_add(27, begin);\n    return status;")


def rewritten_shader_pipeline(text):
    text = '#include "PerfC.h"\n' + text
    return change(text, "        bgfx::setUniform(u->handle, u->bytes, u->count);",
                  "        lite_perf_count(25, u->byteCount);\n"
                  "        lite_perf_guard(u->bytes, u->byteCount);\n"
                  "        bgfx::setUniform(u->handle, u->bytes, u->count);")


def shader(text):
    text = '#include "PerfC.h"\n' + text
    # Count bytes handed to bgfx, rather than pretending they are dynamic geometry uploads.
    text = change(text, "        bgfx::setUniform(u->handle, u->bytes, u->count);",
                  "        lite_perf_count(25, u->byteCount);\n"
                  "        lite_perf_guard(u->bytes, u->byteCount);\n"
                  "        bgfx::setUniform(u->handle, u->bytes, u->count);")
    text = change(text, "    return setUniform(h, n, v.data, v.count, true);",
                  "    int64_t begin = lite_perf_tick();\n"
                  "    bl_Status status = setUniform(h, n, v.data, v.count, true);\n"
                  "    lite_perf_add(27, begin);\n    return status;")
    return text


def sdl(text):
    text = '#include "Perf.h"\n' + text
    text = change(text, "        start = monotonic_milliseconds();",
                  "        if (frame == data_.frame_options.benchmark_warmup())\n"
                  "        { LitePerf::BeginRun(); }\n"
                  "        if (frame >= data_.frame_options.benchmark_warmup())\n"
                  "        { LitePerf::BeginFrame();\n"
                  "          LitePerf::managedStart = js::gc::registry.total_allocations; }\n"
                  "        start = monotonic_milliseconds();")
    text = change(text, "    bool acquire() {",
                  "    bool acquire() {\n        LitePerf::Scope perfScope(LitePerf::Acquire);")
    text = change(text, "        delta_ms = advance_frame(engine, scene, frame_clock, frame_options.frame_delta_ms);",
                  "        const auto callbackBegin = LitePerf::Tick();\n"
                  "        delta_ms = advance_frame(engine, scene, frame_clock, frame_options.frame_delta_ms);\n"
                  "        LitePerf::Add(LitePerf::Callbacks, callbackBegin);")
    text = change(text, "        update_ui_rml_runtime(*ui_runtime, width, height);",
                  "        const auto rmlBegin = LitePerf::Tick();\n"
                  "        update_ui_rml_runtime(*ui_runtime, width, height);\n"
                  "        LitePerf::Add(LitePerf::RmlUpdate, rmlBegin);")
    text = change(text, "    void synchronize() {",
                  "    void synchronize() {\n        LitePerf::Scope perfScope(LitePerf::Synchronize);")
    text = change(text, "    void encode() {",
                  "    void encode() {\n        LitePerf::Scope perfScope(LitePerf::Encode);")
    text = change(text, "    void present() {",
                  "    void present() {\n        LitePerf::Scope perfScope(LitePerf::Submit);")
    text = change(text, "        finish_frame(engine);",
                  "        const auto finishBegin = LitePerf::Tick();\n"
                  "        finish_frame(engine);\n        LitePerf::Add(LitePerf::Finish, finishBegin);")
    text = change(text, "            samples.push_back(end - start);",
                  "            LitePerf::CountEvent(LitePerf::ManagedNew,\n"
                  "                js::gc::registry.total_allocations - LitePerf::managedStart);\n"
                  "            LitePerf::CountEvent(LitePerf::RenderItems, render_plan.items.size());\n"
                  "            LitePerf::CountEvent(LitePerf::Draws,\n"
                  "                render_plan.draw_lists.opaque.commands.size() +\n"
                  "                render_plan.draw_lists.transparent.commands.size());\n"
                  "            LitePerf::EndFrame(end - start);\n"
                  "            samples.push_back(end - start);")
    text = change(text, "    void finish_run() {",
                  "    void finish_run() {\n        LitePerf::EndRun();\n"
                  "        LitePerf::Write(\"SDL_GPU\");")
    text = change(text, "SceneRun run_gpu_engine(Engine& engine) {",
                  "SceneRun run_gpu_engine(Engine& engine) {\n"
                  "    LitePerf::Initialize(1200);")
    return text


def sdl_frame(text):
    text = '#include "Perf.h"\n' + text
    text = change(text, "    js::collect_at_frame_boundary();",
                  "    const auto nodesBefore = js::managed_node_count();\n"
                  "    const auto gcBegin = LitePerf::Tick();\n"
                  "    if (!LitePerf::gcOff) { js::collect_at_frame_boundary(); }\n"
                  "    LitePerf::Add(LitePerf::Gc, gcBegin);\n"
                  "    LitePerf::CountEvent(LitePerf::NodesBefore, nodesBefore);\n"
                  "    LitePerf::CountEvent(LitePerf::NodesAfter, js::managed_node_count());")
    text = change(text, "    run_timeout_callbacks(engine);\n    run_interval_callbacks(engine);",
                  "    const auto timersBegin = LitePerf::Tick();\n"
                  "    run_timeout_callbacks(engine);\n    run_interval_callbacks(engine);\n"
                  "    LitePerf::Add(LitePerf::Timers, timersBegin);")
    return text


def rml(text):
    text = '#include "Perf.h"\n' + text
    text = change(text, "    const bool tree_changed = runtime.projected_revision != runtime.engine.ui_revision;",
                  "    const bool tree_changed = runtime.projected_revision != runtime.engine.ui_revision;\n"
                  "    if (tree_changed) { LitePerf::CountEvent(LitePerf::RmlTreeChanges); }")
    text = change(text, "    if ((tree_changed && !text_only) || motion_changed || viewport_changed) {",
                  "    if (text_only) { LitePerf::CountEvent(LitePerf::RmlTextUpdates); }\n"
                  "    if ((tree_changed && !text_only) || motion_changed || viewport_changed) {\n"
                  "        LitePerf::CountEvent(LitePerf::RmlFullProjections);")
    return text


def commands(text):
    text = '#include "Perf.h"\n' + text
    return change(text,
                  "    bool submit() noexcept { return SDL_SubmitGPUCommandBuffer(std::exchange(command_, nullptr)); }",
                  "    bool submit() noexcept {\n"
                  "        LitePerf::Scope submitScope(LitePerf::DriverSubmit);\n"
                  "        return SDL_SubmitGPUCommandBuffer(std::exchange(command_, nullptr));\n"
                  "    }")


def sdl_ui(text):
    text = '#include "Perf.h"\n' + text
    return change(text, "    const double started = cpu_sample ? monotonic_milliseconds() : 0;",
                  "    LitePerf::Scope perfScope(LitePerf::RmlRender);\n"
                  "    const double started = cpu_sample ? monotonic_milliseconds() : 0;")


def sdl_upload(text):
    text = '#include "Perf.h"\n' + text
    return change(text, "        std::memcpy(mapped, bytes_.get(), bytes_size_);",
                  "        LitePerf::CountEvent(LitePerf::UploadBytes, bytes_size_);\n"
                  "        std::memcpy(mapped, bytes_.get(), bytes_size_);")


def timers(text):
    text = '#include "Perf.h"\n' + text
    begin = text.index("void run_timeout_callbacks(")
    end = text.index("\ndouble set_interval(", begin)
    region = text[begin:end]
    region = change(region, "    const double now_ms = engine.animation_frame_timestamp_ms;",
                    "    LitePerf::CountEvent(LitePerf::PendingTimers, engine.timeout_callbacks.size());\n"
                    "    const double now_ms = engine.animation_frame_timestamp_ms;")
    region = change(region, "        callback();",
                    "        LitePerf::CountEvent(LitePerf::TimerCallbacks);\n        callback();")
    return text[:begin] + region + text[end:]


def main():
    mode, native_arg, compiler_arg, output_arg = sys.argv[1:]
    native, compiler, output = map(Path, (native_arg, compiler_arg, output_arg))
    output.mkdir(parents=True, exist_ok=True)
    if mode == "native":
        source = native / "Apps/LiteTests/NativeProjection/FullMinecraft/Source"
        for file in source.iterdir():
            if file.suffix not in (".h", ".cpp"):
                continue
            transform = {"NativeHost.cpp": lambda t: host(t, native),
                         "Platform.cpp": platform, "C99Client.cpp": client}.get(file.name)
            save(file, output / "Host" / file.name, transform or (lambda t: t))
        runtime = output / "Runtime/bblite"
        runtime.mkdir(parents=True, exist_ok=True)
        save(compiler / "native/include/bblite/js_gc.hpp", runtime / "js_gc.hpp", gc)
        (output / "Core/Source").mkdir(parents=True, exist_ok=True)
        shutil.copy2(native / "Core/LiteLayer/.clang-format", output / "Core/.clang-format")
        shutil.copy2(native / "Core/LiteLayer/.clang-tidy", output / "Core/.clang-tidy")
        for name, transform in (("Engine.cpp", rewritten_engine), ("Nodes.cpp", nodes),
                                ("ShaderMaterial.cpp", rewritten_shader_values),
                                ("ShaderPipeline.cpp", rewritten_shader_pipeline),
                                ("RenderOrder.cpp", rewritten_order)):
            save(native / "Core/LiteLayer/Source" / name, output / "Core/Source" / name, transform)
        for header in (native / "Core/LiteLayer/Source").glob("*.h"):
            save(header, output / "Core/Source" / header.name)
        save(native / "Core/LiteLayer/Include/babylon_lite.h",
             output / "Core/Include/babylon_lite.h")
    elif mode == "sdl":
        source = compiler / "native"
        upstream = output.parent / "upstream"
        upstream.mkdir(parents=True, exist_ok=True)
        for pin in (compiler / "upstream").glob("*.json"):
            shutil.copy2(pin, upstream / pin.name)
        for folder in ("src", "include", "patches"):
            shutil.copytree(source / folder, output / folder, dirs_exist_ok=True)
        for file in source.iterdir():
            if file.is_file():
                shutil.copy2(file, output / file.name)
        save(source / "patch-identity.cmake", output / "patch-identity.cmake",
             lambda t: change(t,
                 'get_filename_component(BBLITE_PATCH_REPOSITORY "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)',
                 f'set(BBLITE_PATCH_REPOSITORY "{compiler.as_posix()}")'))
        shutil.copy2(TOOLS / "Allocations.cpp", output / "src/Allocations.cpp")
        (output / "src/Perf.h").write_text('#include "../include/bblite/Perf.h"\n')
        shutil.copy2(TOOLS / "Perf.h", output / "include/bblite/Perf.h")
        for name, transform in (("pal_sdl_gpu.cpp", sdl), ("pal_gpu_frame.cpp", sdl_frame),
                                ("pal_ui_rml.cpp", rml), ("pal.cpp", timers),
                                ("pal_sdl_gpu_commands.hpp", commands),
                                ("pal_sdl_gpu_sprite_ui.hpp", sdl_ui),
                                ("pal_sdl_gpu_shared.hpp", sdl_upload)):
            save(source / "src" / name, output / "src" / name, transform)
        save(source / "include/bblite/js_gc.hpp", output / "include/bblite/js_gc.hpp", gc)
        cmake = output / "CMakeLists.txt"
        cmake.write_text(cmake.read_text() +
                         '\ntarget_sources(bblite_native PRIVATE src/Allocations.cpp)\n'
                         'target_link_libraries(bblite_native PRIVATE psapi)\n')
    else:
        raise ValueError(mode)
    (output / "overlay-manifest.json").write_text(json.dumps(RECORDS, indent=2) + "\n")
    print(f"Prepared {mode}: {output}")


if __name__ == "__main__":
    main()
