import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { copyFile, mkdir, readFile, readdir, writeFile } from "node:fs/promises";
import path from "node:path";

const [emittedArgument, outputArgument] = process.argv.slice(2);
assert(emittedArgument && outputArgument, "Usage: prepare.mjs <canonical-standard-Emitted> <build>");
const emitted = path.resolve(emittedArgument);
const output = path.resolve(outputArgument);
assert(output.includes(`${path.sep}no-ui-comparison${path.sep}`),
    "Experimental outputs must stay in the new no-ui-comparison build tree.");
const manifest = JSON.parse(await readFile(path.join(emitted, "manifest.json"), "utf8"));
assert(!manifest.nativeBabylonLayer, "Use the one canonical STANDARD emission.");
const hash = bytes => createHash("sha256").update(bytes).digest("hex");
async function walk(directory, relative = "") {
    const result = [];
    for (const entry of (await readdir(directory, { withFileTypes: true }))
        .sort((a, b) => a.name.localeCompare(b.name))) {
        const name = path.join(relative, entry.name);
        if (entry.isDirectory()) result.push(...await walk(path.join(directory, entry.name), name));
        else result.push(name);
    }
    return result;
}
const inputs = (await walk(emitted)).filter(name => name.endsWith(".cpp") ||
    name.endsWith("application.hpp"));
assert.equal(inputs.filter(name => name.endsWith(".cpp")).length, 26);
assert.equal(inputs.filter(name => name.endsWith("application.hpp")).length, 1);
const records = [];
for (const name of inputs) {
    const source = path.join(emitted, name);
    const destination = path.join(output, "UserSources", name);
    await mkdir(path.dirname(destination), { recursive: true });
    const bytes = await readFile(source);
    try {
        if (hash(await readFile(destination)) !== hash(bytes)) await copyFile(source, destination);
    } catch (error) {
        if (error.code !== "ENOENT") throw error;
        await copyFile(source, destination);
    }
    assert.equal(hash(await readFile(destination)), hash(bytes), name);
    records.push({ file: name.split(path.sep).join("/"), bytes: bytes.length, sha256: hash(bytes) });
}
const header = await readFile(path.join(emitted, "sources", "application.hpp"), "utf8");
const overrides = [];
for (const match of header.matchAll(/^#include <(bblite\/[^>]+)>/gm)) {
    const name = match[1];
    if (name.startsWith("bblite/js_") && name !== "bblite/js_file.hpp") continue;
    if (name === "bblite/uncaught_error.hpp") continue;
    const file = path.join(output, "Override", ...name.split("/"));
    await mkdir(path.dirname(file), { recursive: true });
    await writeFile(file, '#pragma once\n#include "StandardBinding.h"\n');
    overrides.push(name);
}

// Only original manifest DATA is rehydrated, outside the untouched user TUs.
const types = { f32: ["BL_UNIFORM_F32", 1], u32: ["BL_UNIFORM_U32", 1],
    i32: ["BL_UNIFORM_I32", 1], "vec2<f32>": ["BL_UNIFORM_VEC2", 2],
    "vec3<f32>": ["BL_UNIFORM_VEC3", 3], "vec4<f32>": ["BL_UNIFORM_VEC4", 4],
    "mat4x4<f32>": ["BL_UNIFORM_MAT4", 16] };
let cpp = '#include "C99Client.h"\nnamespace bbl\n{\n';
cpp += "const MaterialMetadata& MaterialDescription(uint32_t variant)\n{\nswitch (variant)\n{\n";
for (const [index, shader] of manifest.customShaderPrograms.entries()) {
    assert.equal(shader.storageBuffers.length, 0);
    assert.equal(shader.defines.length, 0);
    cpp += `case ${index}:\n{\n`;
    cpp += `static const bl_VertexSemantic attributes[] = {${shader.attributes
        .map(name => `BL_ATTRIBUTE_${name.toUpperCase()}`).join(",")}};\n`;
    const defaults = new Map(shader.uniformDefaults.map(item => [item.name, item.values]));
    for (const [name, values] of defaults) {
        cpp += `static const double default_${name}[] = {${values.join(",")}};\n`;
    }
    cpp += "static const bl_ShaderUniformDecl uniforms[] = {\n";
    let offset = 0;
    const slots = [];
    for (const declaration of shader.uniforms) {
        const [name, type] = declaration.split(":");
        if (type) assert(types[type], declaration);
        cpp += `{String(${JSON.stringify(name)}),${type ? types[type][0] : "BL_UNIFORM_MAT4"},`;
        cpp += defaults.has(name) ? `{default_${name},${defaults.get(name).length}}` : "{}";
        cpp += `,${!type}},\n`;
        if (type) {
            slots.push(`{${offset},String(${JSON.stringify(name)}),${types[type][1]}}`);
            offset += types[type][1];
        }
    }
    cpp += "};\n";
    cpp += `static const UniformSlot slots[] = {${slots.join(",")}};\n`;
    if (shader.samplers.length) {
        cpp += `static const bl_ShaderSamplerDecl samplers[] = {${shader.samplers
            .map(name => `{String(${JSON.stringify(name)})}`).join(",")}};\n`;
    }
    cpp += "static const MaterialMetadata metadata = []\n{\nMaterialMetadata result{};\n";
    for (const name of ["name", "vertexSource", "fragmentSource"]) {
        cpp += `result.options.${name} = String(${JSON.stringify(shader[name])});\n`;
    }
    cpp += `result.options.attributes=attributes;\nresult.options.attributeCount=${shader.attributes.length};\n`;
    cpp += `result.options.uniforms=uniforms;\nresult.options.uniformCount=${shader.uniforms.length};\n`;
    if (shader.samplers.length) {
        cpp += `result.options.samplers=samplers;\nresult.options.samplerCount=${shader.samplers.length};\n`;
    }
    for (const name of ["needAlphaBlending", "needAlphaTesting", "backFaceCulling", "depthWrite"]) {
        cpp += `result.options.${name}=${shader[name] ? "BL_BOOL_TRUE" : "BL_BOOL_FALSE"};\n`;
    }
    cpp += `result.options.blendMode=${shader.blendMode === "additive" ?
        "BL_BLEND_ADDITIVE" : "BL_BLEND_ALPHA"};\n`;
    cpp += `result.slots=slots;\nresult.slotCount=${slots.length};\nreturn result;\n}();\n`;
    cpp += "return metadata;\n}\n";
}
cpp += 'default: throw std::runtime_error("Unknown canonical material variant.");\n}\n}\n}\n';
await mkdir(path.join(output, "BindingData"), { recursive: true });
await writeFile(path.join(output, "BindingData", "MaterialMetadata.cpp"), cpp);
await writeFile(path.join(output, "canonical-inputs.json"), JSON.stringify({
    standardMode: true, source: emitted, records,
    userCppEdited: false, generatedApplicationHeaderEdited: false,
    worldObserverInjected: false, engineProfilingTagsRemoved: false,
    entryAdaptation: "main=MinecraftApplication only on canonical user-TU compiler commands",
    vendorHeaderOverrides: overrides,
    materialDataSha256: hash(cpp),
    manifestSha256: hash(await readFile(path.join(emitted, "manifest.json"))),
}, null, 2) + "\n");
console.log(`Copied ${records.length} canonical files byte-for-byte; generated binding DATA only.`);
