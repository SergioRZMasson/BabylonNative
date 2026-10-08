// Native Win32 browser-platform services for demo/user code only. No engine code.
import { retainedUi } from "./native-ui.js";
const host = globalThis._litePlatform;
let ui;
const nativeImages = new Map();
const timers = new Map();
const animation = new Map();
let nextTask = 1;
let clock = 0;

class EventTarget {
    constructor() { this.listeners = new Map(); }
    addEventListener(type, callback) {
        if (!this.listeners.has(type)) this.listeners.set(type, []);
        this.listeners.get(type).push(callback);
        ui?.changed(this);
    }
    removeEventListener(type, callback) {
        this.listeners.set(type, (this.listeners.get(type) ?? []).filter(value => value !== callback));
        ui?.changed(this);
    }
    dispatchEvent(event) {
        event.target ??= this;
        event.preventDefault ??= () => { event.defaultPrevented = true; };
        for (const callback of [...(this.listeners.get(event.type) ?? [])]) callback.call(this, event);
        this[`on${event.type}`]?.(event);
        return !event.defaultPrevented;
    }
}

class Style {
    constructor() { this.values = {}; }
    set cssText(text) {
        this.values = {};
        for (const declaration of host.parseCss(String(text))) {
            this.values[declaration.name.replace(/-([a-z])/g, (_, letter) => letter.toUpperCase())] = declaration.value;
        }
    }
    get cssText() { return Object.entries(this.values).map(([key, value]) => `${key.replace(/[A-Z]/g, letter => "-" + letter.toLowerCase())}:${value}`).join(";"); }
}

class Element extends EventTarget {
    constructor(tag) {
        super();
        this.tagName = tag.toUpperCase();
        this.children = [];
        this.dataset = {};
        this.parentNode = null;
        this._textContent = "";
        this._attributes = {};
        this.style = new Proxy(new Style(), {
            get(target, key) { return key in target ? target[key] : target.values[key] ?? ""; },
            set: (target, key, value) => {
                const before = target.cssText;
                if (key === "cssText") target.cssText = value; else target.values[key] = String(value);
                if (before !== target.cssText) ui?.changed(this);
                return true;
            },
        });
    }
    get textContent() { return this._textContent; }
    set textContent(value) {
        value = String(value);
        const hadMarkup = this._markup !== undefined;
        this._markup = undefined;
        if (this._textContent !== value || hadMarkup) {
            for (const child of this.children) child.parentNode = null;
            this.children = [];
            this._textContent = value;
            ui?.changed(this);
        }
    }
    setAttribute(name, value) { this._attributes[name] = String(value); ui?.changed(this); }
    getAttribute(name) { return this._attributes[name] ?? null; }
    get value() { return this._attributes.value ?? ""; }
    set value(value) { this.setAttribute("value", value); }
    appendChild(child) { child.remove(); child.parentNode = this; this.children.push(child); ui?.changed(child); return child; }
    remove() {
        if (this.parentNode) this.parentNode.children = this.parentNode.children.filter(child => child !== this);
        this.parentNode = null;
        ui?.changed(this);
    }
    set innerHTML(value) {
        value = String(value);
        this.textContent = value.replace(/<[^>]*>/g, "");
        this._markup = value;
        ui?.changed(this);
    }
    get innerHTML() { return this._markup ?? this.textContent; }
    getBoundingClientRect() {
        if (host.retainedUi && this.tagName !== "CANVAS") {
            throw new Error("The C99 UI contract does not expose element bounds; no browser-layout estimate is substituted.");
        }
        return { left: 0, top: 0, width: this.width ?? host.width, height: this.height ?? host.height };
    }
    requestPointerLock() {
        host.pointerLock(true);
        document.pointerLockElement = this;
        document.dispatchEvent({ type: "pointerlockchange" });
        return Promise.resolve();
    }
    click() { this.dispatchEvent({ type: "click", button: 0 }); }
}

class Canvas extends Element {
    constructor() { super("canvas"); this.width = 300; this.height = 150; this.pixels = null; }
    getContext(type) {
        if (type !== "2d") return null;
        if (!this.pixels || this.pixels.length !== this.width * this.height * 4) this.pixels = new Uint8ClampedArray(this.width * this.height * 4);
        const canvas = this;
        return {
            fillStyle: "#000000",
            fillRect(x, y, width, height) {
                const color = rgba(this.fillStyle);
                for (let row = Math.max(0, y); row < Math.min(canvas.height, y + height); ++row) {
                    for (let col = Math.max(0, x); col < Math.min(canvas.width, x + width); ++col) {
                        const index = (row * canvas.width + col) * 4;
                        canvas.pixels.set(color.map((value, component) => Math.round(value * (component === 3 ? 255 : 1))), index);
                    }
                }
            },
            drawImage(image, x, y, width = image.width, height = image.height) {
                for (let row = 0; row < height; ++row) {
                    const sourceY = Math.min(image.height - 1, Math.floor(row * image.height / height));
                    for (let col = 0; col < width; ++col) {
                        if (x + col < 0 || x + col >= canvas.width || y + row < 0 || y + row >= canvas.height) continue;
                        const sourceX = Math.min(image.width - 1, Math.floor(col * image.width / width));
                        const source = (sourceY * image.width + sourceX) * 4;
                        canvas.pixels.set(image.pixels.subarray(source, source + 4), ((y + row) * canvas.width + x + col) * 4);
                    }
                }
            },
            getImageData(x, y, width, height) {
                const data = new Uint8ClampedArray(width * height * 4);
                for (let row = 0; row < height; ++row) data.set(canvas.pixels.subarray(((row + y) * canvas.width + x) * 4, ((row + y) * canvas.width + x + width) * 4), row * width * 4);
                return { width, height, data };
            },
        };
    }
}

function rgba(value) {
    if (!value || value === "transparent") return [0, 0, 0, 0];
    if (value === "white") return [255, 255, 255, 1];
    if (value.startsWith("#")) {
        const hex = value.match(/^#([0-9a-f]+)/i)[1];
        const expanded = hex.length === 3 ? [...hex].map(letter => letter + letter).join("") : hex;
        return [parseInt(expanded.slice(0, 2), 16), parseInt(expanded.slice(2, 4), 16), parseInt(expanded.slice(4, 6), 16), 1];
    }
    const match = value.match(/rgba?\(([^)]+)\)/);
    if (match) { const values = match[1].split(",").map(Number); return [values[0], values[1], values[2], values[3] ?? 1]; }
    return [0, 0, 0, 0];
}

class NativeURL {
    constructor(value, base = "app:///Demos/minecraft.ts") {
        let combined = String(value);
        if (!/^[a-z]+:/i.test(combined)) combined = base.slice(0, base.lastIndexOf("/") + 1) + combined;
        this.prefix = combined.includes("://") ? combined.slice(0, combined.indexOf("://") + 3) : "";
        const path = combined.slice(this.prefix.length);
        const parts = [];
        for (const part of path.split("/")) { if (part === "..") parts.pop(); else if (part !== ".") parts.push(part); }
        this.pathname = parts.join("/");
    }
    get href() { return this.prefix + this.pathname; }
    toString() { return this.href; }
}

class NativeBlob {
    constructor(parts, options = {}) {
        const arrays = parts.map(part => typeof part === "string" ? host.encode(part) : part instanceof ArrayBuffer ? new Uint8Array(part) : part);
        this.bytes = new Uint8Array(arrays.reduce((sum, array) => sum + array.byteLength, 0));
        let offset = 0;
        for (const array of arrays) { this.bytes.set(array, offset); offset += array.byteLength; }
        this.type = options.type ?? "";
        this.size = this.bytes.length;
    }
    async arrayBuffer() { return this.bytes.buffer.slice(0); }
    async text() { return host.decodeText(this.bytes); }
}

class NativeStream {
    constructor(source) { this.source = source; this.queue = []; this.closed = false; }
    getReader() {
        const stream = this;
        const controller = {
            enqueue(value) { stream.queue.push(value); },
            close() { stream.closed = true; },
            error(error) { stream.error = error; },
        };
        return {
            async read() {
                if (!stream.queue.length && !stream.closed) await stream.source.pull(controller);
                if (stream.error) throw stream.error;
                return stream.queue.length ? { value: stream.queue.shift(), done: false } : { done: true };
            },
            async cancel(reason) { stream.closed = true; await stream.source.cancel?.(reason); },
        };
    }
}

class NativeResponse {
    constructor(body, options = {}) {
        this.status = options.status ?? 200;
        this.statusText = options.statusText ?? "OK";
        this.ok = this.status >= 200 && this.status < 300;
        this.headers = options.headers?.get ? options.headers : { get: name => options.headers?.[name.toLowerCase()] ?? null };
        this.body = body?.getReader ? body : new NativeStream({ pull: controller => { controller.enqueue(body); controller.close(); } });
    }
    async blob() {
        const chunks = [];
        const reader = this.body.getReader();
        for (;;) { const result = await reader.read(); if (result.done) break; chunks.push(result.value); }
        return new NativeBlob(chunks, { type: this.headers.get("content-type") });
    }
    async text() { return (await this.blob()).text(); }
    async json() { return JSON.parse(await this.text()); }
}

const document = new EventTarget();
document.body = new Element("body");
document.createElement = tag => tag === "canvas" ? new Canvas() : new Element(tag);
const canvas = new Canvas();
canvas.width = host.width;
canvas.height = host.height;
canvas.id = "renderCanvas";
document.body.appendChild(canvas);
ui = retainedUi(host, document, canvas);
document.getElementById = id => {
    const find = element => element.id === id ? element : element.children.map(find).find(Boolean);
    return find(document.body) ?? null;
};
document.pointerLockElement = null;
document.exitPointerLock = () => { host.pointerLock(false); document.pointerLockElement = null; document.dispatchEvent({ type: "pointerlockchange" }); };
const windowEvents = new EventTarget();
globalThis.window = globalThis;
globalThis.document = document;
globalThis.addEventListener = windowEvents.addEventListener.bind(windowEvents);
globalThis.removeEventListener = windowEvents.removeEventListener.bind(windowEvents);
globalThis.location = { search: "", href: "app:///Demos/minecraft.ts" };
globalThis.URL = NativeURL;
globalThis.URLSearchParams = class {
    constructor(value) { this.values = new Map(value.replace(/^\?/, "").split("&").filter(Boolean).map(pair => pair.split("=").map(decodeURIComponent))); }
    get(name) { return this.values.get(name) ?? null; }
};
globalThis.Blob = NativeBlob;
globalThis.Response = NativeResponse;
globalThis.ReadableStream = NativeStream;
globalThis.DOMException = class extends Error { constructor(message, name) { super(message); this.name = name; } };
globalThis.performance = { now: () => clock };
globalThis._platformNow = () => clock;
globalThis.setTimeout = (callback, delay = 0) => { const id = nextTask++; timers.set(id, { callback, at: clock + delay }); return id; };
globalThis.clearTimeout = id => timers.delete(id);
globalThis.requestAnimationFrame = callback => { const id = nextTask++; animation.set(id, callback); return id; };
globalThis.cancelAnimationFrame = id => animation.delete(id);
globalThis.fetch = async url => {
    const bytes = host.read(String(url));
    return new NativeResponse(bytes, { headers: { "content-length": String(bytes.length), "content-type": "image/png" } });
};
globalThis.createImageBitmap = async blob => ({ ...host.decodeImage(blob.bytes), close() {} });
globalThis.showSaveFilePicker = async options => {
    const path = host.pickFile(true, options?.suggestedName ?? "world.voxelsave.json");
    if (!path) throw new DOMException("Cancelled", "AbortError");
    return { async createWritable() { return { async write(value) { host.write(path, typeof value === "string" ? host.encode(value) : value.bytes); }, async close() {} }; } };
};
globalThis.showOpenFilePicker = async () => {
    const path = host.pickFile(false, "");
    if (!path) throw new DOMException("Cancelled", "AbortError");
    return [{ async getFile() { return new NativeBlob([host.read(path)], { type: "application/json" }); } }];
};

function length(value, extent, fallback = 0) {
    if (!value) return fallback;
    return value.includes("%") ? parseFloat(value) * extent / 100 : parseFloat(value) || 0;
}

function drawDOM() {
    const commands = [];
    function visit(element, parent, inheritedOpacity = 1, flowX = null) {
        if (element === canvas || element.style.display === "none") return;
        const style = element.style;
        const font = style.font || `${style.fontSize || "13px"} ${style.fontFamily || "Segoe UI"}`;
        const fontSize = parseFloat(font.match(/([0-9]+)px/)?.[1] ?? "13");
        const text = element.textContent ?? "";
        const characterWidth = font.includes("monospace") ? 0.62 : 0.57;
        let width = length(style.width, parent.width, text ? Math.max(...text.split("\n").map(line => line.length)) * fontSize * characterWidth : parent.width);
        let height = length(style.height, parent.height, text ? text.split("\n").length * fontSize * 1.4 : parent.height);
        const padding = (style.padding || "0").split(" ").map(parseFloat);
        const padY = padding[0] || 0;
        const padX = padding[1] ?? padY;
        const border = parseFloat(style.border) || 0;
        if (style.display === "flex") {
            width = element.children.reduce((sum, child) => sum + length(child.style.width, parent.width) + 2 * (parseFloat(child.style.border) || 0), 0) +
                Math.max(0, element.children.length - 1) * length(style.gap, parent.width) + padX * 2;
            height = Math.max(...element.children.map(child => length(child.style.height, parent.height) + 2 * (parseFloat(child.style.border) || 0))) + padY * 2;
        } else {
            width += 2 * (padX + border);
            height += 2 * (padY + border);
        }
        let x = flowX ?? parent.x + length(style.left, parent.width);
        let y = parent.y + length(style.top, parent.height);
        if (style.right) x = parent.x + parent.width - width - length(style.right, parent.width);
        if (style.bottom) y = parent.y + parent.height - height - length(style.bottom, parent.height);
        if (style.inset === "0") { x = 0; y = 0; width = host.width; height = host.height; }
        if (style.transform.includes("translateX(-50%)") || style.transform.includes("translate(-50%")) x -= width / 2;
        if (style.transform.includes("translate(-50%,-50%)")) y -= height / 2;
        const opacity = inheritedOpacity * (style.opacity === "" ? 1 : Number(style.opacity));
        const background = style.background || style.backgroundColor;
        const color = rgba(background);
        color[3] *= opacity;
        if (background.includes("radial-gradient")) {
            const colors = [...background.matchAll(/rgba?\([^)]+\)/g)].map(match => rgba(match[0]));
            if (colors.length >= 2 && opacity > 0) commands.push({ kind: "radial", x, y, width, height, color: colors[0], outer: colors.at(-1), opacity });
        } else if (color[3] > 0) commands.push({ kind: "rect", x, y, width, height, color });
        if (style.boxShadow.startsWith("inset") && opacity > 0) {
            const shadow = style.boxShadow.match(/inset\s+0\s+0\s+([0-9]+)px\s+([0-9]+)px\s+(rgba?\([^)]+\))/);
            if (shadow) commands.push({ kind: "insetShadow", x, y, width, height, blur: Number(shadow[1]), spread: Number(shadow[2]), color: rgba(shadow[3]), opacity });
        }
        const imageUrl = background.match(/url\(["']?([^"')]+)["']?\)/)?.[1];
        if (imageUrl) {
            if (!nativeImages.has(imageUrl)) nativeImages.set(imageUrl, host.decodeImage(host.read(imageUrl)));
            commands.push({ kind: "image", x: x + border, y: y + border, width: width - 2 * border, height: height - 2 * border, image: nativeImages.get(imageUrl), opacity });
        }
        if (border) commands.push({ kind: "border", x, y, width, height, thickness: border, color: rgba(style.borderColor || style.border.slice(style.border.indexOf("solid") + 5).trim()), opacity });
        if (background.includes("linear-gradient")) {
            commands.push({ kind: "rect", x: x + width / 2 - 1, y, width: 2, height, color: [255, 255, 255, opacity] });
            commands.push({ kind: "rect", x, y: y + height / 2 - 1, width, height: 2, color: [255, 255, 255, opacity] });
        }
        if (text && opacity > 0) commands.push({ kind: "text", x: x + padX, y: y + padY, width, height, text, size: fontSize, font, color: rgba(style.color || "#fff"), opacity });
        let nextX = x + padX;
        for (const child of element.children) {
            visit(child, { x, y, width, height }, opacity, style.display === "flex" ? nextX : null);
            if (style.display === "flex") nextX += length(child.style.width, width) + 2 * (parseFloat(child.style.border) || 0) + length(style.gap, width);
        }
    }
    for (const child of document.body.children) visit(child, { x: 0, y: 0, width: host.width, height: host.height });
    host.hud(commands);
}

globalThis._platformDispatch = event => {
    if (event.type === "pointerunlock") { document.exitPointerLock(); return; }
    if (globalThis._platformUiInput?.(event)) return;
    if (event.type === "mousemove") document.dispatchEvent(event);
    else if (["click", "mousedown", "mouseup", "contextmenu"].includes(event.type)) canvas.dispatchEvent(event);
    else windowEvents.dispatchEvent(event);
};
globalThis._platformTick = deltaMs => {
    clock += deltaMs;
    for (const [id, task] of [...timers]) if (task.at <= clock) { timers.delete(id); task.callback(); }
    const frames = [...animation.values()];
    animation.clear();
    for (const callback of frames) callback(clock);
    if (!ui) drawDOM();
    if (canvas.dataset.error) throw new Error(canvas.dataset.error);
    return canvas.dataset.ready === "true";
};
