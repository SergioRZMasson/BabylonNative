import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const [file, comparison] = process.argv.slice(2);
assert(file, "Usage: verify-run.mjs <summary.json> [identical-idle-summary.json]");
const receipt = JSON.parse(await readFile(file, "utf8"));
for (const [key, expected] of Object.entries({
    seed: 1337, worldSeed: 1337, radius: 6, width: 1280, height: 720,
    chunks: 169, materialCount: 8,
})) assert.equal(receipt[key], expected, key);
assert.equal(receipt.fixedDeltaMs, 1000 / 60);
assert(receipt.draws > 0);
assert(receipt.measurementScope && receipt.coreMeasurementScope);
if (receipt.cpuSamplesMs.length) {
    for (const key of ["cpuSamplesMs", "coreCpuSamplesMs", "totalHostCpuSamplesMs", "gpuSamplesMs",
        "gpuFrames", "drawSamples"]) assert.equal(receipt[key].length, receipt.frames, key);
    assert(receipt.cpuSamplesMs.every(value => Number.isFinite(value) && value > 0));
    assert.equal(receipt.captures, 0, "Benchmark must not capture.");
}
if (receipt.fileDialogs) {
    assert.equal(receipt.fileDialogs, 2);
    assert.equal(receipt.saveLoadVerified, true);
    assert(receipt.blockEdits > 0);
}
if (comparison) {
    const other = JSON.parse(await readFile(comparison, "utf8"));
    assert.equal(receipt.inputEvents, 0);
    assert.equal(other.inputEvents, 0);
    for (const key of ["frames", "worldHash", "initialWorldHash", "cameraPosition", "cameraTarget",
        "timeOfDay", "chunks", "mobs", "draws", "materialCount", "captureHash"]) {
        assert.deepEqual(receipt[key], other[key], `Nondeterministic idle ${key}`);
    }
}
console.log("Native Minecraft receipt verified: complete original workload and explicit timing scope.");
