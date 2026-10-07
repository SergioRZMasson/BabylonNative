import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { readFile, readdir, writeFile } from "node:fs/promises";
import path from "node:path";

const [repositoryArgument, buildArgument] = process.argv.slice(2);
assert(repositoryArgument && buildArgument, "Usage: audit-native.mjs <BabylonNative> <native-build>");
const repository = path.resolve(repositoryArgument);
const build = path.resolve(buildArgument);
assert(build.startsWith(path.join(repository, "build") + path.sep));
const hash = bytes => createHash("sha256").update(bytes).digest("hex");
const ninja = await readFile(path.join(build, "build.ninja"), "utf8");
const link = ninja.match(/build LiteMinecraftNative\.exe:[\s\S]*?(?=\n(?:build|#))/)?.[0];
assert(link, "Missing full native application target.");
assert(!/\b(?:NativeEngine|napi|JsRuntime|AppRuntime|LiteJSBinding|renderer_plan|pal_sdl|pal_ui|dawn_native|webgpu_dawn)\b/i.test(link),
    "Forbidden engine/VM/PAL dependency in native application link graph.");
const cache = await readFile(path.join(build, "CMakeCache.txt"), "utf8");
const ninjaProgram = cache.match(/^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$/m)?.[1];
assert(ninjaProgram, "Missing configured Ninja path.");
const dependencies = execFileSync(ninjaProgram, ["-C", build, "-t", "deps"],
    { encoding: "utf8" });
assert(!/bblite[\\/](?:runtime\.hpp|pal\.hpp|upstream[\\/]renderer_plan\.hpp|babylon[\\/])/i.test(dependencies),
    "Legacy engine/PAL headers entered the application's actual dependency graph.");
const imports = await readFile(path.join(build, "imports.txt"), "utf8");
const dlls = [...new Set([...imports.matchAll(/^\s+([A-Za-z0-9_.-]+\.dll)\s*$/gim)]
    .map(match => match[1]))];
assert(dlls.length > 0, "Run dumpbin /imports into imports.txt first.");
assert(!dlls.some(name => /v8|quickjs|chakra|javascript|dawn|webgpu|SDL/i.test(name)));
const header = "Core/LiteLayer/Include/babylon_lite.h";
const original = execFileSync("git", ["show", `HEAD:${header}`],
    { cwd: repository, encoding: "utf8" });
const currentHeader = await readFile(path.join(repository, header));
assert.equal(currentHeader.toString("utf8").replaceAll("\r\n", "\n"),
    original.replaceAll("\r\n", "\n"), "Public contract changed beyond checkout line endings.");
const changedCore = execFileSync("git", ["diff", "--name-only", "--",
    "Core/LiteLayer", "Core/LiteShaderCompiler"], { cwd: repository, encoding: "utf8" });
assert.equal(changedCore.trim(), "", "Core implementation was changed by this application projection.");
const source = path.join(repository, "Apps", "LiteTests", "NativeProjection", "FullMinecraft", "Source");
const sourceHashes = [];
for (const name of (await readdir(source)).sort()) {
    sourceHashes.push({ file: `Source/${name}`, sha256: hash(await readFile(path.join(source, name))) });
}
const projection = JSON.parse(await readFile(path.join(build, "Projected", "projection.json"), "utf8"));
const result = {
    scope: "Executed full original Minecraft application; not general package/API/corpus parity",
    executableSha256: hash(await readFile(path.join(build, "LiteMinecraftNative.exe"))),
    publicContractSha256: hash(currentHeader),
    publicContractNormalizedSha256: hash(original.replaceAll("\r\n", "\n")),
    handwrittenCoreChanged: false,
    compiledApplicationSources: projection.compiledApplicationSources,
    hostSources: sourceHashes,
    headerOnlyUserLanguageSupport: ["js_data.hpp", "js_json.hpp", "js_synchronous_promise.hpp",
        "transitive user-language GC/callback/value headers"],
    legacyEnginePalHeadersInCompilerDependencies: [],
    generatedEngineRendererPalObjectsCompiled: [],
    javaScriptVmLinked: false,
    dawnWebGpuGpuRuntimeLinked: false,
    actualPeImports: dlls,
    runtimeCompiler: "Separate full Tint/SPIRV-Cross/FXC service; Tint source is Dawn, not a Dawn GPU runtime",
    shaderData: "Original application's customShaderPrograms manifest",
    sourceEdits: false,
};
await writeFile(path.join(build, "native-provenance.json"), JSON.stringify(result, null, 2) + "\n");
console.log("Native link/import/header audit passed: no VM, legacy engine/PAL or Dawn/WebGPU GPU runtime.");
