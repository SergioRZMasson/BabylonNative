import {
    createEngine, createSceneContext, createArcRotateCamera, attachControl, setCameraLimits,
    onBeforeRender, registerScene, renderFrame, startEngine, createHemisphericLight,
    createStandardMaterial, createBox, addToScene, markMaterialUboDirty, rebuildMaterial,
} from "../../../Plugins/LiteJSBinding/JavaScript/babylon-lite.js";

const assert = (condition, message) => { if (!condition) throw new Error(message); };
async function main() {
    const engine = await createEngine({ width: 1280, height: 720 });
    const scene = createSceneContext(engine);
    const camera = createArcRotateCamera(0, 1, 5, { x: 0, y: 0, z: 0 });
    scene.camera = camera;
    const listeners = new Map();
    let captureId;
    const canvas = {
        addEventListener: (kind, fn) => listeners.set(kind, fn),
        removeEventListener: kind => listeners.delete(kind),
        setPointerCapture: id => { captureId = id; },
        releasePointerCapture() {},
    };
    const event = (type, x) => ({
        type, button: 0, pointerType: "mouse", pointerId: 9, clientX: x, clientY: 0,
        target: canvas, preventDefault() {},
    });
    const options = { pointerMappings: { primaryButton: "rotate" } };
    let received;
    options.shouldHandlePointerDown = function(e) {
        assert(this === options && e.target === canvas, "Predicate receiver/original event lost");
        received = e;
        return true;
    };
    const detach = attachControl(camera, canvas, scene, options);
    options.pointerMappings.primaryButton = "pan";
    const down = event("pointerdown", 0);
    listeners.get("pointerdown")(down);
    assert(received === down && captureId === 9, "Pointer ownership/identity lost");
    listeners.get("pointermove")(event("pointermove", 100));
    assert(camera.inertialAlphaOffset === 0 && camera.inertialPanningX === -2,
        "Live mapping rotate->pan does not match original");
    options.pointerMappings.primaryButton = "rotate";
    listeners.get("pointermove")(event("pointermove", 150));
    assert(camera.inertialPanningX === -3 && camera.inertialAlphaOffset === 0,
        "Options update reset ongoing gesture/last coordinate");
    options.keyboard = true;
    let keyboardRejected = false;
    try { listeners.get("pointermove")(event("pointermove", 200)); }
    catch (error) { keyboardRejected = error.status === 8; }
    assert(keyboardRejected, "Live unsupported keyboard option was silently lost");
    options.keyboard = false;
    listeners.get("pointermove")(event("pointermove", 200));
    assert(camera.inertialPanningX === -4 && camera.inertialAlphaOffset === 0,
        "Failed options replacement changed gesture/last-coordinate state");
    detach();
    detach();
    assert(listeners.size === 0, "Listeners survived explicit detachment");
    const callbackOptions = {};
    const detachPredicate = attachControl(camera, canvas, scene, callbackOptions);
    const predicateError = new Error("original predicate witness");
    callbackOptions.shouldHandlePointerDown = function(e) {
        assert(this === callbackOptions && e.target === canvas, "Live predicate receiver lost");
        throw predicateError;
    };
    let predicateCaught;
    try { listeners.get("pointerdown")(event("pointerdown", 0)); }
    catch (error) { predicateCaught = error; }
    assert(predicateCaught === predicateError, "Predicate exception identity/containment lost");
    let reentry;
    let predicateCalls = 0;
    callbackOptions.shouldHandlePointerDown = function(e) {
        ++predicateCalls;
        callbackOptions.pointerMappings = { primaryButton: "pan" };
        try { listeners.get("pointerdown")(event("pointerdown", 10)); }
        catch (error) { reentry = error.status; }
        assert(e.target === canvas, "Outer event lost after rejected predicate reentry");
        return true;
    };
    listeners.get("pointerdown")(event("pointerdown", 0));
    assert(reentry === 10 && predicateCalls === 1, "Active-predicate options replacement was not BUSY");
    callbackOptions.shouldHandlePointerDown = () => false;
    listeners.get("pointerdown")(event("pointerdown", 0));
    assert(predicateCalls === 1, "Replaced predicate callback was retained");
    detachPredicate();
    const remove = setCameraLimits(camera, { upperRadiusLimit: 3 });
    assert(camera.radius === 3, "Install limits did not clamp");
    camera.upperRadiusLimit = 2;
    camera.alpha = camera.alpha;
    camera.fov = 1;
    camera.target.x = 1;
    camera.inertialRadiusOffset = 1;
    assert(camera.radius === 3, "Raw/equal/non-orbit writes over-enforced bounds");
    camera.alpha = .1;
    assert(camera.radius === 2 && camera.inertialRadiusOffset === 0, "Changed scalar failed hook");
    remove();
    camera.radius = 5;
    camera.target.x = 0;
    camera.alpha = -Math.PI / 2;
    const light = createHemisphericLight([0, 1, 0], 1);
    addToScene(scene, light);
    const material = createStandardMaterial();
    const color = [.85, .34, .2];
    material.diffuseColor = color;
    assert(material.diffuseColor === color, "Original tuple identity lost");
    const cube = createBox(engine, 1);
    cube.material = material;
    addToScene(scene, cube);
    await registerScene(scene);
    color[0] = .5;
    markMaterialUboDirty(material);
    renderFrame(engine, 16);
    assert(material.diffuseColor[0] === .5 && cube.material === material, "Live tuple/shared identity lost");
    let rejected = false;
    try { rebuildMaterial(scene, material, { rebuildFrameGraph: true }); }
    catch (error) { rejected = error.status === 8; }
    assert(rejected, "Unsupported frameGraph was ignored");
    const options2 = {};
    const detach2 = attachControl(camera, canvas, scene, options2);
    let observed;
    onBeforeRender(scene, () => { observed = camera.alpha; });
    listeners.get("pointerdown")(event("pointerdown", 0));
    listeners.get("pointermove")(event("pointermove", 100));
    const old = camera.alpha;
    renderFrame(engine, 333);
    assert(observed === old && Math.abs(camera.alpha - (old - .1)) < 1e-14,
        "APPEND vs PREPEND or dt-independent integration mismatch");
    detach2();
    console.log("Native family JS semantics passed");
    await startEngine(engine);
}
main().catch(error => console.error(error));
