import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import path from "node:path";
import { pathToFileURL, fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
const [compiler, output] = process.argv.slice(2);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "..");
const entry = path.join(compiler, "corpus", "babylon-lite", "lab", "lite", "src", "lite", "scene2.ts");
const hash = bytes => createHash("sha256").update(bytes).digest("hex");
const bytes = readFileSync(entry);
if (hash(bytes) !== "4f62cb2b45f0dcc128f32d40ba357247b42738ee519929deaa839db890c6b05f")
    throw new Error("Original corpus scene2 source changed.");
const { build } = await import(pathToFileURL(path.join(root, "Apps", "LiteTests",
    "LitePlayground", "JavaScript", "node_modules", "esbuild", "lib", "main.js")));
mkdirSync(output, { recursive: true });
const shim = path.join(root, "Plugins", "LiteJSBinding", "JavaScript", "babylon-lite.js");
const result = await build({
    entryPoints: [entry], bundle: true, format: "iife", platform: "browser",
    target: "es2021", outfile: path.join(output, "scene2.native.js"), metafile: true,
    plugins: [{ name: "native-only", setup(builder) {
        builder.onResolve({ filter: /^babylon-lite$/ }, () => ({ path: shim }));
    } }],
});
const inputs = Object.keys(result.metafile.inputs).map(p => path.resolve(p));
if (inputs.some(p => p !== path.resolve(entry) && p !== path.resolve(shim)))
    throw new Error(`Engine JS in original scene2 bundle: ${inputs}`);
writeFileSync(path.join(output, "bundle-receipt.json"), JSON.stringify({
    source: entry, sha256: hash(bytes), shim, shimSha256: hash(readFileSync(shim)),
    inputs, noEngineJs: true, classification: "Original Babylon Lite corpus scene2",
}, null, 2));
