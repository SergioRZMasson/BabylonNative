import {readFileSync, writeFileSync} from "node:fs";
import path from "node:path";
import {pathToFileURL, fileURLToPath} from "node:url";
import {createHash} from "node:crypto";

const [dataArgument] = process.argv.slice(2);
const data = path.resolve(dataArgument);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "..");
const {build} = await import(pathToFileURL(path.join(root, "Apps", "LiteTests",
    "LitePlayground", "JavaScript", "node_modules", "esbuild", "lib", "main.js")));
const shim = path.join(root, "Plugins", "LiteJSBinding", "JavaScript", "babylon-lite.js");
const platform = path.join(path.dirname(fileURLToPath(import.meta.url)), "platform.js");
const hash = file => createHash("sha256").update(readFileSync(file)).digest("hex");
const scenes = JSON.parse(readFileSync(path.join(data, "scenes.json"), "utf8"));
scenes.push({id: "projection", entry: path.join(path.dirname(fileURLToPath(import.meta.url)),
    "projection-tests.js")});
const receipts = [];
for (const scene of scenes) {
    const entry = scene.entry ?? path.join(data, "source", "lite", `scene${scene.id}.ts`);
    const outfile = path.join(data, `scene${scene.id}.js`);
    const result = await build({
        absWorkingDir: root,
        entryPoints: [entry], bundle: true, format: "iife", platform: "browser",
        target: "es2021", outfile, metafile: true,
        banner: {js: readFileSync(platform, "utf8")},
        plugins: [{name: "native-only", setup(builder) {
            builder.onResolve({filter: /^babylon-lite$/}, () => ({path: shim}));
        }}],
    });
    const inputs = Object.keys(result.metafile.inputs).map(p => path.resolve(root, p));
    if (inputs.some(p => p !== shim && p !== scene.entry &&
        !p.startsWith(path.resolve(data, "source") + path.sep))) {
        throw new Error("Non-user-code JS input: " + inputs.join(","));
    }
    receipts.push({id: scene.id, inputs, bundleSha256: hash(outfile),
        sourceSha256: hash(entry), shimSha256: hash(shim), platform,
        platformSha256: hash(platform), noEngineJs: true});
}
writeFileSync(path.join(data, "bundles.json"), JSON.stringify(receipts, null, 2) + "\n");
