import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdir, readFile, readdir, writeFile as writeFileAlways } from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";

const [checkout, emittedArgument, outputArgument] = process.argv.slice(2);
assert(checkout && emittedArgument && outputArgument,
    "Usage: project-game.mjs <bblitec> <Emitted> <output>");
const emitted = path.resolve(emittedArgument);
const output = path.resolve(outputArgument);
const { cppTokens } = await import(pathToFileURL(path.join(checkout,
    "dist", "src", "compiler", "cpp-identifiers.js")));
const manifest = JSON.parse(await readFile(path.join(emitted, "manifest.json"), "utf8"));
async function writeFile(file, text) {
    try {
        if (await readFile(file, "utf8") === text) return;
    } catch (error) {
        if (error.code !== "ENOENT") throw error;
    }
    await writeFileAlways(file, text);
}
await mkdir(path.join(output, "sources", "minecraft"), { recursive: true });
const files = ["main.cpp", "sources/minecraft.part1.cpp",
    ...(await readdir(path.join(emitted, "sources", "minecraft")))
        .filter(name => name.endsWith(".cpp")).map(name => `sources/minecraft/${name}`)];
const records = [];
for (const file of files) {
    const source = await readFile(path.join(emitted, file), "utf8");
    const tokens = [...cppTokens(source)];
    const edits = [];
    for (let index = 0; index < tokens.length; ++index) {
        // Profiling tags are compiler bookkeeping, not user or engine behavior.
        if (tokens[index].text === "begin_scene_mesh_profile") {
            let end = index + 1;
            assert.equal(tokens[end].text, "(");
            let depth = 1;
            while (depth) {
                ++end;
                if (tokens[end].text === "(") ++depth;
                if (tokens[end].text === ")") --depth;
            }
            assert.equal(tokens[end + 1].text, ",");
            assert.equal(tokens[index - 4].text, "bbl");
            edits.push([tokens[index - 4].start, tokens[end + 1].end, ""]);
        }
        if (file === "main.cpp" && tokens[index].text === "main" &&
            tokens[index - 1]?.text === "int" && tokens[index + 1]?.text === "(") {
            edits.push([tokens[index].start, tokens[index].end, "MinecraftApplication"]);
        }
    }
    let projected = source;
    for (const [begin, end, replacement] of edits.sort((a, b) => b[0] - a[0])) {
        projected = projected.slice(0, begin) + replacement + projected.slice(end);
    }
    if (file === "main.cpp") {
        const anchor = "        bblscene::bbl_register_scene(v_scene);";
        assert(projected.includes(anchor), "Missing native game startup observation point.");
        const required = ["v_bblite_renderer_fields_world_chunks_90",
            "v_bblite_mobs_fields_mobs_1436", "v_bblite_class_field_camera_521",
            "v_bblite_class_field_timeOfDay_133"];
        for (const name of required) assert(projected.includes(name));
        projected = projected.replace(anchor, `        LiteMinecraft::SetWorldProbe([&]
        {
            LiteMinecraft::WorldSnapshot result{};
            std::vector<bblscene::Chunk> chunks;
            for (const auto& entry : *v_bblite_renderer_fields_world_chunks_90)
            {
                chunks.push_back(entry.second);
            }
            std::sort(chunks.begin(), chunks.end(), [](const auto& a, const auto& b)
            {
                return a->cz != b->cz ? a->cz < b->cz : a->cx < b->cx;
            });
            result.chunks = v_bblite_renderer_fields_active_84->size();
            for (const auto& chunk : chunks)
            {
                if (chunk->cx < -6 || chunk->cx > 6 || chunk->cz < -6 || chunk->cz > 6)
                {
                    continue;
                }
                for (const auto block : chunk->blocks)
                {
                    result.hash = (result.hash ^ block) * 1099511628211ull;
                    result.nonzeroBlocks += block != 0;
                }
            }
            result.mobs = v_bblite_mobs_fields_mobs_1436->size();
            bbl::Check(bl_getNodePosition(bbl::Node(v_bblite_class_field_camera_521),
                &result.cameraPosition));
            bbl::Check(bl_getCameraTarget(v_bblite_class_field_camera_521,
                &result.cameraTarget));
            result.timeOfDay = *v_bblite_class_field_timeOfDay_133;
            result.seed = *v_bblite_class_field_seed_32;
            result.edits = v_bblite_renderer_fields_world_edits_91->size();
            result.playerPosition = {*v_bblite_class_field_x_518,
                *v_bblite_class_field_feetY_519, *v_bblite_class_field_z_520};
            return result;
        });
${anchor}`);
    }
    assert(!projected.includes("bbl::upstream::"), "Unexpected generated engine dependency.");
    await writeFile(path.join(output, file), projected);
    records.push({ file, originalSha256: createHash("sha256").update(source).digest("hex"),
        projectedSha256: createHash("sha256").update(projected).digest("hex"),
        adaptations: edits.length });
}
await writeFile(path.join(output, "sources", "application.hpp"), '#include "C99Client.h"\n');

const literal = value => JSON.stringify(value);
const uniformType = { f32: "BL_UNIFORM_F32", u32: "BL_UNIFORM_U32", i32: "BL_UNIFORM_I32",
    "vec2<f32>": "BL_UNIFORM_VEC2", "vec3<f32>": "BL_UNIFORM_VEC3",
    "vec4<f32>": "BL_UNIFORM_VEC4", "mat4x4<f32>": "BL_UNIFORM_MAT4" };
const elements = { f32: 1, u32: 1, i32: 1, "vec2<f32>": 2, "vec3<f32>": 3,
    "vec4<f32>": 4, "mat4x4<f32>": 16 };
let metadata = '#include "C99Client.h"\n\nnamespace bbl\n{\n';
metadata += "const MaterialMetadata& MaterialDescription(uint32_t variant)\n{\n";
metadata += "    switch (variant)\n    {\n";
for (let index = 0; index < manifest.customShaderPrograms.length; ++index) {
    const shader = manifest.customShaderPrograms[index];
    assert(shader.storageBuffers.length === 0 && shader.defines.length === 0);
    metadata += `        case ${index}:\n        {\n`;
    metadata += `            static const bl_VertexSemantic attributes[] = {${shader.attributes
        .map(name => `BL_ATTRIBUTE_${name.toUpperCase()}`).join(", ")}};\n`;
    const defaults = new Map(shader.uniformDefaults.map(value => [value.name, value.values]));
    for (const [name, values] of defaults) {
        metadata += `            static const double default_${name}[] = {${values.join(", ")}};\n`;
    }
    metadata += "            static const bl_ShaderUniformDecl uniforms[] =\n            {\n";
    const slots = [];
    let offset = 0;
    for (const declaration of shader.uniforms) {
        const [name, type] = declaration.split(":");
        if (type) assert(uniformType[type], `Unsupported custom uniform ${declaration}.`);
        const defaultSpan = defaults.has(name) ?
            `{default_${name}, ${defaults.get(name).length}}` : "{}";
        metadata += `                {String(${literal(name)}), ${type ? uniformType[type] :
            "BL_UNIFORM_MAT4"}, ${defaultSpan}, ${!type}},\n`;
        if (type) {
            slots.push(`{${offset}, String(${literal(name)}), ${elements[type]}}`);
            offset += elements[type];
        }
    }
    metadata += "            };\n";
    metadata += `            static const UniformSlot slots[] = {${slots.join(", ")}};\n`;
    if (shader.samplers.length) {
        metadata += `            static const bl_ShaderSamplerDecl samplers[] = {${shader.samplers
            .map(name => `{String(${literal(name)})}`).join(", ")}};\n`;
    }
    metadata += "            static const MaterialMetadata value = []\n            {\n";
    metadata += "                MaterialMetadata result{};\n";
    metadata += `                result.options.name = String(${literal(shader.name)});\n`;
    metadata += `                result.options.vertexSource = String(${literal(shader.vertexSource)});\n`;
    metadata += `                result.options.fragmentSource = String(${literal(shader.fragmentSource)});\n`;
    metadata += `                result.options.attributes = attributes;\n`;
    metadata += `                result.options.attributeCount = ${shader.attributes.length};\n`;
    metadata += `                result.options.uniforms = uniforms;\n`;
    metadata += `                result.options.uniformCount = ${shader.uniforms.length};\n`;
    if (shader.samplers.length) {
        metadata += `                result.options.samplers = samplers;\n`;
        metadata += `                result.options.samplerCount = ${shader.samplers.length};\n`;
    }
    for (const name of ["needAlphaBlending", "needAlphaTesting", "backFaceCulling", "depthWrite"]) {
        metadata += `                result.options.${name} = ${shader[name] ?
            "BL_BOOL_TRUE" : "BL_BOOL_FALSE"};\n`;
    }
    metadata += `                result.options.blendMode = ${shader.blendMode === "additive" ?
        "BL_BLEND_ADDITIVE" : "BL_BLEND_ALPHA"};\n`;
    metadata += `                result.slots = slots;\n                result.slotCount = ${slots.length};\n`;
    metadata += "                return result;\n            }();\n            return value;\n        }\n";
}
metadata += '        default:\n            throw std::runtime_error("Unknown application material variant.");\n';
metadata += "    }\n}\n}\n";
await writeFile(path.join(output, "MaterialMetadata.cpp"), metadata);
await writeFile(path.join(output, "projection.json"), JSON.stringify({
    originalTsEdited: false, shaderDataOrigin: "Emitted/manifest.json customShaderPrograms",
    compiledApplicationSources: records, generatedEngineObjectsCompiled: [],
    legacyRendererPalObjectsCompiled: [], languageSupport: ["js_data.hpp", "js_json.hpp",
        "js_synchronous_promise.hpp"], publicAbi: "babylon_lite.h",
    scope: "Full original Minecraft application, not complete Babylon Lite API coverage",
}, null, 2) + "\n");
console.log(`Projected ${files.length} application translation units and original shader metadata.`);
