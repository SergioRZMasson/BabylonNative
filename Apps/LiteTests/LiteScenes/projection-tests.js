import {
    createEngine, createTransformNode, createArcRotateCamera, createSceneContext, attachControl,
    setMeshVisible, loadGltf, disposeAssetContainer,
    playAnimation, pauseAnimation, stopAnimation, goToFrame,
} from "babylon-lite";

function check(value, message) {
    if (!value) throw new Error(message);
}

function reject(action, message) {
    let failed = false;
    try { action(); } catch { failed = true; }
    check(failed, message);
}

async function main() {
    const canvas = document.getElementById("renderCanvas");
    const engine = await createEngine(canvas, {useHighPrecisionMatrix: false, msaaSamples: 4});
    check(engine.useHighPrecisionMatrix === false, "Precision creation option was not exposed.");
    reject(() => { engine.useHighPrecisionMatrix = true; }, "Precision mutation was silently ignored.");
    for (const options of [{msaaSamples: 8}, {requiredLimits: {maxColorAttachmentBytesPerSample: 64}},
        {maxDevicePixelRatio: 2}, {useFloatingOrigin: true}]) {
        let failed = false;
        try { await createEngine(canvas, options); } catch { failed = true; }
        check(failed, "Unsupported engine option was silently ignored.");
    }
    const parent = createTransformNode("visibility-parent");
    const child = createTransformNode("visibility-child");
    parent.children.push(child);
    setMeshVisible(parent, false);
    check(!parent.visible && !child.visible, "setMeshVisible did not reach native subtree state.");
    setMeshVisible(parent, true);
    check(parent.visible && child.visible, "setMeshVisible did not restore native subtree state.");
    const scene = createSceneContext(engine);
    const camera = createArcRotateCamera(0.4, 1, 5, {x: 0, y: 0, z: 0});
    attachControl(camera, canvas, scene);
    const pointer = (type, x) => ({type, pointerType: "mouse", pointerId: 1, button: 0,
        clientX: x, clientY: 100, preventDefault() { this.defaultPrevented = true; }});
    canvas.dispatchEvent(pointer("pointerdown", 100));
    canvas.dispatchEvent(pointer("pointermove", 120));
    check(camera.inertialAlphaOffset !== 0, "Canvas listeners did not retain/forward native controls.");
    canvas.dispatchEvent(pointer("pointerup", 120));

    // Keep the fixture independent of browser btoa/Blob/network services.
    const base64 = "AAAAAAAAgD8AAAAAAAAAAAAAAAAAAABAAAAAAAAAAAA=";
    const json = JSON.stringify({
        asset: {version: "2.0"}, scenes: [{nodes: [0]}], scene: 0,
        nodes: [{name: "animated"}],
        buffers: [{byteLength: 32, uri: "data:application/octet-stream;base64," + base64}],
        bufferViews: [{buffer: 0, byteLength: 8}, {buffer: 0, byteOffset: 8, byteLength: 24}],
        accessors: [{bufferView: 0, componentType: 5126, count: 2, type: "SCALAR", min: [0], max: [1]},
            {bufferView: 1, componentType: 5126, count: 2, type: "VEC3"}],
        animations: [{name: "translate", samplers: [{input: 0, output: 1}],
            channels: [{sampler: 0, target: {node: 0, path: "translation"}}]}],
    });
    const payload = new Uint8Array(Array.from(json, c => c.charCodeAt(0)));
    const container = await loadGltf(engine, payload);
    const groups = container.animationGroups;
    check(groups === container.animationGroups && groups.length === 1,
        "Animation collection identity/count changed.");
    const group = groups[0];
    check(group.name === "translate" && group.duration === 1 && group.frameRate === 60,
        "Animation info did not reach the native group.");
    check(group.isPlaying && !group._stopped, "First animation did not autoplay.");
    pauseAnimation(group);
    check(!group.isPlaying, "Pause was not forwarded.");
    group.speedRatio = -2;
    group.loopAnimation = false;
    check(group.speedRatio === -2 && !group.loopAnimation, "Playback fields were not forwarded.");
    goToFrame(group, 30, engine);
    check(group.currentTime === 0.5 && !group.isPlaying, "Seek was not forwarded.");
    check(container.entities[0].children[0].worldMatrix[12] === -1,
        "Seek changed playback metadata without evaluating the native node pose.");
    reject(() => { group.currentTime = 0.75; }, "Unsupported direct time write was silently ignored.");
    reject(() => { group.isPlaying = true; }, "Unsupported direct playing write was silently ignored.");
    check(group.currentTime === 0.5 && !group.isPlaying, "Rejected direct writes changed native state.");
    playAnimation(group);
    check(group.isPlaying && !group._stopped, "Play was not forwarded.");
    stopAnimation(group);
    check(!group.isPlaying && group._stopped && group.currentTime === 0, "Stop was not forwarded.");
    reject(() => { group.speedRatio = NaN; }, "Invalid playback speed was silently accepted.");
    check(group.speedRatio === -2, "Failed playback write changed native state.");
    reject(() => goToFrame(parent, 10), "Wrong-kind animation identity was accepted.");
    disposeAssetContainer(container);
    reject(() => group.duration, "Disposed container left an apparently live animation group.");
    canvas.dataset.ready = "true";
}

main().catch(error => {
    console.error(error);
    document.getElementById("renderCanvas").dataset.error = String(error);
});
