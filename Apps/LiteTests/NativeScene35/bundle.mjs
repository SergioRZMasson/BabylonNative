import {readFileSync, writeFileSync, mkdirSync} from "node:fs";
import path from "node:path";
import {pathToFileURL, fileURLToPath} from "node:url";
import {createHash} from "node:crypto";

const [source, output] = process.argv.slice(2);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "..");
const entry = path.join(source, "scene35.ts");
const hash = bytes => createHash("sha256").update(bytes).digest("hex");
const bytes = readFileSync(entry);
if (hash(bytes) !== "a1f7c456e5b89ea608b0f54ba1ea916a1d418b674b25cb321b9525763e4fd1e2") {
    throw new Error("Original scene35 source changed.");
}
const {build} = await import(pathToFileURL(path.join(root, "Apps", "LiteTests",
    "LitePlayground", "JavaScript", "node_modules", "esbuild", "lib", "main.js")));
mkdirSync(output, {recursive: true});
const shim = path.join(root, "Plugins", "LiteJSBinding", "JavaScript", "babylon-lite.js");
const result = await build({
    entryPoints: [entry], bundle: true, format: "iife", platform: "browser",
    target: "es2021", outfile: path.join(output, "scene35.native.js"), metafile: true,
    plugins: [{name: "native-only", setup(builder) {
        builder.onResolve({filter: /^babylon-lite$/}, () => ({path: shim}));
    }}],
});
const inputs = Object.keys(result.metafile.inputs).map(p => path.resolve(p));
if (inputs.some(p => p !== path.resolve(entry) && p !== path.resolve(shim))) {
    throw new Error("Engine JS in scene35 bundle: " + inputs.join(","));
}
writeFileSync(path.join(output, "bundle-receipt.json"), JSON.stringify({
    source: entry, sha256: hash(bytes), shim, shimSha256: hash(readFileSync(shim)),
    bundleSha256: hash(readFileSync(path.join(output, "scene35.native.js"))),
    inputs, noEngineJs: true, sourceAttachedControls: 1, sourceAnimationCallbacks: 0,
    classification: "ORIGINAL Babylon Lite 1.32.0 scene35; blocking fulfilled Promise adaptation",
}, null, 2) + "\n");
