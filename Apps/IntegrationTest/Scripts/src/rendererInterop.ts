import type { NativeTexture } from "@babylonjs/core/Engines/Native/nativeInterfaces";
import type { NativeEngine } from "@babylonjs/core/Engines/nativeEngine";
import "@babylonjs/core/Helpers/sceneHelpers";
import { RenderTargetTexture } from "@babylonjs/core/Materials/Textures/renderTargetTexture";
import type { SceneContext } from "./sceneContext";
import { Engine } from "@babylonjs/core/Engines/engine";

export async function createRenderTargetTextureAsync(
  context: SceneContext,
  texturePromise: Promise<NativeTexture>,
  textureWidth: number,
  textureHeight: number
): Promise<RenderTargetTexture> {
  const scene = context.scene;
  const externalTexture = await texturePromise;
  const nativeEngine = scene.getEngine() as NativeEngine;
  const outputTexture = nativeEngine.wrapNativeTexture(externalTexture);

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
