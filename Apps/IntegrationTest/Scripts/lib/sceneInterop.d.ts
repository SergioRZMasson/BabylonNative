import { SceneContext } from "./sceneContext";
export declare function createSceneAsync(environmentData: ArrayBuffer, modelData: ArrayBuffer): Promise<SceneContext>;
export declare function destroyScene(context: SceneContext): void;
