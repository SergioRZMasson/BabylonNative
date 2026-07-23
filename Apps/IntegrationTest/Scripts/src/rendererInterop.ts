import type { NativeTexture } from "@babylonjs/core/Engines/Native/nativeInterfaces";
import type { NativeEngine } from "@babylonjs/core/Engines/nativeEngine";
import "@babylonjs/core/Helpers/sceneHelpers";
import { RenderTargetTexture } from "@babylonjs/core/Materials/Textures/renderTargetTexture";
import type { SceneContext } from "./sceneContext";
import { Engine } from "@babylonjs/core/Engines/engine";

export function createRenderTargetTexture(
  context: SceneContext,
  texture: NativeTexture,
  textureWidth: number,
  textureHeight: number,
  sampleCount: number,
  samplingMode: number
): RenderTargetTexture {
  const scene = context.scene;

  const nativeEngine = scene.getEngine() as NativeEngine;
  const outputTexture = nativeEngine.wrapNativeTexture(texture);

  return new RenderTargetTexture(
    "outputTexture",
    {
      width: textureWidth,
      height: textureHeight,
    },
    scene,
    {
      colorAttachment: outputTexture,
      generateDepthBuffer: true,
      generateStencilBuffer: true,
      // The sample count and sampling mode are provided by the native side so the render target matches the
      // texture Babylon renders into: sampleCount is the MSAA sample count of that (external) texture - the
      // depth buffer is created to match - and samplingMode reflects the texture (non-mipmapped for MSAA).
      samples: sampleCount,
      samplingMode: samplingMode,
    }
  );
}

export function destroyRenderTargetTexture(texture: RenderTargetTexture): void {
  texture.dispose();
}

export interface EngineInfo {
  name: string;
  version: string;
}

export function getEngineInfo(): EngineInfo {
  return {
    name: "Babylon",
    version: Engine.Version,
  };
}
