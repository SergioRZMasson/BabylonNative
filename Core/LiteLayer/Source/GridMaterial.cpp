/*
 * Original material/grid/grid-material.ts, Lite 1.32.0 (2e064d88), Apache-2.0.
 */
#include "LiteInternal.h"

static bool append(bl_Runtime* r, L_Text* text, const char* value)
{
    return l_appendText(r, text, {value, strlen(value)});
}

bl_Status bl_createGridMaterial(bl_Runtime* r, const bl_GridMaterialOptions* o,
                                bl_ShaderMaterial* out)
{
    L_TRY(l_check(r));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_GridMaterialOptions defaults = {};
    if (!o)
    {
        o = &defaults;
    }
    const bl_OptionalBool booleans[4] = {o->antialias, o->preMultiplyAlpha, o->useMaxLine,
                                         o->backFaceCulling};
    for (unsigned i = 0; i < 4; ++i)
    {
        if ((unsigned)booleans[i] > BL_BOOL_TRUE)
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    double opacity = o->opacity.present ? o->opacity.value : 1;
    double visibility = o->visibility.present ? o->visibility.value : 1;
    double major = o->majorUnitFrequency.present ? o->majorUnitFrequency.value : 10;
    double control[4] = {o->gridRatio.present ? o->gridRatio.value : 1, floor(major + .5),
                         o->minorUnitVisibility.present ? o->minorUnitVisibility.value : .33,
                         opacity};
    if (control[1] == 0 && major < 0)
    {
        control[1] = -0.0;
    }
    bl_Vec3 main = o->hasMainColor ? o->mainColor : bl_Vec3{};
    bl_Vec3 line = o->hasLineColor ? o->lineColor : bl_Vec3{0, .5, .5};
    bl_Vec3 offset = o->hasGridOffset ? o->gridOffset : bl_Vec3{};
    double mainValues[3] = {main.x, main.y, main.z};
    double lineValues[3] = {line.x, line.y, line.z};
    double offsetValues[3] = {offset.x, offset.y, offset.z};
    bool texture = !L_NULL(o->opacityTexture);
    if (texture)
    {
        if (o->opacityTexture._runtime != r)
        {
            return BL_WRONG_RUNTIME;
        }
        L_GET(o->opacityTexture, L_TEXTURE, L_Texture, checked);
        (void)checked;
    }
    const char* output =
        texture ? "struct VertexOutput{@builtin(position) position:vec4<f32>,"
                  "@location(0) vPosition:vec3<f32>,@location(1) vNormal:vec3<f32>,"
                  "@location(2) vUv:vec2<f32>,};\n"
                : "struct VertexOutput{@builtin(position) position:vec4<f32>,"
                  "@location(0) vPosition:vec3<f32>,@location(1) vNormal:vec3<f32>,};\n";
    L_Text vertex = {};
    L_Text fragment = {};
    bool ok = append(r, &vertex, output) &&
              append(r, &vertex,
                     "@vertex fn mainVertex(input:VertexInput)->VertexOutput{var out:VertexOutput;"
                     "out.position=shaderSystem.projection*(shaderSystem.view*"
                     "(shaderSystem.world*vec4<f32>(input.position,1.0)));"
                     "out.vPosition=input.position;out.vNormal=input.normal;");
    ok = ok && (!texture || append(r, &vertex, "out.vUv=input.uv;")) &&
         append(r, &vertex, "return out;}");
    ok = ok && append(r, &fragment, output) &&
         append(r, &fragment,
                "const SQRT2:f32=1.41421356;\nconst PI:f32=3.14159;\n"
                "fn gridDynamicVisibility(position:f32)->f32{let f=shaderUniforms.gridControl.y;"
                "if(floor(position+0.5)==floor(position/f+0.5)*f){return 1.0;}"
                "return shaderUniforms.gridControl.z;}\n"
                "fn gridAniso(d:f32)->f32{return clamp(1.0/(d+1.0)-1.0/10.0,0.0,1.0);}\n"
                "fn gridIsOnLine(position:f32,d:f32)->f32{var fr=position-floor(position+0.5);"
                "fr=fr/d;");
    ok = ok && append(r, &fragment,
                      o->antialias == BL_BOOL_FALSE
                          ? "if(abs(fr)<SQRT2/4.0){return 1.0;}return 0.0;}\n"
                          : "fr=clamp(fr,-1.0,1.0);return 0.5+0.5*cos(fr*PI);}\n");
    ok = ok && append(r, &fragment,
                      "fn gridContrib(position:f32)->f32{var d=length(vec2<f32>(dpdx(position),"
                      "dpdy(position)));d=d*SQRT2;var r=gridIsOnLine(position,d);"
                      "r=r*gridDynamicVisibility(position);r=r*gridAniso(d);return r;}\n"
                      "fn gridNormalImpact(x:f32)->f32{return clamp(1.0-3.0*abs(x*x*x),0.0,1.0);}\n"
                      "@fragment fn mainFragment(input:VertexOutput)->@location(0) vec4<f32>{"
                      "let gridRatio=shaderUniforms.gridControl.x;"
                      "let gridPos=(input.vPosition+shaderUniforms.gridOffset)/gridRatio;"
                      "var x=gridContrib(gridPos.x);var y=gridContrib(gridPos.y);"
                      "var z=gridContrib(gridPos.z);let n=normalize(input.vNormal);"
                      "x=x*gridNormalImpact(n.x);y=y*gridNormalImpact(n.y);"
                      "z=z*gridNormalImpact(n.z);");
    ok = ok && append(r, &fragment,
                      o->useMaxLine == BL_BOOL_TRUE ? "let grid=clamp(max(max(x,y),z),0.0,1.0);"
                                                    : "let grid=clamp(x+y+z,0.0,1.0);");
    ok = ok && append(r, &fragment,
                      "var rgb=mix(shaderUniforms.mainColor,shaderUniforms.lineColor,"
                      "vec3<f32>(grid));var opacity=1.0;");
    if (opacity < 1)
    {
        ok = ok &&
             append(r, &fragment, "opacity=clamp(grid,0.08,shaderUniforms.gridControl.w*grid);");
    }
    if (texture)
    {
        ok = ok && append(r, &fragment,
                          "opacity=opacity*textureSample(opacitySampler,"
                          "opacitySamplerSampler,input.vUv).a;");
    }
    if (opacity < 1 && o->preMultiplyAlpha == BL_BOOL_TRUE)
    {
        ok = ok && append(r, &fragment, "rgb=rgb*opacity;");
    }
    ok = ok && append(r, &fragment, "return vec4<f32>(rgb,opacity*shaderUniforms.visibility);}");
    bl_Status status = BL_OUT_OF_MEMORY;
    if (ok)
    {
        const bl_VertexSemantic attributes[3] = {BL_ATTRIBUTE_POSITION, BL_ATTRIBUTE_NORMAL,
                                                 BL_ATTRIBUTE_UV};
        const bl_ShaderUniformDecl uniforms[8] = {
            {{"world", 5}, BL_UNIFORM_MAT4, {}, true},
            {{"view", 4}, BL_UNIFORM_MAT4, {}, true},
            {{"projection", 10}, BL_UNIFORM_MAT4, {}, true},
            {{"gridControl", 11}, BL_UNIFORM_VEC4, {control, 4}, false},
            {{"mainColor", 9}, BL_UNIFORM_VEC3, {mainValues, 3}, false},
            {{"lineColor", 9}, BL_UNIFORM_VEC3, {lineValues, 3}, false},
            {{"gridOffset", 10}, BL_UNIFORM_VEC3, {offsetValues, 3}, false},
            {{"visibility", 10}, BL_UNIFORM_F32, {&visibility, 1}, false}};
        const bl_ShaderSamplerDecl sampler = {{"opacitySampler", 14}};
        bl_ShaderMaterialOptions options = {};
        options.name = o->name.data ? o->name : bl_String{"GridMaterial", 12};
        options.vertexSource = {vertex.data, vertex.length};
        options.fragmentSource = {fragment.data, fragment.length};
        options.attributes = attributes;
        options.attributeCount = texture ? 3 : 2;
        options.uniforms = uniforms;
        options.uniformCount = 8;
        options.samplers = texture ? &sampler : NULL;
        options.samplerCount = texture ? 1 : 0;
        options.needAlphaBlending = opacity < 1 || texture ? BL_BOOL_TRUE : BL_BOOL_FALSE;
        options.backFaceCulling = o->backFaceCulling;
        bl_ShaderMaterial material;
        status = bl_createShaderMaterial(r, &options, &material);
        if (status == BL_OK && texture)
        {
            status = bl_setShaderTexture(material, {"opacitySampler", 14}, o->opacityTexture);
            if (status != BL_OK)
            {
                bl_disposeShaderMaterial(material);
            }
        }
        if (status == BL_OK)
        {
            *out = material;
        }
    }
    l_free(r, vertex.data);
    l_free(r, fragment.data);
    return status;
}
