import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import path from "node:path";
import { pathToFileURL, fileURLToPath } from "node:url";
import { createHash } from "node:crypto";

const [compiler, output] = process.argv.slice(2);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "..");
const entry = path.join(compiler, "corpus", "babylon-lite", "lab", "lite", "src", "lite", "scene38.ts");
const hash = bytes => createHash("sha256").update(bytes).digest("hex");
const bytes = readFileSync(entry);
if (hash(bytes) !== "e38a7be866dd6ce15b1d42c3204d6f2cf4db14f42a8e155c0fbb17d280b0e95c") {
    throw new Error("Original scene38 source changed.");
}
const { build } = await import(pathToFileURL(path.join(root, "Apps", "LiteTests",
    "LitePlayground", "JavaScript", "node_modules", "esbuild", "lib", "main.js")));
mkdirSync(output, { recursive: true });
const shim = path.join(root, "Plugins", "LiteJSBinding", "JavaScript", "babylon-lite.js");
const result = await build({
    entryPoints: [entry], bundle: true, format: "iife", platform: "browser",
    target: "es2021", outfile: path.join(output, "scene38.native.js"), metafile: true,
    plugins: [{ name: "native-only", setup(builder) {
        builder.onResolve({ filter: /^babylon-lite$/ }, () => ({ path: shim }));
    } }],
});
const inputs = Object.keys(result.metafile.inputs).map(p => path.resolve(p));
if (inputs.some(p => p !== path.resolve(entry) && p !== path.resolve(shim))) {
    throw new Error("Engine JS in scene38 bundle: " + inputs.join(","));
}
writeFileSync(path.join(output, "bundle-receipt.json"), JSON.stringify({
    source: entry, sha256: hash(bytes), shim, shimSha256: hash(readFileSync(shim)),
    bundleSha256: hash(readFileSync(path.join(output, "scene38.native.js"))),
    inputs, noEngineJs: true, classification: "ORIGINAL Babylon Lite corpus scene38",
    originalAttachedControls: false, originalAnimationCallbacks: 0,
}, null, 2) + "\n");
