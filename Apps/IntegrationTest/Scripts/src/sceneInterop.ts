import { setEnvironmentTexture } from "./lighting";
import { engine, SceneContext } from "./sceneContext";

// Caches the shared environment/IBL map once (sent by the native host at startup) so it does not need to be
// transferred from native code to JS on every scene load. Must be called before createSceneAsync.
export function setEnvironment(environmentData: ArrayBuffer): void {
  setEnvironmentTexture(engine, environmentData);
}

export async function createSceneAsync(
  modelData: ArrayBuffer
): Promise<SceneContext> {
  return SceneContext.createAsync(modelData);
}

export function destroyScene(context: SceneContext): void {
  context.dispose();
}
