import { SceneContext } from "./sceneContext";
export declare function setEnvironment(environmentData: ArrayBuffer): void;
export declare function createSceneAsync(modelData: ArrayBuffer): Promise<SceneContext>;
export declare function destroyScene(context: SceneContext): void;
