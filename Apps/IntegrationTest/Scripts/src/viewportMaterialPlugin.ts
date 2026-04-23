import type { Material } from "@babylonjs/core/Materials/material";
import type { MaterialDefines } from "@babylonjs/core/Materials/materialDefines";
import { MaterialPluginBase } from "@babylonjs/core/Materials/materialPluginBase";
import type { UniformBuffer } from "@babylonjs/core/Materials/uniformBuffer";
import type { Nullable } from "@babylonjs/core/types";

export const defaultViewport = { x: 0, y: 0, scale: 1 };

const vertexDeclaration = `
#ifdef MP_VIEWPORT
  uniform vec4 viewport_xyz;
#endif
`;

const vertexCode = `
#ifdef MP_VIEWPORT
    float viewport_x = viewport_xyz.x;
    float viewport_y = viewport_xyz.y;
    float viewport_scale = viewport_xyz.z;
    gl_Position.x = gl_Position.x * viewport_scale + (viewport_x * gl_Position.w);
    gl_Position.y = gl_Position.y * viewport_scale + (viewport_y * gl_Position.w);
#endif
`;

export class ViewportMaterialPlugin extends MaterialPluginBase {
  public viewport = defaultViewport;

  public get isEnabled(): boolean {
    return this._isEnabled;
  }

  public set isEnabled(enabled: boolean) {
    if (this._isEnabled === enabled) {
      return;
    }
    this._isEnabled = enabled;
    this.markAllDefinesAsDirty();
    this._enable(this._isEnabled);
  }

  private _isEnabled = true;

  constructor(material: Material) {
    super(material, "Viewport", 200, { MP_VIEWPORT: false });
    this._enable(this._isEnabled);
  }

  public prepareDefines(defines: MaterialDefines): void {
    defines.MP_VIEWPORT = this._isEnabled;
  }

  public getUniforms(): {
    ubo?: Array<{
      name: string;
      size?: number;
      type?: string;
      arraySize?: number;
    }>;
    vertex?: string;
    fragment?: string;
  } {
    return {
      ubo: [{ name: "viewport_xyz", size: 3, type: "vec3" }],
      vertex: vertexDeclaration,
    };
  }

  public bindForSubMesh(uniformBuffer: UniformBuffer): void {
    if (this._isEnabled) {
      uniformBuffer.updateFloat3(
        "viewport_xyz",
        this.viewport.x,
        this.viewport.y,
        this.viewport.scale
      );
    }
  }

  public getClassName(): string {
    return "ViewportMaterialPlugin";
  }

  public getCustomCode(shaderType: string): Nullable<{
    [pointName: string]: string;
  }> {
    return shaderType === "vertex"
      ? { CUSTOM_VERTEX_MAIN_END: vertexCode }
      : null;
  }
}
