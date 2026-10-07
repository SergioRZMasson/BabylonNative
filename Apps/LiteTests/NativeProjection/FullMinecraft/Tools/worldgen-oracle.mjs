import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";

const [checkout, demos] = process.argv.slice(2);
assert(checkout && demos, "Usage: worldgen-oracle.mjs <bblitec> <original-demos>");
const require = createRequire(path.resolve(checkout, "package.json"));
const ts = require("typescript");
const cache = new Map();
function load(file) {
    file = path.resolve(file);
    if (cache.has(file)) return cache.get(file).exports;
    const module = { exports: {} };
    cache.set(file, module);
    const javascript = ts.transpileModule(readFileSync(file, "utf8"), {
        compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 },
        fileName: file,
    }).outputText;
    const localRequire = name => {
        assert(name.startsWith("."), `The terrain oracle must not import an engine/runtime: ${name}`);
        return load(path.resolve(path.dirname(file), name.replace(/\.js$/, ".ts")));
    };
    new Function("exports", "require", "module", javascript)(module.exports, localRequire, module);
    return module.exports;
}
const { generateChunk } = load(path.resolve(demos, "minecraft", "worldgen.ts"));
let digest = 1469598103934665603n;
let nonzero = 0;
let chunks = 0;
for (let z = -6; z <= 6; ++z) {
    for (let x = -6; x <= 6; ++x) {
        const blocks = new Uint8Array(16 * 16 * 96);
        generateChunk(1337, x, z, blocks);
        for (const block of blocks) {
            digest = BigInt.asUintN(64, (digest ^ BigInt(block)) * 1099511628211n);
            nonzero += block !== 0;
        }
        ++chunks;
    }
}
console.log(JSON.stringify({ seed: 1337, radius: 6, chunks, nonzero, hash: String(digest),
    scope: "Original user terrain code only; not native rendering/gameplay proof" }));
