/// <reference path="../node_modules/babylonjs/babylon.module.d.ts" />
/// <reference path="../node_modules/babylonjs-loaders/babylonjs.loaders.module.d.ts" />
/// <reference path="../node_modules/babylonjs-materials/babylonjs.materials.module.d.ts" />
/// <reference path="../node_modules/babylonjs-gui/babylon.gui.module.d.ts" />

var engine = new BABYLON.NativeEngine();
var scene = new BABYLON.Scene(engine);
var _shaderLanguage = 0;
var _processingContext = engine._getShaderProcessingContext(_shaderLanguage);
var _indexParameters =
{
    maxSimultaneousLights: 10,
    maxSimultaneousMorphTargets: 5
};

var _vertexProcessorOptions  = {
    defines: "",
    indexParameters: _indexParameters,
    isFragment: false,
    shouldUseHighPrecisionShader: engine._shouldUseHighPrecisionShader,
    processor: engine._getShaderProcessor(0),
    supportsUniformBuffers: engine.supportsUniformBuffers,
    shadersRepository: "src/Shaders/",
    includesShadersStore: BABYLON.ShaderStore.GetIncludesShadersStore(_shaderLanguage),
    version: (engine.version * 100).toString(),
    platformName: engine.shaderPlatformName,
    processingContext: _processingContext,
    isNDCHalfZRange: engine.isNDCHalfZRange,
    useReverseDepthBuffer: engine.useReverseDepthBuffer,
    undefined,
};


engine.runRenderLoop(function () {
    scene.render();
});

function ParseVertexShader(vertexShader, isFragment ,callback) {
    _vertexProcessorOptions.isFragment = isFragment;
    BABYLON.ShaderProcessor.Initialize(_vertexProcessorOptions);
    BABYLON.ShaderProcessor.Process(
        vertexShader,
        _vertexProcessorOptions,
        (migratedVertexCode, codeBeforeMigration) => {
            console.log(`MIGRATED CODE: ${migratedVertexCode}`);
            console.log(`CODE BEFORE MIGRATION: ${codeBeforeMigration}`);
            callback(migratedVertexCode);
        },
        engine);
}