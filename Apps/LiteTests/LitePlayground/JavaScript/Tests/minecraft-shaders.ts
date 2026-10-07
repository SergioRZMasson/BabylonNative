import {
    addToScene, createBoxData, createEngine, createFreeCamera, createMeshFromData,
    createSceneContext, createTexture2DFromPixels, registerScene, startEngine,
} from "babylon-lite";
import { createVoxelMaterial } from "../Demos/minecraft/voxel-material.js";
import { createMobMaterial } from "../Demos/minecraft/mob-material.js";
import { createSky } from "../Demos/minecraft/sky.js";
import { createHighlight } from "../Demos/minecraft/highlight.js";
import { createClouds } from "../Demos/minecraft/clouds.js";

declare const _liteTestsDone: (success: boolean, detail: string) => void;

async function main(): Promise<void> {
    const engine = await createEngine({ width: 256, height: 256 });
    const scene = createSceneContext(engine);
    scene.camera = createFreeCamera({ x: 0, y: 0, z: -6 }, { x: 0, y: 0, z: 0 });
    scene.camera.nearPlane = 0.1;
    scene.camera.farPlane = 5000;
    const atlas = createTexture2DFromPixels(engine, new Uint8Array([255, 255, 255, 255]), 1, 1, { srgb: true });
    const data = createBoxData();
    const colors = new Float32Array(24 * 4).fill(1);
    for (const [index, mode] of ["opaque", "cutout", "blend"].entries()) {
        const mesh = createMeshFromData(engine, `voxel-${mode}`, data.positions, data.normals, data.indices,
            data.uvs, undefined, undefined, colors);
        mesh.material = createVoxelMaterial(`fixture-${mode}`, atlas, mode);
        mesh.position.x = index * 1.5 - 1.5;
        addToScene(scene, mesh);
    }
    const mob = createMeshFromData(engine, "mob-material", data.positions, data.normals, data.indices,
        data.uvs, undefined, undefined, colors);
    mob.material = createMobMaterial("fixture-mob");
    mob.position.y = 1.5;
    addToScene(scene, mob);
    const sky = createSky(engine);
    addToScene(scene, sky.mesh);
    const highlight = createHighlight(engine);
    highlight.show(0, 0, 0);
    addToScene(scene, highlight.mesh);
    createClouds(engine, scene);
    await registerScene(scene);
    await startEngine(engine);
    _liteTestsDone(engine.drawCallCount > 0,
        `Original Minecraft opaque/cutout/blend/mob/sky/highlight/cloud WGSL compiled and submitted (${engine.drawCallCount} draws). Shader subset only; full radius-6 Minecraft not executed.`);
}

main().catch(error => _liteTestsDone(false, String(error) + "\n" + (error.stack ?? "")));
