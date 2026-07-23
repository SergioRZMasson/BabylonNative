import type { ThinEngine } from "@babylonjs/core/Engines/thinEngine";
import { ShadowGenerator } from "@babylonjs/core/Lights/Shadows/shadowGenerator";
import "@babylonjs/core/Lights/Shadows/shadowGeneratorSceneComponent";
import { PointLight } from "@babylonjs/core/Lights/pointLight";
import type { ShadowLight } from "@babylonjs/core/Lights/shadowLight";
import { CubeTexture } from "@babylonjs/core/Materials/Textures/cubeTexture";
import { ImageProcessingConfiguration } from "@babylonjs/core/Materials/imageProcessingConfiguration";
import { Color3 } from "@babylonjs/core/Maths/math.color";
import { Vector3 } from "@babylonjs/core/Maths/math.vector";
import type { TransformNode } from "@babylonjs/core/Meshes/transformNode";
import { Tools } from "@babylonjs/core/Misc/tools";
import type { Scene } from "@babylonjs/core/scene";
import type { DeepImmutable, Nullable } from "@babylonjs/core/types";

function getBlurKernel(light: PointLight, bufferSize: number) {
  const normalizedBlurKernel = 0.03 / light.shadowAngle;
  const minimumBlurKernel = 5 / (bufferSize / 256);
  return Math.max(bufferSize * normalizedBlurKernel, minimumBlurKernel);
}

function createPointLight(
  name: string,
  position: DeepImmutable<Vector3>,
  color: DeepImmutable<Color3>,
  intensity: number,
  scene: Scene
): PointLight {
  const pointLight = new PointLight(name, position, scene);
  pointLight.radius = 0;
  pointLight.diffuse.copyFrom(color);
  pointLight.intensity = intensity;

  pointLight.shadowMinZ = 0.2;
  pointLight.shadowMaxZ = 10;
  pointLight.shadowAngle = Tools.ToRadians(45);
  pointLight.setDirectionToTarget(Vector3.Zero());
  pointLight.shadowEnabled = true;

  const bufferSize = 512.0;
  const shadowGeneratorBias = 0.002;
  const shadowGenerator = new ShadowGenerator(bufferSize, pointLight);
  shadowGenerator.useBlurCloseExponentialShadowMap = true;
  shadowGenerator.useKernelBlur = true;
  shadowGenerator.blurScale = 1;
  shadowGenerator.bias = shadowGeneratorBias;
  shadowGenerator.blurKernel = getBlurKernel(pointLight, bufferSize);
  shadowGenerator.depthScale =
    50 * (pointLight.shadowMaxZ - pointLight.shadowMinZ);
  shadowGenerator.transparencyShadow = true;
  shadowGenerator.frustumEdgeFalloff = 0;

  return pointLight;
}

export function createLights(scene: Scene): Array<ShadowLight> {
  return [
    createPointLight(
      "pointLight0",
      new Vector3(0.61, 1.97, 0.454),
      new Color3(1.0, 0.95, 0.92),
      5,
      scene
    ),
    createPointLight(
      "pointLight1",
      new Vector3(-1.05455824, 1.42028987, 1.60088825),
      new Color3(0.85, 0.87, 0.95),
      5.5,
      scene
    ),
    createPointLight(
      "pointLight2",
      new Vector3(-1.04830909, 1.61268401, -0.965823412),
      new Color3(0.868366683, 0.726999986, 1),
      3.125,
      scene
    ),
  ];
}

// The environment (IBL) texture is identical for every model/scene, so we cache it once and reuse it across
// all scenes instead of receiving the environment bytes from the native host and rebuilding a CubeTexture on
// every scene load. The texture is created against the engine (not a specific Scene) so it is NOT tracked by
// any scene's `scene.textures` list and therefore survives `scene.dispose()` - letting a single instance be
// shared by every scene for the lifetime of the engine.
let cachedEnvironmentTexture: Nullable<CubeTexture> = null;

// Caches the shared environment/IBL texture from the raw `.env` bytes sent once by the native host. Called
// before any scene is created; the resulting CubeTexture is reused by `configureEnvironment` for every scene.
export function setEnvironmentTexture(
  engine: ThinEngine,
  environmentData: ArrayBuffer
): void {
  // Release any previously cached texture (e.g. if the environment is ever re-sent) to avoid leaking it.
  cachedEnvironmentTexture?.dispose();

  const hdrTexture = new CubeTexture("data:environment.env", engine, {
    buffer: new Uint8Array(environmentData),
    forcedExtension: ".env",
  });
  hdrTexture.rotationY = 2.73;
  cachedEnvironmentTexture = hdrTexture;
}

export function configureEnvironment(scene: Scene) {
  // Add `Khronos PBR Neutral` tone mapping.
  scene.imageProcessingConfiguration.toneMappingEnabled = true;
  scene.imageProcessingConfiguration.toneMappingType =
    ImageProcessingConfiguration.TONEMAPPING_KHR_PBR_NEUTRAL;

  // Reuse the globally cached environment texture (set once via setEnvironmentTexture) instead of building a
  // new one from environment bytes per scene.
  scene.environmentTexture = cachedEnvironmentTexture;
}

export function enableShadows(
  lights: Array<ShadowLight>,
  rootNode: TransformNode
) {
  for (const light of lights) {
    const renderList = light.getShadowGenerator()!.getShadowMap()!.renderList!;
    for (const childMesh of rootNode.getChildMeshes()) {
      renderList.push(childMesh);
      childMesh.receiveShadows = true;
    }
  }
}
