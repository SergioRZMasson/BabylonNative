import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { createHash } from "node:crypto";

const [packageDirectory, output] = process.argv.slice(2);
const pkg = JSON.parse(fs.readFileSync(path.join(packageDirectory, "package.json"), "utf8"));
if (pkg.version !== "1.32.0" ||
    pkg.babylonLiteRelease?.sourceVersion !== "2e064d88ec7422af946f8ec7f089ac6519f99295") {
    throw new Error("Ground goldens require the reviewed original Lite source pin.");
}
const modulePath = path.join(packageDirectory, "lib", "mesh", "create-ground.js");
const { createFlatGroundData } = await import(pathToFileURL(modulePath));
fs.mkdirSync(output, { recursive: true });
const hash = bytes => createHash("sha256").update(bytes).digest("hex");
const cases = [
    ["default", {}], ["primitives-8x8", { width: 8, height: 8 }],
    ["subdiv-2", { width: 3, height: 5, subdivisions: 2 }],
    ["subdiv-3-uv", { width: 2.5, height: 7.3, subdivisions: 3, uvScale: [1.3, -2.2] }],
    ["zero-width", { width: 0 }], ["negative-width", { width: -2, height: 3 }],
    ["ignored-heightmap-only", { minHeight: 9, maxHeight: -4 }],
];
const files = [];
for (const [name, options] of cases) {
    const data = createFlatGroundData(options);
    for (const stream of ["positions", "normals", "uvs", "indices"]) {
        const bytes = Buffer.from(data[stream].buffer, data[stream].byteOffset, data[stream].byteLength);
        const file = `ground-${name}-${stream}.bin`;
        fs.writeFileSync(path.join(output, file), bytes);
        files.push({ file, sha256: hash(bytes), bytes: bytes.length });
    }
}
fs.writeFileSync(path.join(output, "ground-inventory.json"), JSON.stringify({
    package: pkg.name, version: pkg.version, sourcePin: pkg.babylonLiteRelease.sourceVersion,
    authority: "Original package module and its original TypeScript sourcesContent, not compiler-generated engine",
    originalSourceMapSha256: hash(fs.readFileSync(modulePath + ".map")),
    license: "Apache-2.0; existing fixture/source LICENSE retained",
    cases: Object.fromEntries(cases), files,
}, null, 2) + "\n");
