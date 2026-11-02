import {readFileSync, writeFileSync} from "node:fs";
import path from "node:path";
import {pathToFileURL} from "node:url";

const [corpus, data, output] = process.argv.slice(2);
const {computeBuildStamp} = await import(pathToFileURL(path.join(corpus, "dist", "src", "build-stamp.js")));
const {verifyDeployedPayload} = await import(pathToFileURL(path.join(corpus, "dist", "src", "parity-scene.js")));
for (const name of Object.keys(process.env)) {
    if (name.startsWith("BBLITE_")) delete process.env[name];
}
const scenes = JSON.parse(readFileSync(path.join(data, "scenes.json"), "utf8"));
const results = [];
for (const scene of scenes) {
    const generated = path.join(corpus, "generated", `scene${scene.id}`);
    const computed = computeBuildStamp(generated, corpus);
    const declared = JSON.parse(readFileSync(path.join(generated, "build-inputs.json"), "utf8"));
    let payloadValid = false;
    let payloadError = null;
    try {
        verifyDeployedPayload(scene.executable, generated);
        payloadValid = true;
    } catch (error) {
        payloadError = String(error);
    }
    results.push({scene: scene.id, computedStamp: computed.stamp, declaredStamp: declared.stamp,
        declaredMatchesCurrent: computed.stamp === declared.stamp,
        executableContainsCurrentStamp: readFileSync(scene.executable).includes(Buffer.from(computed.stamp)),
        payloadValid, payloadError, inputs: computed.inputs});
}
writeFileSync(output, JSON.stringify(results, null, 2) + "\n");
for (const result of results) {
    console.log(result.scene, result.declaredMatchesCurrent,
        result.executableContainsCurrentStamp, result.payloadValid);
}
if (results.some(x => !x.executableContainsCurrentStamp || !x.payloadValid)) process.exitCode = 1;
