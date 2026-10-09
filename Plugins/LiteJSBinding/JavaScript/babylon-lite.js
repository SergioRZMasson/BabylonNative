// Shape/identity/property marshalling only. All engine behavior is native.
const native = globalThis._liteNative;
if (!native) throw new Error("LiteJSBinding has not been initialized by the host.");

const wrappers = new WeakMap();
const handles = new WeakMap();
const meshHandles = new WeakSet();
const cameraHandles = new WeakSet();
const sceneEntities = new WeakMap();
const referenceValues = [];
const flushReference = Symbol("native-reference-values");

globalThis._liteFlushReferenceValues = () => {
    let live = 0;
    for (const reference of referenceValues) {
        const object = reference.deref();
        if (object) {
            object[flushReference]();
            referenceValues[live++] = reference;
        }
    }
    referenceValues.length = live;
};

function tupleFields(object, handle, properties, names, set) {
    const values = {};
    const previous = {};
    for (const name of names) {
        values[name] = properties[name];
        previous[name] = [...values[name]];
        field(object, name, () => values[name], value => {
            set(handle, name, value);
            values[name] = value;
            previous[name] = [...value];
        });
    }
    Object.defineProperty(object, flushReference, { value: () => {
        for (const name of names) {
            const value = values[name];
            if (value.length !== previous[name].length ||
                value.some((component, index) => component !== previous[name][index])) {
                set(handle, name, value);
                previous[name] = [...value];
            }
        }
    } });
    referenceValues.push(new WeakRef(object));
}

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

function vector(read, write, components, sameValueNoop = false) {
    const object = {};
    for (const component of components) {
        field(object, component, () => read()[component], value => {
            const data = read();
            if (sameValueNoop && data[component] === value) return;
            data[component] = value;
            write(data);
        });
    }
    Object.defineProperty(object, "set", {
        value: (...values) => write(Object.fromEntries(components.map((name, index) => [name, values[index]]))),
    });
    if (sameValueNoop) {
        Object.defineProperty(object, "copyFrom", { value: value => write(value) });
        Object.defineProperty(object, "toArray", { value: (out, offset = 0) => {
            const data = read();
            components.forEach((name, index) => { out[offset + index] = data[name]; });
        } });
    }
    return object;
}

function node(handle, readonlyPosition = false) {
    return identity(handle, (object, value) => {
        let parentReference = null;
        let childReferences = [];
        for (const name of ["position", "rotation", "scaling"]) {
            const proxy = vector(() => native.getNodeVector(value, name),
                data => native.setNodeVector(value, name, data), ["x", "y", "z"],
                readonlyPosition && name === "position");
            field(object, name, () => proxy, readonlyPosition && name === "position"
                ? undefined : data => native.setNodeVector(value, name, data));
        }
        const quaternion = vector(() => native.getNodeRotationQuaternion(value),
            data => native.setNodeRotationQuaternion(value, data), ["x", "y", "z", "w"]);
        field(object, "rotationQuaternion", () => quaternion,
            data => native.setNodeRotationQuaternion(value, data));
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
            const value = native.getMeshProperties2(handle)[name];
            if (name !== "material") return value;
            materialReference = identity(value);
            return materialReference;
        }, value => {
            native.setMeshProperty2(handle, name, name === "material" ? token(value) : value);
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
    const engine = native.createEngine(canvas, options);
    globalThis._platformAttachUi?.(engine);
    return identity(engine, (object, handle) => {
        for (const name of ["drawCallCount", "gpuFrameTimeMs"]) {
            field(object, name, () => native.getEngineStats(handle)[name]);
        }

    });
}

// Native retained-UI extensions, not browser or upstream package API claims.
export const nativeUi = Object.freeze({
    createContext: (engine, options) => native.createUiContext(token(engine), options),
    ...Object.fromEntries([
        "disposeUiContext", "setUiViewport", "getUiRoot", "createUiElement", "appendUiChild",
        "removeUiChild", "disposeUiElement", "setUiProperty", "removeUiProperty", "setUiText",
        "setUiAttribute", "setUiMarkup", "loadUiFont", "registerUiImage", "unregisterUiImage",
        "setUiImageSampling", "setUiWhiteDifference", "processUiInput", "updateUi", "renderUi",
        "getUiStats", "addUiEventListener", "removeUiEventListener",
    ].map(name => [name, (...args) => native[name](...args)])),
});

export function createSceneContext(engine) {
    return identity(native.createSceneContext(token(engine)), (object, handle) => {
        sceneEntities.set(object, []);
        let cameraReference = null;
        const color = vector(() => native.getSceneProperties2(handle).clearColor,
            value => native.setSceneProperty2(handle, "clearColor", value), ["r", "g", "b", "a"]);
        field(object, "clearColor", () => color, value => native.setSceneProperty2(handle, "clearColor", value));
        field(object, "camera", () => {
            const value = native.getSceneProperties2(handle).camera;
            cameraReference = value === null ? null : camera(value);
            return cameraReference;
        }, value => {
            native.setSceneProperty2(handle, "camera", token(value));
            cameraReference = value;
        });
        field(object, "fixedDeltaMs", () => native.getSceneProperties2(handle).fixedDeltaMs,
            value => native.setSceneProperty2(handle, "fixedDeltaMs", value));
    });
}

export const createFreeCamera = (position, target) => camera(native.createFreeCamera(position, target));
export function createArcRotateCamera(alpha, beta, radius, target) {
    const handle = native.createArcRotateCamera(alpha, beta, radius, target);
    const object = node(handle);
    cameraHandles.add(handle);
    const targetView = vector(() => native.getArcRotateCameraProperties(handle).target,
        value => native.setArcRotateCameraProperty(handle, "target", value), ["x", "y", "z"]);
    field(object, "target", () => targetView,
        value => native.setArcRotateCameraProperty(handle, "target", value));
    for (const name of ["alpha", "beta", "radius", "fov", "nearPlane", "farPlane", "inertia",
        "panningInertia", "angularSensibility", "panningSensibility", "wheelPrecision",
        "inertialAlphaOffset", "inertialBetaOffset", "inertialRadiusOffset",
        "inertialPanningX", "inertialPanningY"]) {
        field(object, name, () => native.getArcRotateCameraProperties(handle)[name],
            value => native.setArcRotateCameraProperty(handle, name, value));
    }
    for (const name of ["lowerAlphaLimit", "upperAlphaLimit", "lowerBetaLimit", "upperBetaLimit",
        "lowerRadiusLimit", "upperRadiusLimit"]) {
        field(object, name, () => native.getArcRotateCameraLimits(handle)[name],
            value => native.setArcRotateCameraLimitFields(handle, { [name]: value }));
    }
    return object;
}

export function setCameraLimits(object, limits, _unusedScene) {
    const disposer = native.setCameraLimits(token(object), limits);
    return () => native.removeCameraLimits(disposer);
}

export function attachControl(object, canvas, scene, options) {
    const mappings = {
        primaryButton: options?.pointerMappings?.primaryButton,
        secondaryButton: options?.pointerMappings?.secondaryButton,
        keyboard: options?.keyboard,
    };
    let currentPointerEvent = null;
    const transportOptions = {
        ...options,
        shouldHandlePointerDown: () =>
            options?.shouldHandlePointerDown ? options.shouldHandlePointerDown(currentPointerEvent) : true,
        isExternalDragActive: () => options?.isExternalDragActive?.() ?? false,
        isExternalPickPending: () => options?.isExternalPickPending?.() ?? false,
    };
    const control = native.attachControl(token(object), token(scene), transportOptions);
    const attached = [];
    let detached = false;
    const dispatch = event => {
        if (options?.pointerMappings?.primaryButton !== mappings.primaryButton ||
            options?.pointerMappings?.secondaryButton !== mappings.secondaryButton ||
            options?.keyboard !== mappings.keyboard) {
            native.setArcRotateControlOptions(control, {
                ...transportOptions, pointerMappings: options?.pointerMappings,
                keyboard: options?.keyboard,
            });
            mappings.primaryButton = options?.pointerMappings?.primaryButton;
            mappings.secondaryButton = options?.pointerMappings?.secondaryButton;
            mappings.keyboard = options?.keyboard;
        }
        const input = {
            type: event.type,
            pointerType: event.pointerType,
            pointerId: event.pointerId,
            button: event.button,
            clientX: event.clientX,
            clientY: event.clientY,
            deltaY: event.deltaY,
        };
        if (event.changedTouches) {
            input.changedTouches = Array.from(event.changedTouches, touch => ({
                identifier: touch.identifier, clientX: touch.clientX, clientY: touch.clientY,
            }));
        }
        let effects;
        const previousPointerEvent = currentPointerEvent;
        currentPointerEvent = event;
        try {
            effects = native.processArcRotateInput(control, input);
        } finally {
            currentPointerEvent = previousPointerEvent;
        }
        if (effects.capturePointer) canvas.setPointerCapture(effects.pointerId);
        if (effects.releasePointer) canvas.releasePointerCapture(effects.pointerId);
        if (effects.preventDefault) event.preventDefault();
    };
    const detach = () => {
        if (detached) return;
        native.detachControl(control);
        for (const [type, handler] of attached) canvas.removeEventListener(type, handler);
        detached = true;
    };
    try {
        for (const type of ["pointerdown", "pointermove", "pointerup", "wheel", "contextmenu",
            "touchstart", "touchmove", "touchend", "gesturestart", "gesturechange", "gestureend"]) {
            canvas.addEventListener(type, dispatch,
                ["wheel", "touchstart", "touchmove", "gesturestart", "gesturechange", "gestureend"].includes(type)
                    ? { passive: false } : undefined);
            attached.push([type, dispatch]);
        }
    } catch (error) {
        detach();
        throw error;
    }
    return detach;
}

export function createHemisphericLight(direction, intensity) {
    const handle = native.createHemisphericLight(direction, intensity);
    const object = node(handle);
    const directionView = vector(() => native.getHemisphericLightProperties(handle).direction,
        value => native.setHemisphericLightProperty(handle, "direction", value), ["x", "y", "z"]);
    field(object, "direction", () => directionView);
    field(object, "lightType", () => "hemispheric");
    field(object, "intensity", () => native.getHemisphericLightProperties(handle).intensity,
        value => native.setHemisphericLightProperty(handle, "intensity", value));
    tupleFields(object, handle, native.getHemisphericLightProperties(handle),
        ["diffuseColor", "specularColor", "groundColor"],
        (value, name, data) => native.setHemisphericLightProperty(value, name, data));
    return Object.seal(object);
}

export function createDirectionalLight(direction, intensity) {
    const handle = native.createDirectionalLight(direction, intensity);
    const object = node(handle, true);
    const directionView = vector(() => native.getDirectionalLightProperties(handle).direction,
        value => native.setDirectionalLightProperty(handle, "direction", value, true),
        ["x", "y", "z"], true);
    field(object, "direction", () => directionView);
    field(object, "lightType", () => "directional");
    field(object, "intensity", () => native.getDirectionalLightProperties(handle).intensity,
        value => native.setDirectionalLightProperty(handle, "intensity", value));
    tupleFields(object, handle, native.getDirectionalLightProperties(handle),
        ["diffuse", "specular"],
        (value, name, data) => native.setDirectionalLightProperty(value, name, data));
    Object.defineProperty(object, "_bumpLightVersion", {
        value: () => markLightUboDirty(object),
    });
    return Object.seal(object);
}

export function createStandardMaterial() {
    const handle = native.createStandardMaterial();
    const object = identity(handle);
    tupleFields(object, handle, native.getStandardMaterialProperties(handle),
        ["diffuseColor", "specularColor", "emissiveColor", "ambientColor"],
        (value, name, data) => native.setStandardMaterialProperty(value, name, data));
    for (const name of ["alpha", "specularPower", "backFaceCulling", "disableLighting"]) {
        field(object, name, () => native.getStandardMaterialProperties(handle)[name],
            value => native.setStandardMaterialProperty(handle, name, value));
    }
    const defaults = {
        diffuseTexture: null, diffuseCoordIndex: 0, bumpLevel: 1, specularCoordIndex: 0,
        ambientTexLevel: 1, ambientCoordIndex: 0, lightmapLevel: 1, lightmapCoordIndex: 1,
        useLightmapAsShadowmap: false, opacityLevel: 1, opacityFromRGB: false, alphaCutOff: 0,
        reflectionLevel: 1, reflectionCoordMode: 1,
    };
    for (const [name, value] of Object.entries(defaults)) {
        field(object, name, () => value, () => {
            throw new Error(`Standard ${name} is outside the current native feature contract.`);
        });
    }
    const uvScale = Object.freeze([1, 1]);
    field(object, "uvScale", () => uvScale, () => {
        throw new Error("Standard textures/UV features are outside the current native contract.");
    });
    return Object.seal(object);
}

export const setLightIntensity = (light, value) => native.setLightIntensity(token(light), value);
export const markLightUboDirty = light => {
    light[flushReference]?.();
    native.markLightUboDirty(token(light));
};
export const markMaterialUboDirty = material => {
    material[flushReference]?.();
    native.markMaterialUboDirty(token(material));
};
export const rebuildMaterial = (scene, material, options) => {
    if (!options?.rebuildFrameGraph) material[flushReference]?.();
    return native.rebuildMaterial(token(scene), token(material), options);
};
export const rebuildSceneRenderables = scene => native.rebuildSceneRenderables(token(scene));
export const createTransformNode = name => node(native.createTransformNode(name));
export const createBoxData = options => native.createBoxData(options);
export const createSphereData = options => native.createSphereData(options);
export const createFlatGroundData = options => native.createFlatGroundData(options);
export const createCylinderData = options => native.createCylinderData(options);
// Native helper exports: these six data functions are original module exports,
// not exports of the original Babylon Lite package root.
export const createPlaneData = options => native.createPlaneData(options);
export const createDiscData = options => native.createDiscData(options);
export const createPolyhedronData = options => native.createPolyhedronData(options);
export const createRibbonData = options => native.createRibbonData(options);
export const createTubeData = options => native.createTubeData(options);
export const createExtrudeShapeData = options => native.createExtrudeShapeData(options);
export const createCylinder = (engine, options) => mesh(native.createCylinder(token(engine), options));
export const createPlane = (engine, options) => mesh(native.createPlane(token(engine), options));
export const createDisc = (engine, options) => mesh(native.createDisc(token(engine), options));
export const createPolyhedron = (engine, options) => mesh(native.createPolyhedron(token(engine), options));
export const createRibbon = (engine, options) => mesh(native.createRibbon(token(engine), options));
export const createTube = (engine, options) => mesh(native.createTube(token(engine), options));
export const createExtrudeShape = (engine, options) => mesh(native.createExtrudeShape(token(engine), options));
export const createGround = (engine, options) => mesh(native.createGround(token(engine), options));
export const createBox = (engine, options) => mesh(native.createBox(token(engine), options));
export const createMeshFromData = (engine, name, ...streams) => mesh(native.createMeshFromData(token(engine), name, ...streams));
export const createShaderMaterial = options => identity(native.createShaderMaterial(options));
export const createSphere = (engine, options) => mesh(native.createSphere(token(engine), options));
export const createTexture2DFromPixels = (engine, ...args) => identity(native.createTexture2DFromPixels(token(engine), ...args));
export const setShaderFloat = (material, ...args) => native.setShaderFloat(token(material), ...args);
export const setShaderVector3 = (material, ...args) => native.setShaderVector3(token(material), ...args);
export const setShaderUniform = (material, ...args) => native.setShaderUniform(token(material), ...args);
export const setShaderUniformF32 = (material, ...args) => native.setShaderUniformF32(token(material), ...args);
export const setShaderMatrix = (material, ...args) => native.setShaderMatrix(token(material), ...args);
export const setShaderMatrixF32 = (material, ...args) => native.setShaderMatrixF32(token(material), ...args);
export const getShaderUniform = (material, ...args) => native.getShaderUniform(token(material), ...args);
export const setShaderTexture = (material, name, texture) => native.setShaderTexture(token(material), name, token(texture));
export const getShaderTexture = (material, name) => identity(native.getShaderTexture(token(material), name));
export const getTexture2DInfo = texture => native.getTexture2DInfo(token(texture));
export const updateTexture2DFromPixels = (engine, texture, ...args) =>
    native.updateTexture2DFromPixels(token(engine), token(texture), ...args);
export const updateMeshGeometry = (engine, object, geometry) =>
    native.updateMeshGeometry(token(engine), token(object), geometry);
export const resizeMeshGeometry = (engine, object, geometry) =>
    native.resizeMeshGeometry(token(engine), token(object), geometry);
export const updateMeshGeometryCapacity = (engine, object, ...args) =>
    native.updateMeshGeometryCapacity(token(engine), token(object), ...args);
export const updateMeshPositions = (engine, object, ...args) => native.updateMeshPositions(token(engine), token(object), ...args);
export const updateMeshNormals = (engine, object, ...args) => native.updateMeshNormals(token(engine), token(object), ...args);
export const updateMeshColors = (engine, object, ...args) => native.updateMeshColors(token(engine), token(object), ...args);
export const updateMeshUvs = (engine, object, ...args) => native.updateMeshUvs(token(engine), token(object), ...args);
export const updateMeshUv2 = (engine, object, ...args) => native.updateMeshUv2(token(engine), token(object), ...args);
export const updateMeshTangents = (engine, object, ...args) => native.updateMeshTangents(token(engine), token(object), ...args);
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
export const onSceneDispose = (scene, callback) => native.onSceneDispose(token(scene), callback);
export const removeSceneCallback = (scene, callback) => native.removeSceneCallback(token(scene), callback);
export async function registerScene(scene) { native.registerScene(token(scene)); }
export const unregisterScene = scene => native.unregisterScene(token(scene));
export const disposeScene = scene => { native.disposeScene(token(scene)); sceneEntities.delete(scene); };
export const startEngine = engine => native.startEngine(token(engine));
export const stopEngine = engine => native.stopEngine(token(engine));
export const renderFrame = (engine, deltaMs) => native.renderFrame(token(engine), deltaMs);
export const invalidateRenderBundles = engine => native.invalidateRenderBundles(token(engine));
export async function createAudioEngineAsync(options) {
    return identity(native.createAudioEngineAsync(options), (object, handle) => {
        object.audioContext = handle.audioContext;
    });
}
export async function unlockAudioEngineAsync(engine) { native.unlockAudioEngineAsync(token(engine)); }
export const getAudioEngineInfo = engine => native.getAudioEngineInfo(token(engine));
export const getMasterVolume = engine => native.getMasterVolume(token(engine));
export const setMasterVolume = (engine, ...args) => native.setMasterVolume(token(engine), ...args);
export const setSoundSourceVolume = (source, ...args) => native.setSoundSourceVolume(token(source), ...args);
export const audioUserGesture = engine => native.audioUserGesture(token(engine));

// Embedding-only helpers; these are not claims about the upstream package exports.
export const nativeExtras = Object.freeze({
    disposeEngine: engine => native.disposeEngine(token(engine)),
    resizeEngine: (engine, width, height) => native.resizeEngine(token(engine), width, height),
    waitForGpuIdle: engine => native.waitForGpuIdle(token(engine)),
    waitForGpuResourceRetirements: engine => native.waitForGpuResourceRetirements(token(engine)),
    getHostServices: () => native.getHostServices(),
});
export async function createSoundSourceAsync(engine, source, options) {
    const result = identity(native.createSoundSourceAsync(token(engine), source, options));
    // A native source retains its external input and engine, just as the source API does.
    Object.defineProperty(result, "_sourceOwnership", { value: [engine, source] });
    return result;
}

// The original, uncalled decoder helper imports these. They are not coverage.
export function setDracoBaseUrl() { throw new Error("Draco loading is outside the current native contract."); }
export function setMeshoptBaseUrl() { throw new Error("Meshopt loading is outside the current native contract."); }
