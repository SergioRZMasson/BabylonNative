import "@babylonjs/core/Lights/Shadows/shadowGeneratorSceneComponent";
import type { ShadowLight } from "@babylonjs/core/Lights/shadowLight";
import type { TransformNode } from "@babylonjs/core/Meshes/transformNode";
import type { Scene } from "@babylonjs/core/scene";
export declare function createLights(scene: Scene): Array<ShadowLight>;
export declare function configureEnvironment(scene: Scene, environmentData: ArrayBuffer): void;
export declare function enableShadows(lights: Array<ShadowLight>, rootNode: TransformNode): void;
