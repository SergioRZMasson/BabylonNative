import { FreeCamera } from "@babylonjs/core/Cameras/freeCamera";
import type { RenderTargetTexture } from "@babylonjs/core/Materials/Textures/renderTargetTexture";
import { Scene } from "@babylonjs/core/scene";
import type { Nullable } from "@babylonjs/core/types";
import "@babylonjs/loaders/glTF";
interface SceneStats {
    indexCount: number;
    materialCount: number;
    meshCount: number;
    textureCount: number;
    vertexCount: number;
}
export declare class SceneContext {
    private readonly _scene;
    private readonly _camera;
    private readonly _materialPlugins;
    private readonly _rootNode;
    private readonly _animationGroups;
    private readonly _gltfExtensions;
    private _activeAnimationGroup;
    private _requestedViewport;
    private _scissor;
    private _maxBoundingBoxesCache;
    private _boundingInfoCache;
    private _statsCache;
    private constructor();
    static createAsync(environmentData: ArrayBuffer, modelData: ArrayBuffer): Promise<SceneContext>;
    dispose(): void;
    get scene(): Scene;
    get camera(): FreeCamera;
    get animationInfos(): Array<{
        name: string;
        duration: number;
    }>;
    get gltfExtensions(): Array<string>;
    waitForSceneReadyAsync(): Promise<void>;
    applyRootNodeTransform(transformArray: Float32Array): void;
    applyCameraTransform(aspectRatio: number, orthographic: boolean, fovOrOrthographicSize: number, minZ: number, maxZ: number, positionX: number, positionY: number, positionZ: number, targetX: number, targetY: number, targetZ: number, upVectorX: number, upVectorY: number, upVectorZ: number, viewportMinX: number, viewportMinY: number, viewportMaxX: number, viewportMaxY: number, scissorLeft: number, scissorTop: number, scissorRight: number, scissorBottom: number): void;
    applyAnimationProgress(animationIndex: number, progress: number): void;
    render(renderTargetTexture: RenderTargetTexture): void;
    getBoundingBoxCenter(animationIndex: Nullable<number>): Float32Array;
    getBoundingBoxExtents(animationIndex: Nullable<number>): Float32Array;
    getProjectionBounds(animationIndex: Nullable<number>, aspectRatio: number, orthographic: boolean, fovOrOrthographicSize: number, minZ: number, maxZ: number, positionX: number, positionY: number, positionZ: number, targetX: number, targetY: number, targetZ: number, upVectorX: number, upVectorY: number, upVectorZ: number, sceneTransform: Float32Array): Float32Array;
    getStats(): SceneStats;
    private _computeBoundingInfo;
    private _getMaxBoundingBoxes;
    private _setActiveAnimation;
}
export {};
