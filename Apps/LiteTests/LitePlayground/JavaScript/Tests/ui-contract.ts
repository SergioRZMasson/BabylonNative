import { createEngine, createSceneContext, disposeScene, nativeExtras, nativeUi, onSceneDispose,
    startEngine, stopEngine } from "babylon-lite";

const raw = (globalThis as any)._liteNative;
const check = (condition: unknown, message: string) => { if (!condition) throw new Error(message); };
function status(operation: () => void, expected: number) {
    try { operation(); }
    catch (error) {
        check((error as any).status === expected, `Expected status ${expected}, got ${String(error)}`);
        check(typeof (error as any).operation === "string", "Native operation diagnostic missing");
        return;
    }
    throw new Error(`Expected native status ${expected}`);
}
function wrongType(operation: () => void) {
    try { operation(); } catch (error) { check(error instanceof TypeError, "Expected TypeError"); return; }
    throw new Error("Wrong UI argument silently accepted");
}

async function main() {
    const canvas = document.getElementById("renderCanvas") as HTMLCanvasElement;
    const engine = await createEngine(canvas);
    const firstFrame = startEngine(engine);
    stopEngine(engine);
    let cancelled = false;
    await firstFrame.catch(error => { cancelled = error.status === 11 && error.operation === "startEngine"; });
    check(cancelled, "Stopping before the first C frame left its JS Promise unsettled");
    status(() => nativeExtras.disposeEngine(engine), 10);
    const scene = createSceneContext(engine);
    const sceneSentinel = new Error("SceneDisposeSentinel");
    onSceneDispose(scene, (...args: unknown[]) => {
        check(args.length === 0, "Scene-dispose callback argument shape changed");
        throw sceneSentinel;
    });
    let sceneOriginal = false;
    try { disposeScene(scene); } catch (error) { sceneOriginal = error === sceneSentinel; }
    check(sceneOriginal, "Scene-dispose exception was not restored after returning from C");
    check((sceneSentinel as any).operation === undefined, "Original scene exception was mutated");
    const context = nativeUi.createContext(engine, { firstViewId: 32, viewCount: 32, densityRatio: 1 });
    const root = raw.getUiRoot(context);
    raw.setUiProperty(root, "font-family", "Segoe UI");
    raw.setUiProperty(root, "font-size", "16px");
    check(root === raw.getUiRoot(context), "UI root identity changed");
    wrongType(() => raw.getUiRoot(root));
    wrongType(() => raw.registerUiImage(context, "bad", new Float32Array(4), 1, 1));
    status(() => raw.registerUiImage(context, "short", new Uint8Array(3), 1, 1), 1);
    status(() => raw.disposeUiElement(root), 10);
    status(() => raw.setUiProperty(root, "not-a-property", "yes"), 8);
    let parent: any = raw.createUiElement(context, "div");
    const child = raw.createUiElement(context, "button");
    raw.appendUiChild(root, parent);
    raw.appendUiChild(parent, child);
    raw.setUiProperty(child, "position", "absolute");
    raw.setUiProperty(child, "left", "20px");
    raw.setUiProperty(child, "top", "20px");
    raw.setUiProperty(child, "width", "180px");
    raw.setUiProperty(child, "height", "50px");
    raw.setUiProperty(child, "pointer-events", "auto");
    raw.setUiText(child, "Native C99 button");
    parent = null;
    (globalThis as any)._liteForceGC?.();
    raw.updateUi(context, 0);
    check(raw.getUiStats(context).liveElementCount >= 3, "Wrapper GC destroyed a native-owned UI parent");
    const sentinel = Object.assign(new Error("UiCallbackSentinel"), { marker: 913 });
    const token = raw.addUiEventListener(child, 0, () => {
        raw.setUiText(child, "Callback mutation reached native UI");
        status(() => raw.updateUi(context, 0), 10);
        throw sentinel;
    });
    const input = { x: 30, y: 30, button: 0, key: 0, modifiers: 0, text: "" };
    raw.processUiInput(context, { ...input, kind: 0 });
    raw.processUiInput(context, { ...input, kind: 1 });
    let original = false;
    await Promise.resolve().then(() => raw.processUiInput(context, { ...input, kind: 2 }))
        .catch(error => { original = error === sentinel; });
    check(original, "UI C callback lost the original JS exception");
    check((sentinel as any).operation === undefined, "Original UI exception was mutated");
    raw.removeUiEventListener(child, token);
    wrongType(() => raw.removeUiEventListener(child, token));
    let pixels: Uint8Array | null = new Uint8Array([255, 0, 0, 255]);
    raw.registerUiImage(context, "copied", pixels, 1, 1);
    raw.setUiImageSampling(context, "copied", true);
    pixels.fill(0);
    pixels = null;
    (globalThis as any)._liteForceGC?.();
    raw.setUiProperty(child, "decorator", 'image("copied" cover)');
    raw.updateUi(context, 0);
    raw.renderUi(context);
    check(raw.getUiStats(context).textureCreateCount > 0, "Copied/GC image did not reach native texture creation");
    status(() => raw.setUiImageSampling(context, "copied", false), 10);
    status(() => raw.unregisterUiImage(context, "copied"), 10);
    let rejected = false;
    await Promise.resolve().then(() => raw.registerUiImage(context, "short-async", new Uint8Array(3), 1, 1))
        .catch(error => { rejected = error.status === 1 && typeof error.operation === "string"; });
    check(rejected, "Native UI status did not reject an async source operation");
    raw.registerUiImage(context, "clamped", new Uint8ClampedArray([0, 255, 0, 255]), 1, 1);
    raw.unregisterUiImage(context, "clamped");
    raw.disposeUiContext(context);
    status(() => raw.getUiStats(context), 3);
    status(() => raw.setUiText(child, "stale"), 3);

    const panel = document.createElement("div");
    panel.style.cssText = "position:absolute;inset:0;background:#122244;color:#fff;pointer-events:auto";
    const button = document.createElement("button");
    button.style.cssText = "position:absolute;left:20px;top:20px;width:200px;height:60px;pointer-events:auto";
    button.textContent = "Generic native DOM button";
    panel.appendChild(button);
    const textbox = document.createElement("input");
    textbox.setAttribute("type", "text");
    textbox.setAttribute("value", "Retained textbox");
    textbox.style.cssText = "position:absolute;left:20px;top:100px;width:240px;height:40px;pointer-events:auto;color:#fff";
    panel.appendChild(textbox);
    document.body.appendChild(panel);
    let clicked = false;
    let changed = false;
    button.addEventListener("click", () => { clicked = true; });
    textbox.addEventListener("change", () => { changed = true; });
    const tick = () => {
        if (!(globalThis as any)._uiFixtureFrames) {
            (globalThis as any)._uiFixtureFrames = 1;
        } else if ((globalThis as any)._uiFixtureFrames++ === 2) {
            const dispatch = (globalThis as any)._platformDispatch;
            dispatch({ type: "mousemove", clientX: 40, clientY: 40, button: 0 });
            dispatch({ type: "mousedown", clientX: 40, clientY: 40, button: 0 });
            dispatch({ type: "mouseup", clientX: 40, clientY: 40, button: 0 });
            check(clicked, "Generic DOM click did not traverse native RmlUI/NAPI callback");
            dispatch({ type: "mousemove", clientX: 40, clientY: 110, button: 0 });
            dispatch({ type: "mousedown", clientX: 40, clientY: 110, button: 0 });
            dispatch({ type: "mouseup", clientX: 40, clientY: 110, button: 0 });
            dispatch({ type: "keydown", key: "End", button: 0 });
            dispatch({ type: "text", text: "!", button: 0 });
            check(changed && textbox.value.endsWith("!"), "Native textbox/change/value transport failed");
            (globalThis as any)._liteUiFixtureResult = true;
        }
        requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
    canvas.dataset.ready = "true";
}
main().catch(error => {
    const canvas = document.getElementById("renderCanvas") as HTMLCanvasElement;
    canvas.dataset.error = String(error) + "\n" + (error.stack ?? "");
});
