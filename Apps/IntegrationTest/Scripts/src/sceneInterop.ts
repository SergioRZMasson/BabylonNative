import { SceneContext } from "./sceneContext";

export async function createSceneAsync(
  environmentData: ArrayBuffer,
  modelData: ArrayBuffer
): Promise<SceneContext> {
  return SceneContext.createAsync(environmentData, modelData);
}

export function destroyScene(context: SceneContext): void {
  context.dispose();
}
