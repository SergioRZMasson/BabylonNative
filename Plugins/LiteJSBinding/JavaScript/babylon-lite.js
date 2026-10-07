// Shape/identity/property marshalling only. All engine behavior is native.
const native = globalThis._liteNative;
if (!native) throw new Error("LiteJSBinding has not been initialized by the host.");

const wrappers = new WeakMap();
const handles = new WeakMap();
const meshHandles = new WeakSet();
const cameraHandles = new WeakSet();
const sceneEntities = new WeakMap();

function token(object) {
    if (object === null || object === undefined) return null;
    const value = handles.get(object);
    if (!value) throw new TypeError("Expected a Babylon Lite native object.");
    return value;
}

function identity(handle, initialize = () => {}) {
    if (handle === null) return null;
    const previous = wrappers.get(handle);
    if (previous) return previous;
    const object = {};
    wrappers.set(handle, object);
    handles.set(object, handle);
    initialize(object, handle);
    return object;
}

function field(object, name, read, write) {
    Object.defineProperty(object, name, { enumerable: true, get: read, set: write });
}

function vector(read, write, components) {
    const object = {};
    for (const component of components) {
        field(object, component, () => read()[component], value => {
            const data = read();
            data[component] = value;
            write(data);
        });
    }
    Object.defineProperty(object, "set", {
        value: (...values) => write(Object.fromEntries(components.map((name, index) => [name, values[index]]))),
    });
    return object;
}

function node(handle) {
    return identity(handle, (object, value) => {
        let parentReference = null;
        let childReferences = [];
        for (const name of ["position", "rotation", "scaling"]) {
            const proxy = vector(() => native.getNodeVector(value, name),
                data => native.setNodeVector(value, name, data), ["x", "y", "z"]);
            field(object, name, () => proxy, data => native.setNodeVector(value, name, data));
        }
        field(object, "name", () => native.getNodeName(value), name => native.setNodeName(value, name));
        field(object, "visible", () => native.getNodeVisible(value), visible => native.setNodeVisible(value, visible));
        field(object, "parent", () => {
            parentReference = node(native.getNodeParent(value));
            return parentReference;
        }, parent => {
            native.setNodeParent(value, token(parent));
            parentReference = parent;
        });
        const children = new Proxy([], {
            get(target, property) {
                childReferences = native.getNodeChildren(value).map(node);
                const values = childReferences;
                if (property === "push") return (...added) => {
                    for (const child of added) native.appendNodeChild(value, token(child));
                    childReferences.push(...added);
                    return native.getNodeChildren(value).length;
                };
                if (property === Symbol.iterator) return values[Symbol.iterator].bind(values);
                if (property === "length") return values.length;
                if (["pop", "shift", "unshift", "splice", "sort", "reverse", "fill", "copyWithin"].includes(property)) {
                    throw new Error(`children.${property} is outside the current native collection contract.`);
                }
                const result = values[property];
                return typeof result === "function" ? result.bind(values) : result;
            },
            set() {
                throw new Error("Assign children through native hierarchy operations.");
            },
        });
        field(object, "children", () => children);
        const worldMatrix = new Float32Array(16);
        field(object, "worldMatrix", () => {
            worldMatrix.set(native.getNodeWorldMatrix(value));
            return worldMatrix;
        });
    });
}

function mesh(handle) {
    const object = node(handle);
    if (meshHandles.has(handle)) return object;
    meshHandles.add(handle);
    let materialReference = null;
    for (const name of ["material", "renderOrder", "receiveShadows"]) {
        field(object, name, () => {
            const value = native.getMeshProperties(handle)[name];
            if (name !== "material") return value;
            materialReference = identity(value);
            return materialReference;
        }, value => {
            native.setMeshProperty(handle, name, name === "material" ? token(value) : value);
            if (name === "material") materialReference = value;
        });
    }
    return object;
}

function camera(handle) {
    const object = node(handle);
    if (cameraHandles.has(handle)) return object;
    cameraHandles.add(handle);
    const target = vector(() => native.getNodeVector(handle, "target"),
        data => native.setNodeVector(handle, "target", data), ["x", "y", "z"]);
    field(object, "target", () => target, data => native.setNodeVector(handle, "target", data));
    for (const name of ["fov", "nearPlane", "farPlane", "speed", "angularSensitivity", "inertia"]) {
        field(object, name, () => native.getCameraProperties(handle)[name],
            value => native.setCameraProperty(handle, name, value));
    }
    return object;
}

export async function createEngine(canvas, options) {
    return identity(native.createEngine(canvas, options), (object, handle) => {
        for (const name of ["drawCallCount", "gpuFrameTimeMs"]) {
            field(object, name, () => native.getEngineStats(handle)[name]);
        }
    });
}

export function createSceneContext(engine) {
    return identity(native.createSceneContext(token(engine)), (object, handle) => {
        sceneEntities.set(object, []);
        let cameraReference = null;
        const color = vector(() => native.getSceneProperties(handle).clearColor,
            value => native.setSceneProperty(handle, "clearColor", value), ["r", "g", "b", "a"]);
        field(object, "clearColor", () => color, value => native.setSceneProperty(handle, "clearColor", value));
        field(object, "camera", () => {
            const value = native.getSceneProperties(handle).camera;
            cameraReference = value === null ? null : camera(value);
            return cameraReference;
        }, value => {
            native.setSceneProperty(handle, "camera", token(value));
            cameraReference = value;
        });
        field(object, "fixedDeltaMs", () => native.getSceneProperties(handle).fixedDeltaMs,
            value => native.setSceneProperty(handle, "fixedDeltaMs", value));
    });
}

export const createFreeCamera = (position, target) => camera(native.createFreeCamera(position, target));
export const createTransformNode = name => node(native.createTransformNode(name));
export const createBoxData = options => native.createBoxData(options);
export const createSphereData = options => native.createSphereData(options);
export const createBox = (engine, options) => mesh(native.createBox(token(engine), options));
export const createMeshFromData = (engine, name, ...streams) => mesh(native.createMeshFromData(token(engine), name, ...streams));
export const createShaderMaterial = options => identity(native.createShaderMaterial(options));
export const createTexture2DFromPixels = (engine, ...args) => identity(native.createTexture2DFromPixels(token(engine), ...args));
export const setShaderFloat = (material, ...args) => native.setShaderFloat(token(material), ...args);
export const setShaderVector3 = (material, ...args) => native.setShaderVector3(token(material), ...args);
export const setShaderUniform = (material, ...args) => native.setShaderUniform(token(material), ...args);
export const getShaderUniform = (material, ...args) => native.getShaderUniform(token(material), ...args);
export const setShaderTexture = (material, name, texture) => native.setShaderTexture(token(material), name, token(texture));
export function addToScene(scene, entity) {
    native.addToScene(token(scene), token(entity));
    sceneEntities.get(scene).push(entity);
}
export function removeFromScene(scene, entity) {
    native.removeFromScene(token(scene), token(entity));
    sceneEntities.set(scene, sceneEntities.get(scene).filter(value => value !== entity));
}
export const setSubtreeVisible = (entity, visible) => native.setSubtreeVisible(token(entity), visible);
export const onBeforeRender = (scene, callback) => native.onBeforeRender(token(scene), callback);
export async function registerScene(scene) { native.registerScene(token(scene)); }
export const startEngine = engine => native.startEngine(token(engine));
export async function createAudioEngineAsync(options) {
    return identity(native.createAudioEngineAsync(options), (object, handle) => {
        object.audioContext = handle.audioContext;
    });
}
export async function unlockAudioEngineAsync(engine) { native.unlockAudioEngineAsync(token(engine)); }
export async function createSoundSourceAsync(engine, source, options) {
    const result = identity(native.createSoundSourceAsync(token(engine), source, options));
    // A native source retains its external input and engine, just as the source API does.
    Object.defineProperty(result, "_sourceOwnership", { value: [engine, source] });
    return result;
}

// The original, uncalled decoder helper imports these. They are not coverage.
export function setDracoBaseUrl() { throw new Error("Draco loading is outside the current native contract."); }
export function setMeshoptBaseUrl() { throw new Error("Meshopt loading is outside the current native contract."); }
