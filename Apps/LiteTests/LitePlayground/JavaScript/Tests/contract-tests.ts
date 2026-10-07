import {
    createAudioEngineAsync, createBoxData, createFreeCamera, createShaderMaterial,
    createSphereData, createTransformNode, getShaderUniform, setShaderFloat,
    setShaderVector3,
} from "babylon-lite";

declare const _liteTestsDone: (success: boolean, detail: string) => void;

function check(condition: unknown, message: string): asserts condition {
    if (!condition) throw new Error(message);
}

async function main(): Promise<void> {
    const parent = createTransformNode("parent");
    const child = createTransformNode("child");
    parent.position.x = 10;
    child.position.set(1, 2, 3);
    child.parent = parent;
    check(child.parent === parent, "native parent identity changed");
    check(parent.children.length === 0, "parent assignment implicitly appended a child");
    parent.children.push(child);
    check(parent.children[0] === child, "native children identity changed");
    check(child.worldMatrix[12] === 11, "mutable position did not reach native transform");
    check(child.position === child.position, "position proxy identity changed");
    if (typeof (globalThis as { _liteForceGC?: () => void })._liteForceGC === "function") {
        const raw = (globalThis as { _liteNative: {
            createTransformNode(name: string): object;
            setNodeVector(node: object, field: string, value: object): void;
            setNodeParent(child: object, parent: object): void;
            appendNodeChild(parent: object, child: object): void;
            getNodeWorldMatrix(node: object): Float64Array;
            getNodeParent(node: object): object;
        }})._liteNative;
        let rawParent: object | null = raw.createTransformNode("native-parent-gc");
        const rawChild = raw.createTransformNode("native-child-retained");
        raw.setNodeVector(rawParent, "position", { x: 10, y: 0, z: 0 });
        raw.setNodeVector(rawChild, "position", { x: 1, y: 0, z: 0 });
        raw.setNodeParent(rawChild, rawParent);
        raw.appendNodeChild(rawParent, rawChild);
        rawParent = null;
        (globalThis as { _liteForceGC: () => void })._liteForceGC();
        check(raw.getNodeWorldMatrix(rawChild)[12] === 11, "GC altered a retained native parent transform/hierarchy");
        check(raw.getNodeParent(rawChild) !== null, "GC cleared native child.parent");
    }
    child.rotation.set(0.5, Math.PI / 2, 0.75);
    child.rotation.x = 0.6;
    check(child.rotation.z === 0.75, "Euler proxy lost native cache");
    child.scaling.x = child.scaling.y = child.scaling.z = 0;
    check(child.scaling.z === 0, "zero scaling was discarded");
    const camera = createFreeCamera({ x: 1, y: 2, z: 3 }, { x: 0, y: 0, z: 0 });
    camera.nearPlane = 0.1;
    camera.target.x = 4;
    check(camera.nearPlane === 0.1 && camera.target.x === 4, "camera properties did not reach native state");
    const a = createBoxData();
    const b = createBoxData();
    check(a.positions.length === 72 && a.indices.length === 36, "native box counts mismatch");
    a.positions[0] = 123;
    check(b.positions[0] !== 123, "factory typed arrays were shared");
    const sky = createSphereData({ segments: 16, diameter: 4000 });
    check(sky.positions.length === 703 * 3 && sky.indices.length === 3888, "Minecraft sky was simplified");
    const material = createShaderMaterial({
        vertexSource: "@vertex fn mainVertex(input: VertexInput) -> @builtin(position) vec4f { return vec4f(input.position, 1); }",
        fragmentSource: "@fragment fn mainFragment() -> @location(0) vec4f { return vec4f(1); }",
        attributes: ["position"],
        uniforms: [{ name: "amount", type: "f32" }, { name: "tint", type: "vec3<f32>" }],
    });
    setShaderFloat(material, "amount", 0.7);
    setShaderVector3(material, "tint", [0.1, 0.2, 0.3]);
    check(Math.abs(getShaderUniform(material, "amount") - 0.7) < 1e-7, "native scalar uniform result/rounding mismatch");
    const tint = getShaderUniform(material, "tint");
    check(Math.abs(tint[2] - 0.3) < 1e-7, "typed uniform marshalling mismatch");
    check(tint === getShaderUniform(material, "tint"), "uniform backing identity was not stable");
    setShaderVector3(material, "tint", [0.4, 0.5, 0.6]);
    check(Math.abs(tint[2] - 0.6) < 1e-7, "native setter did not update the existing JS backing view");
    let audioUnavailable = false;
    try { await createAudioEngineAsync(); }
    catch (error) { audioUnavailable = (error as { status: number }).status === 16; }
    check(audioUnavailable, "missing native audio host did not reject its Promise explicitly");
    _liteTestsDone(true, "Native property/identity/typed-array/Promise contract passed; not a Minecraft rendering result.");
}

main().catch(error => _liteTestsDone(false, String(error) + "\n" + (error.stack ?? "")));
