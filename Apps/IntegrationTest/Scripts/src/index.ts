import { transformPoint } from "./factoryInterop";
import {
  createSceneAsync,
  destroyScene,
  setEnvironment,
} from "./sceneInterop";
import {
  createRenderTargetTexture,
  destroyRenderTargetTexture,
  getEngineInfo,
} from "./rendererInterop";

declare global {
  /* eslint-disable no-var */
  var BI_setEnvironment: typeof setEnvironment;
  var BI_createSceneAsync: typeof createSceneAsync;
  var BI_destroyScene: typeof destroyScene;
  var BI_transformPoint: typeof transformPoint;
  var BI_createRenderTargetTexture: typeof createRenderTargetTexture;
  var BI_destroyRenderTargetTexture: typeof destroyRenderTargetTexture;
  var BI_getEngineInfo: typeof getEngineInfo;
  /* eslint-enable no-var */
}

globalThis.BI_getEngineInfo = getEngineInfo;
globalThis.BI_setEnvironment = setEnvironment;
globalThis.BI_createSceneAsync = createSceneAsync;
globalThis.BI_destroyScene = destroyScene;
globalThis.BI_transformPoint = transformPoint;
globalThis.BI_createRenderTargetTexture = createRenderTargetTexture;
globalThis.BI_destroyRenderTargetTexture = destroyRenderTargetTexture;
