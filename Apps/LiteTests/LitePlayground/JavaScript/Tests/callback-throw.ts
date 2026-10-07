import { createEngine, createSceneContext, onBeforeRender, registerScene, startEngine } from "babylon-lite";

declare const _liteTestsDone: (success: boolean, detail: string) => void;
declare const globalThis: {
    _liteExpectedCallbackError?: Error & { marker: number };
    _liteCallbackPromiseRejected?: boolean;
};

async function main(): Promise<void> {
    const engine = await createEngine({ width: 256, height: 256 });
    const scene = createSceneContext(engine);
    const original = Object.assign(new Error("LiteCallbackSentinel"), { marker: 173 });
    globalThis._liteExpectedCallbackError = original;
    onBeforeRender(scene, () => { throw original; });
    await registerScene(scene);
    try {
        await startEngine(engine);
        _liteTestsDone(false, "Throwing before-render callback silently resolved startEngine.");
    } catch (error) {
        globalThis._liteCallbackPromiseRejected = error === original;
        if (error !== original) _liteTestsDone(false, "First-frame Promise lost the original callback error identity.");
    }
}

main().catch(error => _liteTestsDone(false, String(error) + "\n" + (error.stack ?? "")));
