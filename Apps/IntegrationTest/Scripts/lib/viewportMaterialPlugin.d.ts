import type { Material } from "@babylonjs/core/Materials/material";
import type { MaterialDefines } from "@babylonjs/core/Materials/materialDefines";
import { MaterialPluginBase } from "@babylonjs/core/Materials/materialPluginBase";
import type { UniformBuffer } from "@babylonjs/core/Materials/uniformBuffer";
import type { Nullable } from "@babylonjs/core/types";
export declare const defaultViewport: {
    x: number;
    y: number;
    scale: number;
};
export declare class ViewportMaterialPlugin extends MaterialPluginBase {
    viewport: {
        x: number;
        y: number;
        scale: number;
    };
    get isEnabled(): boolean;
    set isEnabled(enabled: boolean);
    private _isEnabled;
    constructor(material: Material);
    prepareDefines(defines: MaterialDefines): void;
    getUniforms(): {
        ubo?: Array<{
            name: string;
            size?: number;
            type?: string;
            arraySize?: number;
        }>;
        vertex?: string;
        fragment?: string;
    };
    bindForSubMesh(uniformBuffer: UniformBuffer): void;
    getClassName(): string;
    getCustomCode(shaderType: string): Nullable<{
        [pointName: string]: string;
    }>;
}
