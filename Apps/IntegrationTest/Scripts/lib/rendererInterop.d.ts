import type { NativeTexture } from "@babylonjs/core/Engines/Native/nativeInterfaces";
import "@babylonjs/core/Helpers/sceneHelpers";
import { RenderTargetTexture } from "@babylonjs/core/Materials/Textures/renderTargetTexture";
import type { SceneContext } from "./sceneContext";
export declare function createRenderTargetTexture(context: SceneContext, texture: NativeTexture, textureWidth: number, textureHeight: number, sampleCount: number, samplingMode: number): RenderTargetTexture;
export declare function destroyRenderTargetTexture(texture: RenderTargetTexture): void;
export interface EngineInfo {
    name: string;
    version: string;
}
export declare function getEngineInfo(): EngineInfo;
