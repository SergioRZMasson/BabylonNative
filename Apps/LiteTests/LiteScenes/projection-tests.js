import {
    createEngine, createTransformNode, createArcRotateCamera, createSceneContext, attachControl,
    setMeshVisible, loadGltf, disposeAssetContainer,
    playAnimation, pauseAnimation, stopAnimation, goToFrame,
    createBox, createStandardMaterial, createHemisphericLight, createDirectionalLight,
    addToScene, removeFromScene, getContainerMeshes,
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
    engine.useHighPrecisionMatrix = true;
    check(engine.useHighPrecisionMatrix === true, "Source plain precision metadata was not writable.");
    const precisionNode = createTransformNode("creation-precision");
    precisionNode.position.x = 16777217;
    check(precisionNode.worldMatrix[12] === 16777216,
        "Writing source precision metadata changed creation-time matrix storage.");
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
    const inspectionScene = createSceneContext(engine);
    const inspected = createBox(engine, {size: 2});
    inspected.position.x = 100;
    const standard = createStandardMaterial();
    inspected.material = standard;
    const hemi = createHemisphericLight([0, 1, 0], 1);
    const directional = createDirectionalLight([1, -1, 0], 1);
    const meshesView = inspectionScene.meshes;
    addToScene(inspectionScene, inspected);
    addToScene(inspectionScene, hemi);
    addToScene(inspectionScene, inspected);
    addToScene(inspectionScene, directional);
    check(meshesView === inspectionScene.meshes && meshesView.length === 2 &&
        meshesView[0] === inspected && meshesView[1] === inspected,
        "Native scene inspection lost live membership, order, duplicate or wrapper identity.");
    check(Object.keys(meshesView).length === 2 && 1 in meshesView &&
        Object.getOwnPropertyDescriptor(meshesView, "length").value === 2,
        "Native-owned collection enumeration did not expose current native membership.");
    check(inspectionScene.lights[0] === hemi && inspectionScene.lights[1] === directional,
        "Scene light family identities were not preserved.");
    check(inspected.boundMin[0] === -1 && inspected.boundMax[0] === 1,
        "Bounds inspection incorrectly applied the node's world translation.");
    check(inspected.material === standard && typeof standard.name === "string",
        "Material name inspection did not use the original material identity.");
    reject(() => meshesView.push(inspected), "Native-owned collection accepted an independent JS write.");
    reject(() => delete meshesView[0], "Native-owned collection silently accepted deletion.");
    reject(() => Object.defineProperty(meshesView, "0", {value: inspected}),
        "Native-owned collection accepted independent JS state.");
    removeFromScene(inspectionScene, hemi);
    check(inspectionScene.lights.length === 1 && inspectionScene.lights[0] === directional,
        "Native light removal did not invalidate collection inspection.");
    const positions = new Float32Array([-1, -2, -3, 4, 5, 6, 0, 1, 2]);
    const indices = new Uint32Array([0, 1, 2]);
    const model = JSON.stringify({
        asset: {version: "2.0"}, scene: 0, scenes: [{nodes: [0]}],
        nodes: [{mesh: 0, translation: [50, 0, 0]}],
        meshes: [{primitives: [{attributes: {POSITION: 0}, indices: 1, material: 0}]}],
        materials: [{name: "inspection-native", pbrMetallicRoughness: {}}],
        buffers: [{byteLength: 48}],
        bufferViews: [{buffer: 0, byteLength: 36}, {buffer: 0, byteOffset: 36, byteLength: 12}],
        accessors: [{bufferView: 0, componentType: 5126, count: 3, type: "VEC3",
            min: [-1, -2, -3], max: [4, 5, 6]},
        {bufferView: 1, componentType: 5125, count: 3, type: "SCALAR"}],
    });
    const jsonBytes = new Uint8Array(Array.from(model, c => c.charCodeAt(0)));
    const jsonSize = (jsonBytes.length + 3) & ~3;
    const glb = new Uint8Array(12 + 8 + jsonSize + 8 + 48);
    const glbHeader = new DataView(glb.buffer);
    for (const [offset, value] of [[0, 0x46546c67], [4, 2], [8, glb.length],
        [12, jsonSize], [16, 0x4e4f534a], [20 + jsonSize, 48], [24 + jsonSize, 0x004e4942]]) {
        glbHeader.setUint32(offset, value, true);
    }
    glb.fill(32, 20, 20 + jsonSize);
    glb.set(jsonBytes, 20);
    glb.set(new Uint8Array(positions.buffer), 28 + jsonSize);
    glb.set(new Uint8Array(indices.buffer), 28 + jsonSize + 36);
    const loaded = await loadGltf(engine, glb);
    const genericNode = loaded.entities[0].children[0].children[0];
    const loadedMeshes = getContainerMeshes(loaded);
    check(loadedMeshes.length === 1 && loadedMeshes[0] === genericNode,
        "Container traversal changed an already-projected generic mesh identity.");
    check(loadedMeshes[0].material.name === "inspection-native" &&
        loadedMeshes[0].boundMin[1] === -2 && loadedMeshes[0].boundMax[2] === 6,
        "Imported material name/local bounds inspection was fabricated.");
    disposeAssetContainer(loaded);
    reject(() => getContainerMeshes(loaded), "Disposed container inspection was accepted.");
    reject(() => loadedMeshes[0].boundMin, "Disposed mesh bounds were accepted.");
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
