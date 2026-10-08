// Browser-shaped host transport only. Native RmlUI owns layout and GPU effects.
export function retainedUi(host, document, canvas) {
    if (!host.retainedUi) return null;
    const api = globalThis._liteNative;
    const changed = new Set();
    const elements = new Map();
    const images = new Map();
    const contexts = [];
    let engine;
    let nextElement = 1;
    let density = 1;
    let seconds = 0;
    const eventKinds = new Map([["click", 0], ["change", 1], ["mousedown", 2],
        ["mouseup", 3], ["keydown", 4], ["keyup", 5]]);
    const eventNames = [...eventKinds.keys()];
    const keyKinds = new Map(["Backspace", "Tab", "Enter", "Escape", " ", "ArrowLeft",
        "ArrowRight", "ArrowUp", "ArrowDown", "Home", "End", "Delete",
        "a", "c", "v", "x", "y", "z"].map((key, index) => [key, index + 1]));
    const keyNames = new Map([...keyKinds].map(([key, kind]) => [kind, key]));

    function context(index) {
        if (contexts[index]) return contexts[index];
        const ranges = [[64, 96], [32, 32], [160, 96]];
        const value = api.createUiContext(engine, {
            firstViewId: ranges[index][0], viewCount: ranges[index][1], densityRatio: density,
        });
        contexts[index] = value;
        if (index === 1) api.setUiWhiteDifference(value, true);
        const root = api.getUiRoot(value);
        api.setUiProperty(root, "font-family", "Segoe UI");
        api.setUiProperty(root, "font-size", "16dp");
        api.setUiProperty(root, "pointer-events", "none");
        if (index === 0) {
            for (const [file, family, weight, fallback] of host.systemFonts()) {
                api.loadUiFont(value, host.read(file), family, weight, false, fallback);
            }
        }
        return value;
    }

    function record(element, index) {
        if (element === canvas || element.tagName === "CANVAS") return null;
        if (element._ui) {
            if (element._ui.index !== index) throw new Error("Moving a live DOM subtree between native UI contexts is unsupported.");
            return element._ui;
        }
        const handle = element === document.body ? api.getUiRoot(context(index)) :
            api.createUiElement(context(index), element.tagName.toLowerCase());
        const value = { handle, index, id: nextElement++, properties: new Map(), listeners: new Map(), parent: null };
        element._ui = value;
        elements.set(value.id, element);
        changed.add(element);
        return value;
    }

    function attach(element, parent, index) {
        if (element === canvas || element.tagName === "CANVAS") return;
        if (element.style.mixBlendMode === "difference") {
            index = 1;
            parent = api.getUiRoot(context(index));
        } else if (element.parentNode === document.body && Number(element.style.zIndex) >= 50) {
            index = 2;
            parent = api.getUiRoot(context(index));
        }
        const value = record(element, index);
        if (value.parent !== parent) {
            if (value.parent) api.removeUiChild(value.parent, value.handle);
            api.appendUiChild(parent, value.handle);
            value.parent = parent;
        }
        for (const child of element.children) attach(child, value.handle, index);
    }

    function property(value, name, text) {
        if (value.properties.get(name) === text) return;
        try { api.setUiProperty(value.handle, name, text); }
        catch (error) {
            error.message += ` [browser UI ${value.id}, ${name}:${text}]`;
            throw error;
        }
        value.properties.set(name, text);
    }

    function image(value, url, pixelated) {
        const key = `${value.index}:${url}:${pixelated}`;
        let source = images.get(key);
        if (!source) {
            const decoded = host.decodeImage(host.read(url));
            source = `host-image-${images.size}`;
            api.registerUiImage(context(value.index), source, decoded.pixels, decoded.width, decoded.height);
            api.setUiImageSampling(context(value.index), source, pixelated);
            images.set(key, source);
        }
        property(value, "decorator", `image("${source}" cover)`);
    }

    function sync(element) {
        const value = element._ui;
        if (!value) return;
        if (element !== document.body && !element.parentNode && value.parent) {
            api.removeUiChild(value.parent, value.handle);
            value.parent = null;
        }
        const css = element.style.cssText;
        if (value.css !== css) {
            const declarations = host.translateCss(css);
            const names = new Set();
            const duration = Number(declarations.find(item => item.name === "host-opacity-duration")?.value ?? 0);
            for (const { name, value: text } of declarations) {
                if (name === "host-image") {
                    image(value, text, element.style.imageRendering === "pixelated");
                    names.add("decorator");
                } else if (name === "host-white-crosshair") {
                    if (!value.crosshair) {
                        for (const css of [
                            "position:absolute;left:10dp;top:0dp;width:2dp;height:22dp;background-color:#fff",
                            "position:absolute;left:0dp;top:10dp;width:10dp;height:2dp;background-color:#fff",
                            "position:absolute;left:12dp;top:10dp;width:10dp;height:2dp;background-color:#fff",
                        ]) {
                            const child = api.createUiElement(context(1), "div");
                            for (const declaration of host.translateCss(css)) api.setUiProperty(child, declaration.name, declaration.value);
                            api.appendUiChild(value.handle, child);
                        }
                        value.crosshair = true;
                    }
                } else if (name.startsWith("host-")) {
                    // Metadata was validated by the native browser-CSS frontend.
                } else if (name === "opacity") {
                    const result = host.cssOpacity(value.id, Number(text), duration);
                    if (result !== null) property(value, name, String(result));
                    names.add(name);
                } else {
                    property(value, name, text);
                    names.add(name);
                }
            }
            // Browser absolute auto-width uses shrink-to-fit, unlike Rml block width.
            if (element.style.position === "absolute" && !element.style.width && element.style.left === "50%" && !element.style.display) {
                property(value, "display", "inline-block");
                const padding = (element.style.padding || "0").split(/\s+/).map(parseFloat);
                const horizontal = (padding[1] ?? padding[0] ?? 0) * 2 * density;
                property(value, "max-width", `${Math.max(0, host.width / 2 - horizontal)}px`);
                names.add("display");
                names.add("max-width");
            }
            for (const name of value.properties.keys()) {
                if (!names.has(name)) {
                    api.removeUiProperty(value.handle, name);
                    value.properties.delete(name);
                }
            }
            value.css = css;
        }
        if ((value.text !== element.textContent || value.markup !== element._markup) &&
            (element.textContent || value.text || element._markup)) {
            if (element._markup?.includes("<")) api.setUiMarkup(value.handle, element._markup);
            else api.setUiText(value.handle, element.textContent);
            value.text = element.textContent;
            value.markup = element._markup;
        }
        for (const [name, text] of Object.entries(element._attributes ?? {})) {
            api.setUiAttribute(value.handle, name, text);
        }
        for (const [name, kind] of eventKinds) {
            const wanted = (name === "change" && ["INPUT", "TEXTAREA", "SELECT"].includes(element.tagName)) ||
                (element.listeners.get(name)?.length ?? 0) > 0 || typeof element[`on${name}`] === "function";
            if (wanted && !value.listeners.has(name)) {
                value.listeners.set(name, api.addUiEventListener(value.handle, kind, event => {
                    const target = [...elements.values()].find(item => item._ui.handle === event.target) ?? element;
                    if (event.kind === 1) element._attributes.value = event.value;
                    element.dispatchEvent({ type: eventNames[event.kind], target, clientX: event.x, clientY: event.y,
                        button: event.button === 1 ? 2 : event.button === 2 ? 1 : 0, nativeValue: event.value,
                        key: keyNames.get(event.key) ?? "", shiftKey: Boolean(event.modifiers & 1),
                        ctrlKey: Boolean(event.modifiers & 2), altKey: Boolean(event.modifiers & 4),
                        metaKey: Boolean(event.modifiers & 8),
                        nativeKey: event.key, nativeModifiers: event.modifiers });
                }));
            } else if (!wanted && value.listeners.has(name)) {
                api.removeUiEventListener(value.handle, value.listeners.get(name));
                value.listeners.delete(name);
            }
        }
    }

    function flush() {
        if (!engine) return;
        const root = record(document.body, 0);
        for (const child of document.body.children) attach(child, root.handle, 0);
        for (const element of changed) sync(element);
        changed.clear();
    }
    globalThis._platformAttachUi = handle => {
        if (engine && engine !== handle) throw new Error("The browser DOM host currently supports one engine.");
        engine = handle;
        context(0);
        changed.add(document.body);
    };
    globalThis._platformUiUpdate = milliseconds => {
        seconds = milliseconds / 1000;
        flush();
        for (const [id, alpha] of host.cssAdvance(seconds)) {
            const element = elements.get(id);
            if (element) property(element._ui, "opacity", String(alpha));
        }
        for (const value of contexts) if (value) api.updateUi(value, seconds);
    };
    globalThis._platformUiRender = () => {
        for (const index of [1, 0, 2]) if (contexts[index]) api.renderUi(contexts[index]);
    };
    globalThis._platformUiStats = () => contexts.filter(Boolean).map(value => api.getUiStats(value));
    globalThis._platformUiDispose = () => {
        for (const value of contexts) if (value) api.disposeUiContext(value);
        contexts.length = 0;
        images.clear();
        elements.clear();
        changed.clear();
        engine = null;
    };
    globalThis._platformUiViewport = (width, height, ratio) => {
        density = ratio;
        host.width = width;
        host.height = height;
        canvas.width = width;
        canvas.height = height;
        if (engine) api.resizeEngine(engine, width, height);
        for (const value of contexts) if (value) api.setUiViewport(value, width, height, ratio);
        for (const element of elements.values()) {
            element._ui.css = null;
            changed.add(element);
        }
    };
    globalThis._platformUiValidate = frame => {
        if (frame === 120 || frame === 170) globalThis._liteForceGC?.();
        const underwater = [...elements.values()].find(element =>
            element.style.boxShadow === "inset 0 0 220px 60px rgba(4,26,60,0.85)");
        if (frame >= 260 && frame <= 330) {
            if (!underwater) throw new Error("UI-only validation could not locate the original underwater element.");
            underwater.style.opacity = "1";
        }
        if (frame === 300) {
            const toast = [...elements.values()].find(element => element.style.top === "64px");
            if (!toast || !toast.textContent) throw new Error("Original save/load toast was not transported.");
        }
        if (frame === 380) {
            const toast = [...elements.values()].find(element => element.style.top === "64px");
            if (!toast || Number(toast._ui.properties.get("opacity")) > 0.001) {
                throw new Error("Original virtual-clock toast expiry/ease did not complete.");
            }
        }
    };
    globalThis._platformUiInput = event => {
        if (!contexts[0] || document.pointerLockElement) return false;
        const kinds = { mousemove: 0, mousedown: 1, mouseup: 2, mouseleave: 3, wheel: 4, keydown: 5, keyup: 6, text: 7 };
        if (!(event.type in kinds)) return false;
        return api.processUiInput(contexts[0], { kind: kinds[event.type],
            x: event.type === "wheel" ? 0 : event.clientX ?? 0,
            y: event.type === "wheel" ? event.deltaY / 120 : event.clientY ?? 0,
            button: event.button === 2 ? 1 : event.button === 1 ? 2 : 0,
            key: keyKinds.get(event.key) ?? 0,
            modifiers: (event.shiftKey ? 1 : 0) | (event.ctrlKey ? 2 : 0),
            text: event.type === "text" ? event.text : "" });
    };
    return { changed: element => changed.add(element), flush };
}
