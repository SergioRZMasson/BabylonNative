/// <reference path="../../node_modules/babylonjs/babylon.module.d.ts" />
/// <reference path="../../node_modules/babylonjs-loaders/babylonjs.loaders.module.d.ts" />

let engine = null;
let scene = null;
let outputTexture = null;
let rootMesh = null;

/**
 * Called from C++ after AddToContextAsync resolves.
 * Sets up the engine, scene, and output render target texture.
 */
function startup(nativeTexture, width, height) {
    engine = new BABYLON.NativeEngine();

    scene = new BABYLON.Scene(engine);
    scene.clearColor.set(1, 1, 1, 1);

    // Environment so reflections look good.
    scene.createDefaultEnvironment({ createSkybox: false, createGround: false });

    // Wrap the native texture provided by C++ into a render target texture.
    outputTexture = new BABYLON.RenderTargetTexture(
        "outputTexture",
        { width: width, height: height },
        scene,
        {
            colorAttachment: engine.wrapNativeTexture(nativeTexture),
            generateDepthBuffer: true,
            generateStencilBuffer: true,
            samples: 4,
        }
    );
}

/**
 * Called from C++ to load a model from a binary ArrayBuffer and render one frame.
 * Returns a Promise that resolves when the frame has been rendered.
 */
async function loadAndRenderModelAsync(modelData) {
    // Dispose previous model if any.
    if (rootMesh) {
        rootMesh.dispose();
        rootMesh = null;
    }

    // Import the GLB from the provided ArrayBuffer.
    const result = await BABYLON.SceneLoader.ImportMeshAsync(
        null,
        "",
        new Uint8Array(modelData),
        scene,
        undefined,
        ".glb"
    );

    rootMesh = result.meshes[0];

    // Auto-position a camera on the model.
    scene.createDefaultCamera(true, true);
    scene.activeCamera.alpha = 2;
    scene.activeCamera.beta = 1.25;
    scene.activeCamera.outputRenderTarget = outputTexture;

    // ACES tone mapping.
    scene.imageProcessingConfiguration.toneMappingEnabled = true;
    scene.imageProcessingConfiguration.toneMappingType =
        BABYLON.ImageProcessingConfiguration.TONEMAPPING_ACES;

    // Wait for everything to be compiled/uploaded.
    await scene.whenReadyAsync();

    // Render the frame.
    scene.render();
}
