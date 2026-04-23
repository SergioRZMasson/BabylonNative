import { transformPoint } from "./factoryInterop";
import { createSceneAsync, destroyScene } from "./sceneInterop";
import {
  createRenderTargetTextureAsync,
  destroyRenderTargetTexture,
  getEngineInfo,
} from "./rendererInterop";

declare global {
  /* eslint-disable no-var */
  var BI_createSceneAsync: typeof createSceneAsync;
  var BI_destroyScene: typeof destroyScene;
  var BI_transformPoint: typeof transformPoint;
  var BI_createRenderTargetTextureAsync: typeof createRenderTargetTextureAsync;
  var BI_destroyRenderTargetTexture: typeof destroyRenderTargetTexture;
  var BI_getEngineInfo: typeof getEngineInfo;
  /* eslint-enable no-var */
}

globalThis.BI_getEngineInfo = getEngineInfo;
globalThis.BI_createSceneAsync = createSceneAsync;
globalThis.BI_destroyScene = destroyScene;
globalThis.BI_transformPoint = transformPoint;
globalThis.BI_createRenderTargetTextureAsync = createRenderTargetTextureAsync;
globalThis.BI_destroyRenderTargetTexture = destroyRenderTargetTexture;
