import type { AnimationGroup } from "@babylonjs/core/Animations/animationGroup";
import { Camera } from "@babylonjs/core/Cameras/camera";
import { FreeCamera } from "@babylonjs/core/Cameras/freeCamera";
import { BoundingBox } from "@babylonjs/core/Culling/boundingBox";
import { NativeEngine } from "@babylonjs/core/Engines/nativeEngine";
import { SceneLoader } from "@babylonjs/core/Loading/sceneLoader";
import type { RenderTargetTexture } from "@babylonjs/core/Materials/Textures/renderTargetTexture";
import type { PBRMaterial } from "@babylonjs/core/Materials/PBR/pbrMaterial";
import {
  RegisterMaterialPlugin,
  UnregisterMaterialPlugin,
} from "@babylonjs/core/Materials/materialPluginManager";
import {
  Matrix,
  Quaternion,
  TmpVectors,
  Vector3,
} from "@babylonjs/core/Maths/math.vector";
import type { TransformNode } from "@babylonjs/core/Meshes/transformNode";
import { Scene } from "@babylonjs/core/scene";
import type { Nullable } from "@babylonjs/core/types";
import "@babylonjs/loaders/glTF";
import type { GLTFFileLoader } from "@babylonjs/loaders/glTF";
import { GLTFLoaderAnimationStartMode } from "@babylonjs/loaders/glTF";
import type { IGLTF } from "babylonjs-gltf2interface";
import { configureEnvironment, createLights, enableShadows } from "./lighting";
import { computeMaxExtents } from "@babylonjs/core/Meshes/meshUtils";
import {
  ViewportMaterialPlugin,
  defaultViewport,
} from "./viewportMaterialPlugin";

const knownSupportedExtensions = [
  "KHR_texture_transform",
  "KHR_materials_unlit",
  "KHR_materials_pbrSpecularGlossiness",
  "KHR_animation_pointer",
];

// Cache variables for transform operations
const transform = new Matrix();
const scaling = new Vector3();
const rotation = new Quaternion();
const position = new Vector3();
const target = new Vector3();

let sceneId = 0;

// WORKAROUND: The native engine's getRenderWidth/getRenderHeight report the *device* (default framebuffer)
//             size, not the bound render target's size. This host always renders into a RenderTargetTexture,
//             so render() stashes the target's size in fallbackRenderWidth/Height and we return that here.
//             We prefer the fallback and only fall back to the native value when it isn't set yet (before the
//             first render). NOTE: this must be `fallback || native`, not `native || fallback`: the device is
//             initialized to a small non-zero size because Babylon Native rejects a zero-sized initialization,
//             so `native` is always truthy and would otherwise shadow the real target size, causing geometry
//             to be projected for the device size while the target is a different size (only the
//             viewport-independent clear would then appear). The native value still guards against a
//             zero-height division in the projection matrix before the first render sets the fallback.
let fallbackRenderWidth = 0;
let fallbackRenderHeight = 0;
const prototype = (NativeEngine as any).prototype;
const getRenderWidth = prototype.getRenderWidth;
const getRenderHeight = prototype.getRenderHeight;
prototype.getRenderWidth = function (this: any, useScreen: any): number {
  return fallbackRenderWidth || getRenderWidth.call(this, useScreen);
};
prototype.getRenderHeight = function (this: any, useScreen: any): number {
  return fallbackRenderHeight || getRenderHeight.call(this, useScreen);
};

// Global engine with disabled async shader compilation
export const engine = new NativeEngine();
engine.getCaps().parallelShaderCompile = undefined;

interface BoundingInfo {
  center: Float32Array;
  extents: Float32Array;
}

interface SceneStats {
  indexCount: number;
  materialCount: number;
  meshCount: number;
  textureCount: number;
  vertexCount: number;
}

// Guards KHR_materials_transmission models against rendering opaque on their first render.
//
// Two Babylon behaviours combine to break transmission on a host that renders on demand (one render per
// size change):
//   1. The glTF loader assigns the transmissive material's opaque refraction render target on a deferred
//      Tools.SetImmediate tick, so a render that happens before it fires compiles a no-refraction (opaque)
//      shader variant.
//   2. Babylon Native compiles shaders on a background thread, so the correct refraction shader may still be
//      compiling on the first render; with shader hot-swapping (on by default) the material keeps drawing its
//      previous opaque effect until the new one is ready.
// Because the host renders only once per size, that single frame is the opaque one and the self-correcting
// second frame never comes. We disable hot-swapping on transmissive materials (so a not-ready material is
// skipped rather than drawn opaque) and wait for the deferred refraction assignment plus all shader
// compilation to finish before the scene is first rendered. Ordinary (non-transmissive) models are left
// untouched so their load latency is unchanged.
async function ensureTransmissionReadyForFirstRender(
  scene: Scene
): Promise<void> {
  const transmissiveMaterials = scene.meshes
    .map((mesh) => mesh.material as Nullable<PBRMaterial>)
    .filter(
      (material): material is PBRMaterial =>
        !!material?.subSurface?.isRefractionEnabled
    );

  if (transmissiveMaterials.length === 0) {
    return;
  }

  for (const material of transmissiveMaterials) {
    material.allowShaderHotSwapping = false;
  }

  // Bounded so a resource that never becomes ready can't hang scene creation.
  const deadline = Date.now() + 5000;
  for (;;) {
    await new Promise<void>((resolve) => setTimeout(resolve, 16));

    // Don't accept readiness until the loader's deferred refraction-texture assignment has run - otherwise
    // scene.isReady() would report the opaque (no-refraction) shader variant as ready. Calling scene.isReady
    // also drives the shader compilation it is waiting on.
    const refractionAssigned = transmissiveMaterials.every(
      (material) => !!material.subSurface.refractionTexture
    );
    if ((refractionAssigned && scene.isReady(true)) || Date.now() >= deadline) {
      break;
    }
  }
}

export class SceneContext {
  private readonly _scene: Scene;
  private readonly _camera: FreeCamera;
  private readonly _materialPlugins: Array<ViewportMaterialPlugin>;
  private readonly _rootNode: TransformNode;
  private readonly _animationGroups: Array<AnimationGroup>;
  private readonly _gltfExtensions: Array<string>;

  private _activeAnimationGroup: Nullable<AnimationGroup> = null;

  private _requestedViewport = { x: 0, y: 0, scale: 1 };
  private _scissor = { x: 0, y: 0, width: 0, height: 0 };

  private _maxBoundingBoxesCache = new Map<
    Nullable<number>,
    Array<BoundingBox>
  >();

  private _boundingInfoCache = new Map<Nullable<number>, BoundingInfo>();

  private _statsCache: Nullable<SceneStats> = null;

  private constructor(
    scene: Scene,
    materialPlugins: Array<ViewportMaterialPlugin>,
    rootNode: TransformNode,
    animationGroups: Array<AnimationGroup>,
    gltfExtensions: Array<string>
  ) {
    this._scene = scene;
    this._rootNode = rootNode;
    this._animationGroups = animationGroups;
    this._gltfExtensions = gltfExtensions;

    this._camera = new FreeCamera("camera", Vector3.Zero(), this._scene);

    const lights = createLights(scene);
    enableShadows(lights, this._rootNode);

    this._materialPlugins = materialPlugins;

    this._scene.onBeforeRenderTargetsRenderObservable.add(() => {
      for (const plugin of this._materialPlugins) {
        plugin.viewport = defaultViewport;
      }
    });

    this._scene.onAfterRenderTargetsRenderObservable.add(() => {
      for (const plugin of this._materialPlugins) {
        plugin.viewport = this._requestedViewport;
      }
    });

    this._scene.onBeforeCameraRenderObservable.add(() => {
      engine.enableScissor(
        this._scissor.x,
        this._scissor.y,
        this._scissor.width,
        this._scissor.height
      );
    });
  }

  public static async createAsync(
    modelData: ArrayBuffer
  ): Promise<SceneContext> {
    const materialPlugins = new Array<ViewportMaterialPlugin>();

    RegisterMaterialPlugin("viewport", (material) => {
      const plugin = new ViewportMaterialPlugin(material);
      materialPlugins.push(plugin);
      return plugin;
    });

    try {
      const scene = new Scene(engine);
      scene.useRightHandedSystem = true;
      scene.clearColor.a = 0;

      configureEnvironment(scene);

      let extensionsUsed: Array<string> | undefined = [];

      SceneLoader.OnPluginActivatedObservable.addOnce((plugin) => {
        const loader = plugin as GLTFFileLoader;
        loader.animationStartMode = GLTFLoaderAnimationStartMode.NONE;
        loader.onParsedObservable.add((data) => {
          if (data && data.json && (data.json as IGLTF).asset) {
            const loadInfo = (data.json as IGLTF).asset;
            extensionsUsed = (data.json as IGLTF).extensionsUsed;
            if (loadInfo.generator) {
              // Handle models from known exporters that need special extension filtering
              if (loadInfo.generator.startsWith("Microsoft GLTF Exporter")) {
                loader.loadNodeAnimations = false;
                loader.loadMorphTargets = false;
                loader.onExtensionLoadedObservable.add((extension) => {
                  if (!knownSupportedExtensions.includes(extension.name)) {
                    extension.enabled = false;
                  }
                });
              }
            }
          }
        });
      });

      const model = await SceneLoader.ImportMeshAsync(
        "",
        `scene${sceneId++}`,
        new Uint8Array(modelData),
        scene,
        undefined,
        ".glb"
      );

      model.meshes[0].rotationQuaternion = Quaternion.Identity();

      const context = new SceneContext(
        scene,
        materialPlugins,
        model.meshes[0],
        model.animationGroups,
        extensionsUsed
      );

      // Ensure KHR_materials_transmission materials have their refraction render target assigned and their
      // shaders fully compiled before the host renders the scene for the first time, so transmissive models
      // don't render opaque on that first (and, for an on-demand host, only) frame.
      await ensureTransmissionReadyForFirstRender(scene);

      return context;
    } finally {
      UnregisterMaterialPlugin("viewport");
    }
  }

  public dispose(): void {
    this._materialPlugins.length = 0;
    this._camera.dispose();
    this._scene.dispose();
  }

  public get scene(): Scene {
    return this._scene;
  }

  public get camera(): FreeCamera {
    return this._camera;
  }

  public get animationInfos(): Array<{ name: string; duration: number }> {
    if (!this._animationGroups) {
      return [];
    }

    return this._animationGroups.map((animationGroup) => {
      if (animationGroup.children.length === 0) {
        return {
          name: animationGroup.name,
          duration: 0,
        };
      }

      return {
        name: animationGroup.name,
        duration: animationGroup.getLength(),
      };
    });
  }

  public get gltfExtensions(): Array<string> {
    return this._gltfExtensions;
  }

  public applyRootNodeTransform(transformArray: Float32Array): void {
    Matrix.FromArrayToRef(transformArray, 0, transform);
    transform.decompose(scaling, rotation, position);

    this._rootNode.scaling.copyFrom(scaling);
    this._rootNode.rotationQuaternion!.copyFrom(rotation);
    this._rootNode.position.copyFrom(position);
    this._rootNode.markAsDirty();
  }

  public applyCameraTransform(
    aspectRatio: number,
    orthographic: boolean,
    fovOrOrthographicSize: number,
    minZ: number,
    maxZ: number,
    positionX: number,
    positionY: number,
    positionZ: number,
    targetX: number,
    targetY: number,
    targetZ: number,
    upVectorX: number,
    upVectorY: number,
    upVectorZ: number,
    viewportMinX: number,
    viewportMinY: number,
    viewportMaxX: number,
    viewportMaxY: number,
    scissorLeft: number,
    scissorTop: number,
    scissorRight: number,
    scissorBottom: number
  ): void {
    this._camera.mode = orthographic
      ? Camera.ORTHOGRAPHIC_CAMERA
      : Camera.PERSPECTIVE_CAMERA;
    this._camera.minZ = minZ;
    this._camera.maxZ = maxZ;
    this._camera.position.set(positionX, positionY, positionZ);
    this._camera.setTarget(target.set(targetX, targetY, targetZ));
    this._camera.upVector.set(upVectorX, upVectorY, upVectorZ);

    if (orthographic) {
      this._camera.orthoLeft = -fovOrOrthographicSize * aspectRatio;
      this._camera.orthoRight = fovOrOrthographicSize * aspectRatio;
      this._camera.orthoTop = fovOrOrthographicSize;
      this._camera.orthoBottom = -fovOrOrthographicSize;
    } else {
      this._camera.fov = fovOrOrthographicSize;
    }

    this._requestedViewport.x = viewportMaxX + viewportMinX - 1;
    this._requestedViewport.y = -(viewportMaxY + viewportMinY - 1);
    this._requestedViewport.scale = viewportMaxY - viewportMinY;

    this._scissor.x = scissorLeft;
    this._scissor.y = scissorTop;
    this._scissor.width = scissorRight - scissorLeft;
    this._scissor.height = scissorBottom - scissorTop;
  }

  public applyAnimationProgress(
    animationIndex: number,
    progress: number
  ): void {
    const animationGroup = this._setActiveAnimation(animationIndex);

    const frame =
      animationGroup.from +
      progress * (animationGroup.to - animationGroup.from);
    animationGroup.goToFrame(frame);
  }

  public render(renderTargetTexture: RenderTargetTexture): void {
    fallbackRenderWidth = renderTargetTexture.getRenderWidth();
    fallbackRenderHeight = renderTargetTexture.getRenderHeight();

    this._camera.outputRenderTarget = renderTargetTexture;
    this._scene.render();
  }

  public getBoundingBoxCenter(animationIndex: Nullable<number>): Float32Array {
    let boundingInfo = this._boundingInfoCache.get(animationIndex);

    if (!boundingInfo) {
      boundingInfo = this._computeBoundingInfo(animationIndex);
      this._boundingInfoCache.set(animationIndex, boundingInfo);
    }

    return boundingInfo.center;
  }

  public getBoundingBoxExtents(animationIndex: Nullable<number>): Float32Array {
    let boundingInfo = this._boundingInfoCache.get(animationIndex);

    if (!boundingInfo) {
      boundingInfo = this._computeBoundingInfo(animationIndex);
      this._boundingInfoCache.set(animationIndex, boundingInfo);
    }

    return boundingInfo.extents;
  }

  public getProjectionBounds(
    animationIndex: Nullable<number>,
    aspectRatio: number,
    orthographic: boolean,
    fovOrOrthographicSize: number,
    minZ: number,
    maxZ: number,
    positionX: number,
    positionY: number,
    positionZ: number,
    targetX: number,
    targetY: number,
    targetZ: number,
    upVectorX: number,
    upVectorY: number,
    upVectorZ: number,
    sceneTransform: Float32Array
  ): Float32Array {
    let minX = Number.POSITIVE_INFINITY;
    let minY = Number.POSITIVE_INFINITY;
    let maxX = Number.NEGATIVE_INFINITY;
    let maxY = Number.NEGATIVE_INFINITY;

    const modelMatrix = Matrix.FromArrayToRef(
      sceneTransform,
      0,
      TmpVectors.Matrix[0]
    );

    const viewMatrix = Matrix.LookAtRHToRef(
      TmpVectors.Vector3[0].set(positionX, positionY, positionZ),
      TmpVectors.Vector3[1].set(targetX, targetY, targetZ),
      TmpVectors.Vector3[2].set(upVectorX, upVectorY, upVectorZ),
      TmpVectors.Matrix[1]
    );

    const projectionMatrix = orthographic
      ? Matrix.OrthoOffCenterLHToRef(
          -fovOrOrthographicSize * aspectRatio,
          fovOrOrthographicSize * aspectRatio,
          -fovOrOrthographicSize,
          fovOrOrthographicSize,
          minZ,
          maxZ,
          TmpVectors.Matrix[2]
        )
      : Matrix.PerspectiveFovRHToRef(
          fovOrOrthographicSize,
          aspectRatio,
          minZ,
          maxZ,
          TmpVectors.Matrix[2]
        );

    const modelViewProjectionMatrix = modelMatrix.multiplyToRef(
      viewMatrix.multiplyToRef(projectionMatrix, TmpVectors.Matrix[3]),
      TmpVectors.Matrix[3]
    );

    for (const boundingBox of this._getMaxBoundingBoxes(animationIndex)) {
      for (const point of boundingBox.vectorsWorld) {
        const transformedPoint = Vector3.TransformCoordinatesToRef(
          point,
          modelViewProjectionMatrix,
          TmpVectors.Vector3[0]
        );

        minX = Math.min(minX, transformedPoint.x);
        maxX = Math.max(maxX, transformedPoint.x);
        minY = Math.min(minY, transformedPoint.y);
        maxY = Math.max(maxY, transformedPoint.y);
      }
    }

    return new Float32Array([minX, minY, maxX, maxY]);
  }

  public getStats(): SceneStats {
    if (!this._statsCache) {
      const meshes = this._scene.meshes.filter(
        (mesh) => mesh !== this._rootNode
      );

      let indexCount = 0;
      let vertexCount = 0;

      for (const mesh of meshes) {
        indexCount += mesh.getTotalIndices();
        vertexCount += mesh.getTotalVertices();
      }

      const materialCount = this._scene.materials.length;
      const meshCount = meshes.length;
      const textureCount = this._scene.textures.length;

      this._statsCache = {
        indexCount,
        materialCount,
        meshCount,
        textureCount,
        vertexCount,
      };
    }

    return this._statsCache;
  }

  private _computeBoundingInfo(animationIndex: Nullable<number>): {
    center: Float32Array;
    extents: Float32Array;
  } {
    let minX = Number.POSITIVE_INFINITY;
    let minY = Number.POSITIVE_INFINITY;
    let minZ = Number.POSITIVE_INFINITY;
    let maxX = Number.NEGATIVE_INFINITY;
    let maxY = Number.NEGATIVE_INFINITY;
    let maxZ = Number.NEGATIVE_INFINITY;

    for (const boundingBox of this._getMaxBoundingBoxes(animationIndex)) {
      minX = Math.min(minX, boundingBox.minimumWorld.x);
      minY = Math.min(minY, boundingBox.minimumWorld.y);
      minZ = Math.min(minZ, boundingBox.minimumWorld.z);
      maxX = Math.max(maxX, boundingBox.maximumWorld.x);
      maxY = Math.max(maxY, boundingBox.maximumWorld.y);
      maxZ = Math.max(maxZ, boundingBox.maximumWorld.z);
    }

    return {
      center: new Float32Array([
        (minX + maxX) * 0.5,
        (minY + maxY) * 0.5,
        (minZ + maxZ) * 0.5,
      ]),
      extents: new Float32Array([
        (maxX - minX) * 0.5,
        (maxY - minY) * 0.5,
        (maxZ - minZ) * 0.5,
      ]),
    };
  }

  private _getMaxBoundingBoxes(
    animationIndex: Nullable<number>
  ): Array<BoundingBox> {
    let maxBoundingBoxes = this._maxBoundingBoxesCache.get(animationIndex);
    if (!maxBoundingBoxes) {
      if (animationIndex !== null) {
        this._setActiveAnimation(animationIndex);
      }

      this._rootNode.position.setAll(0);
      this._rootNode.rotationQuaternion!.set(0, 0, 0, 1);
      this._rootNode.scaling.setAll(1);

      const maxExtents = computeMaxExtents(
        this._rootNode.getChildMeshes(),
        this._activeAnimationGroup
      );

      maxBoundingBoxes = maxExtents.map(
        (value) => new BoundingBox(value.minimum, value.maximum)
      );

      this._maxBoundingBoxesCache.set(animationIndex, maxBoundingBoxes);
    }

    return maxBoundingBoxes;
  }

  private _setActiveAnimation(animationIndex: number): AnimationGroup {
    const animationGroup = this._animationGroups[animationIndex];

    if (animationGroup !== this._activeAnimationGroup) {
      this._activeAnimationGroup?.stop();
      this._activeAnimationGroup = animationGroup;
      animationGroup.start();
      animationGroup.pause();
    }

    return animationGroup;
  }
}
