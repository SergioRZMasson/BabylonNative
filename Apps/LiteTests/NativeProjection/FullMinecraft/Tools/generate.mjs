import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { copyFile, mkdir, readFile, readdir, writeFile } from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";

const [checkoutArgument, nativeArgument, outputArgument] = process.argv.slice(2);
assert(checkoutArgument && nativeArgument && outputArgument, "Usage: generate.mjs <bblitec> <BabylonNative> <ignored-output>");
const checkout = path.resolve(checkoutArgument);
const native = path.resolve(nativeArgument);
const output = path.resolve(outputArgument);
assert(output.startsWith(path.join(native, "build") + path.sep), "Full application generation must stay in the ignored project build tree.");
const original = path.join(native, "Apps", "LiteTests", "LitePlayground", "JavaScript", "Demos");
const stage = path.join(output, "Input");
const demos = path.join(stage, "Demos");
const sha256 = bytes => createHash("sha256").update(bytes).digest("hex");
const sourceFiles = [];
const assetFiles = [];
async function copyTree(from, to, relative = "") {
    await mkdir(to, { recursive: true });
    for (const item of (await readdir(from, { withFileTypes: true })).sort((a, b) => a.name.localeCompare(b.name))) {
        const source = path.join(from, item.name);
        const destination = path.join(to, item.name);
        const name = path.join(relative, item.name);
        if (item.isDirectory()) {
            await copyTree(source, destination, name);
        } else {
            await copyFile(source, destination);
            const bytes = await readFile(source);
            assert.equal(sha256(await readFile(destination)), sha256(bytes));
            const record = { path: name.split(path.sep).join("/"), sha256: sha256(bytes), bytes: bytes.length };
            if (item.name.endsWith(".ts")) sourceFiles.push(record);
            if (item.name.endsWith(".png") || item.name === "License.txt") assetFiles.push(record);
        }
    }
}
await copyTree(original, demos);
const pack = path.join(demos, "minecraft", "voxelpack");
const deployed = path.join(native, "Apps", "LiteTests", "LitePlayground", "Assets", "minecraft", "voxelpack");
await mkdir(pack, { recursive: true });
for (const name of await readdir(deployed)) {
    if (name.endsWith(".png") || name === "License.txt") {
        await copyFile(path.join(deployed, name), path.join(pack, name));
        const bytes = await readFile(path.join(deployed, name));
        assert.equal(sha256(await readFile(path.join(pack, name))), sha256(bytes));
        assetFiles.push({ path: `minecraft/voxelpack/${name}`, sha256: sha256(bytes),
            bytes: bytes.length });
    }
}
// The existing canvas packager discovers its closure root from these two
// metadata files, independently of the compiler checkout/cwd.
await mkdir(path.join(stage, "upstream"), { recursive: true });
await copyFile(path.join(checkout, "upstream", "babylon-lite.json"), path.join(stage, "upstream", "babylon-lite.json"));
await writeFile(path.join(stage, "package.json"), JSON.stringify({
    name: "babylon-lite-source-identical-application-staging", private: true, type: "module",
}));
const entry = path.join(demos, "minecraft.ts");
const source = await readFile(entry, "utf8");
assert(/\bconst SEED = 1337;/.test(source) && /\bconst RENDER_RADIUS = 6;/.test(source));
assert.equal(sourceFiles.filter(record => record.path.startsWith("minecraft/")).length, 24);
process.chdir(checkout);
const { compileSource } = await import(pathToFileURL(path.join(checkout, "dist", "src", "compiler.js")));
const started = Date.now();
const result = compileSource(source, { fileName: entry, publicDir: demos, nativeBabylonLayer: true, width: 1280, height: 720 });
const generated = path.join(output, "Emitted");
await mkdir(generated, { recursive: true });
const emitted = [];
for (const [name, text] of result.cppFiles) {
    const file = path.join(generated, name);
    await mkdir(path.dirname(file), { recursive: true });
    await writeFile(file, text);
    emitted.push({ path: name, sha256: sha256(text), bytes: Buffer.byteLength(text) });
}
await writeFile(path.join(generated, "manifest.json"), JSON.stringify(result.manifest, null, 2) + "\n");
await mkdir(path.join(generated, "Assets"), { recursive: true });
for (const [name, payload] of result.assetPayloads) {
    const asset = result.manifest.assets.find(value => value.source === name);
    assert(asset, `Missing payload manifest entry: ${name}`);
    const file = path.join(generated, "Assets", asset.output);
    await mkdir(path.dirname(file), { recursive: true });
    const bytes = typeof payload === "string" && payload.startsWith("data:") ?
        Buffer.from(payload.slice(payload.indexOf(",") + 1), "base64") : payload;
    await writeFile(file, bytes);
}
const applicationText = [...result.cppFiles.values()].join("\n");
function calls(prefix) {
    const escaped = prefix.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    return [...new Set([...applicationText.matchAll(new RegExp(`\\b${escaped}([A-Za-z0-9_]+)\\s*\\(`, "g"))].map(match => match[1]))].sort();
}
const headers = [...new Set([...applicationText.matchAll(/^#include\s+<([^>]+)>/gm)].map(match => match[1]))].sort();
const nativeCalls = calls("bbl::babylon::");
const palCalls = calls("bbl::pal::");
const internalCalls = calls("bbl::");
const blockers = {
    classification: "C++ output-projection/ABI integration gap, not a full-Minecraft TypeScript lowering refusal",
    emittedNativeCppFacadeCalls: nativeCalls,
    emittedPalCalls: palCalls,
    emittedInternalRuntimeCalls: internalCalls,
    directlyAccessedEngineContainers: [...new Set([...applicationText.matchAll(/\bv_engine\.(meshes|cameras|materials|textures|scenes|nodes)\b/g)].map(match => match[1]))].sort(),
    incompatiblePublicApiExamples: [
        { emitted: "createShaderMaterial(engine, variantIndex)", current: "bl_createShaderMaterial(runtime, bl_ShaderMaterialOptions*, result)", consequence: "Requires explicit original WGSL/options and per-material identity metadata projection, not copying renderer_plan/material bodies." },
        { emitted: "setSceneShaderUniform(engine, variantIndex, scalarOffset, values...)", current: "bl_setShaderUniform(material, originalName, values)", consequence: "Compiler output uses internal variant/offset storage; a correct per-instance mapping must be recovered from lowering metadata." },
        { emitted: "handle_at(engine.meshes, handle).position/scaling/render_order", current: "bl_get/setNodePosition/Scaling and bl_get/setMeshProperties", consequence: "Engine record fields cannot be replaced by a scalar symbol rename; C99-backed property/identity adapters are required." },
    ],
    forbiddenDependencySuggestionsExcluded: {
        runtimeSources: result.manifest.runtimeSources,
        generatedEngineSources: result.manifest.generatedSources,
    },
};
await writeFile(path.join(output, "projection-boundaries.json"), JSON.stringify(blockers, null, 2) + "\n");
await writeFile(path.join(output, "provenance.json"), JSON.stringify({
    sourcePin: "2e064d88ec7422af946f8ec7f089ac6519f99295",
    seed: 1337, radius: 6, sourceFiles, assetFiles, emitted,
    compilerCommit: execFileSync("git", ["rev-parse", "HEAD"], { cwd: checkout, encoding: "utf8" }).trim(),
    compilerSha256: sha256(await readFile(path.join(checkout, "dist", "src", "compiler.js"))),
    buildStampSha256: sha256(await readFile(path.join(checkout, "dist", ".build-stamp"))),
    features: result.manifest.features,
    adaptations: result.manifest.adaptations,
    generatedEngineObjectsCompiled: [],
    rendererPalObjectsCompiled: [],
    nativeCppGameExecuted: false,
    frontendSucceeded: true,
    elapsedMs: Date.now() - started,
    note: "27 emitted user-code files do not imply a runnable C99/native game. Existing renderer/PAL/engine suggestions are recorded and deliberately never compiled.",
}, null, 2) + "\n");
console.log(JSON.stringify({ frontendSucceeded: true, sourceFiles: sourceFiles.length, minecraftModules: 24,
    emittedFiles: emitted.length, features: result.manifest.features.length, nativeFacadeCalls: nativeCalls.length,
    palCalls: palCalls.length, internalCalls: internalCalls.length, nativeCppGameExecuted: false }));
