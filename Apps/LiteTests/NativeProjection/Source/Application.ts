// Native-projection declarations fail if executed without the explicit C99 host.
// These are test-application ABI slots, not a JavaScript engine implementation.
/** @liteNative bl_createBoxData */
function projectionBox(height: number): number { throw new Error("C99 projection required"); }
/** @liteNative bl_GeometryData.vertexCount */
function projectionPositionCount(handle: number): number { throw new Error("C99 projection required"); }
/** @liteNative bl_GeometryData.indexCount */
function projectionIndexCount(handle: number): number { throw new Error("C99 projection required"); }
/** @liteNative bl_GeometryData.positions */
function projectionPosition(handle: number, offset: number): number { throw new Error("C99 projection required"); }
/** @liteNative bl_freeGeometryData */
function projectionReleaseGeometry(handle: number): void { throw new Error("C99 projection required"); }
/** @liteNative bl_createTransformNode */
function projectionNode(): number { throw new Error("C99 projection required"); }
/** @liteNative bl_setNodePosition */
function projectionPositionNode(handle: number, x: number, y: number, z: number): void { throw new Error("C99 projection required"); }
/** @liteNative bl_setNodeParent */
function projectionParent(child: number, parent: number): void { throw new Error("C99 projection required"); }
/** @liteNative bl_appendNodeChild */
function projectionAppend(parent: number, child: number): void { throw new Error("C99 projection required"); }
/** @liteNative bl_removeNodeChild */
function projectionRemove(parent: number, child: number): void { throw new Error("C99 projection required"); }
/** @liteNative bl_getNodeWorldMatrix */
function projectionWorldX(handle: number): number { throw new Error("C99 projection required"); }
/** @liteNative bl_disposeNode */
function projectionDisposeNode(handle: number): void { throw new Error("C99 projection required"); }

export function geometryCycles(cycles: number, height: number): number {
    let checked = 0;
    for (let cycle = 0; cycle < cycles; ++cycle) {
        const geometry = projectionBox(height);
        if (projectionPositionCount(geometry) !== 72 || projectionIndexCount(geometry) !== 36) {
            throw new Error("Native box counts differ from the original 24-face-vertex factory.");
        }
        let minimum = 1e20;
        let maximum = -1e20;
        for (let index = 1; index < 72; index += 3) {
            const position = projectionPosition(geometry, index);
            if (position < minimum) minimum = position;
            if (position > maximum) maximum = position;
        }
        if (minimum !== -height / 2 || maximum !== height / 2) {
            throw new Error("Native box height or origin changed.");
        }
        projectionReleaseGeometry(geometry);
        ++checked;
    }
    return checked;
}

export function hierarchyCycles(cycles: number, offset: number): number {
    let checked = 0;
    for (let cycle = 0; cycle < cycles; ++cycle) {
        const parent = projectionNode();
        const child = projectionNode();
        projectionPositionNode(parent, offset, 0, 0);
        projectionPositionNode(child, 1, 2, 3);
        projectionParent(child, parent);
        projectionAppend(parent, child);
        if (projectionWorldX(child) !== offset + 1) {
            throw new Error("Native hierarchy did not preserve source parent/child transforms.");
        }
        projectionRemove(parent, child);
        projectionParent(child, 0);
        projectionDisposeNode(child);
        projectionDisposeNode(parent);
        ++checked;
    }
    return checked;
}
