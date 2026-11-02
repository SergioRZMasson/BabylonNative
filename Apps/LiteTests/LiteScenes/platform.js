// Event listener ownership is a host responsibility, including when controls' detachers are unused.
(() => {
    const canvas = globalThis._canvas;
    const listeners = new Map();
    canvas.addEventListener = (type, handler) => {
        let handlers = listeners.get(type);
        if (!handlers) {
            handlers = new Set();
            listeners.set(type, handlers);
        }
        handlers.add(handler);
    };
    canvas.removeEventListener = (type, handler) => {
        const handlers = listeners.get(type);
        handlers?.delete(handler);
        if (handlers?.size === 0) listeners.delete(type);
    };
    canvas.dispatchEvent = event => {
        if (typeof event?.type !== "string") throw new TypeError("Expected a typed canvas event.");
        for (const handler of Array.from(listeners.get(event.type) ?? [])) {
            if (listeners.get(event.type)?.has(handler)) handler.call(canvas, event);
        }
        return !event.defaultPrevented;
    };
})();
