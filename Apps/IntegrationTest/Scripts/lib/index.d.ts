import { transformPoint } from "./factoryInterop";
import { createSceneAsync, destroyScene } from "./sceneInterop";
import { createRenderTargetTextureAsync, destroyRenderTargetTexture, getEngineInfo } from "./rendererInterop";
declare global {
    var BI_createSceneAsync: typeof createSceneAsync;
    var BI_destroyScene: typeof destroyScene;
    var BI_transformPoint: typeof transformPoint;
    var BI_createRenderTargetTextureAsync: typeof createRenderTargetTextureAsync;
    var BI_destroyRenderTargetTexture: typeof destroyRenderTargetTexture;
    var BI_getEngineInfo: typeof getEngineInfo;
}
