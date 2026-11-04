/*
 * Original material/line/line-material.ts at Lite 1.32.0, 2e064d88.
 * WGSL is original authored source, composed natively (Apache-2.0).
 */
#include "LiteInternal.h"

static bool lineBool(bl_OptionalBool value)
{
    return value == BL_BOOL_DEFAULT || value == BL_BOOL_FALSE || value == BL_BOOL_TRUE;
}

static bool append(bl_Runtime* r, L_Text* text, const char* value)
{
    return l_appendText(r, text, {value, strlen(value)});
}

bl_Status bl_createLineMaterial(bl_Runtime* r, const bl_LineMaterialOptions* o,
                                bl_ShaderMaterial* out)
{
    L_TRY(l_check(r));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_LineMaterialOptions defaults = {};
    if (!o)
    {
        o = &defaults;
    }
    if (!lineBool(o->useVertexColor) || !lineBool(o->useVertexAlpha) ||
        !lineBool(o->useThinInstances) || !lineBool(o->useThinInstanceColors))
    {
        return BL_INVALID_ARGUMENT;
    }
    bool vertexColor = o->useVertexColor == BL_BOOL_TRUE;
    bool thin = o->useThinInstances == BL_BOOL_TRUE;
    bool instanceColor = o->useThinInstanceColors == BL_BOOL_TRUE;
    bool hasColor = vertexColor || instanceColor;
    if (instanceColor && !thin)
    {
        return l_error(r, BL_INVALID_ARGUMENT, __func__,
                       "Thin-instance colours require thin instances", 262);
    }
    bl_Color4 color = o->hasColor ? o->color : bl_Color4{1, 1, 1, 1};
    double colors[4] = {color.r, color.g, color.b, color.a};
    L_Text vertex = {};
    L_Text fragment = {};
    const char* output = hasColor
                             ? "struct VertexOutput{@builtin(position) position:vec4<f32>,"
                               "@location(0) color:vec4<f32>,};\n"
                             : "struct VertexOutput{@builtin(position) position:vec4<f32>,};\n";
    bool ok = append(r, &vertex, output) &&
              append(r, &vertex,
                     "@vertex fn mainVertex(input:VertexInput)->VertexOutput{"
                     "var out:VertexOutput;");
    ok = ok && append(r, &vertex,
                      thin ? "let instanceWorld=mat4x4<f32>(input.world0,input.world1,"
                             "input.world2,input.world3);"
                             "let finalWorld=shaderSystem.world*instanceWorld;"
                           : "let finalWorld=shaderSystem.world;");
    ok = ok && append(r, &vertex,
                      "out.position=shaderSystem.viewProjection*finalWorld*"
                      "vec4<f32>(input.position,1.0);");
    if (vertexColor || instanceColor)
    {
        ok =
            ok && append(r, &vertex,
                         vertexColor && instanceColor ? "out.color=input.color*input.instanceColor;"
                         : vertexColor                ? "out.color=input.color;"
                                                      : "out.color=input.instanceColor;");
    }
    ok = ok && append(r, &vertex, "return out;}");
    ok = ok && append(r, &fragment, output) &&
         append(r, &fragment,
                "@fragment fn mainFragment(input:VertexOutput)->@location(0) vec4<f32>{return ") &&
         append(r, &fragment, hasColor ? "input.color;}" : "shaderUniforms.lineColor;}");
    bl_Status status = BL_OUT_OF_MEMORY;
    if (ok)
    {
        const bl_VertexSemantic attributes[2] = {BL_ATTRIBUTE_POSITION, BL_ATTRIBUTE_COLOR};
        const bl_ShaderUniformDecl uniforms[3] = {
            {{"world", 5}, BL_UNIFORM_MAT4, {}, true},
            {{"viewProjection", 14}, BL_UNIFORM_MAT4, {}, true},
            {{"lineColor", 9}, BL_UNIFORM_VEC4, {colors, 4}, false}};
        bl_ShaderMaterialOptions options = {};
        options.name = o->name.data ? o->name : bl_String{"LineMaterial", 12};
        options.vertexSource = {vertex.data, vertex.length};
        options.fragmentSource = {fragment.data, fragment.length};
        options.attributes = attributes;
        options.attributeCount = vertexColor ? 2 : 1;
        options.uniforms = uniforms;
        options.uniformCount = hasColor ? 2 : 3;
        options.needAlphaBlending =
            o->useVertexAlpha == BL_BOOL_FALSE ? BL_BOOL_FALSE : BL_BOOL_TRUE;
        options.backFaceCulling = BL_BOOL_FALSE;
        options.depthWrite = o->depthWrite;
        options.depthCompare = o->depthCompare;
        options.topology = BL_TOPOLOGY_LINE_LIST;
        bl_ShaderMaterial material;
        status = bl_createShaderMaterial(r, &options, &material);
        if (status == BL_OK)
        {
            L_Material* record = (L_Material*)l_peek(r, material._id);
            record->lineMaterial = true;
            record->lineVertexColor = vertexColor;
            record->lineThinRequired = thin;
            record->thinColorMode = instanceColor ? 2 : 1;
            *out = material;
        }
    }
    l_free(r, vertex.data);
    l_free(r, fragment.data);
    return status;
}
