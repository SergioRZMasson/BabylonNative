import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import path from "node:path";
import { pathToFileURL, fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
const [compiler, output] = process.argv.slice(2);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "..");
const entry = path.join(compiler, "examples", "primitives.ts");
const bytes = readFileSync(entry);
if (createHash("sha256").update(bytes).digest("hex") !==
    "af87a595fdb4af02bf9bd98f86dc2081f038946899c8fbe1ed907aa6b17b26c4") {
    throw new Error("Original bblitec-owned primitives bytes changed.");
}
const { build } = await import(pathToFileURL(path.join(root, "Apps", "LiteTests",
    "LitePlayground", "JavaScript", "node_modules", "esbuild", "lib", "main.js")));
mkdirSync(output, { recursive: true });
const shim = path.join(root, "Plugins", "LiteJSBinding", "JavaScript", "babylon-lite.js");
const result = await build({
    entryPoints: [entry], bundle: true, format: "iife", platform: "browser",
    target: "es2021", outfile: path.join(output, "primitives.native.js"), metafile: true,
    plugins: [{ name: "native-only", setup(builder) {
        builder.onResolve({ filter: /^@babylonjs\/lite$/ }, () => ({ path: shim }));
    } }],
});
const inputs = Object.keys(result.metafile.inputs).map(p => path.resolve(p));
if (inputs.some(p => p !== path.resolve(entry) && p !== path.resolve(shim))) {
    throw new Error(`Engine JS in auxiliary cube bundle: ${inputs}`);
}
writeFileSync(path.join(output, "bundle-receipt.json"), JSON.stringify({
    source: entry, sha256: createHash("sha256").update(bytes).digest("hex"),
    inputs, noEngineJs: true, classification: "Auxiliary bblitec-owned sample",
}, null, 2));
