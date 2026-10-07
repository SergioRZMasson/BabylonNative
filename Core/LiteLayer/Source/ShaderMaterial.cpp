#include "MaterialInternal.h"
#include <stdarg.h>
#include <stdio.h>

static const uint32_t uniformAlign[7] = {4, 4, 4, 8, 16, 16, 16};
static const char* uniformTypes[7] = {"f32",       "u32",       "i32",        "vec2<f32>",
                                      "vec3<f32>", "vec4<f32>", "mat4x4<f32>"};

static bool systemType(bl_String name, bl_ShaderUniformType* type)
{
    if (l_name(name, "cameraPosition"))
    {
        *type = BL_UNIFORM_VEC3;
    }
    else if (l_name(name, "screenSize"))
    {
        *type = BL_UNIFORM_VEC2;
    }
    else if (l_name(name, "alphaCutoff"))
    {
        *type = BL_UNIFORM_F32;
    }
    else if (l_name(name, "world") || l_name(name, "view") || l_name(name, "projection") ||
             l_name(name, "viewProjection") || l_name(name, "worldView") ||
             l_name(name, "worldViewProjection"))
    {
        *type = BL_UNIFORM_MAT4;
    }
    else
    {
        return false;
    }
    return true;
}

size_t l_uniformSlot(L_Material* m, bl_String name)
{
    for (size_t i = 0; i < m->uniformCount; ++i)
    {
        if (l_equal(m->uniforms[i].name, name))
        {
            return i;
        }
    }
    return SIZE_MAX;
}

size_t l_samplerSlot(L_Material* m, bl_String name)
{
    for (size_t i = 0; i < m->samplerCount; ++i)
    {
        if (l_equal(m->samplers[i].name, name))
        {
            return i;
        }
    }
    return SIZE_MAX;
}

bool l_samplerSuffix(bl_String a, bl_String b)
{
    return a.length == b.length + 7 && !memcmp(a.data, b.data, b.length) &&
           !memcmp(a.data + b.length, "Sampler", 7);
}

static bool boolOption(bl_OptionalBool o)
{
    return o >= BL_BOOL_DEFAULT && o <= BL_BOOL_TRUE;
}

bool l_appendText(bl_Runtime* r, L_Text* t, bl_String s)
{
    if (s.length > SIZE_MAX - t->length - 1)
    {
        return false;
    }
    size_t need = t->length + s.length + 1;
    if (need > t->capacity)
    {
        size_t cap = t->capacity ? t->capacity : 512;
        while (cap < need)
        {
            if (cap > SIZE_MAX / 2)
            {
                cap = need;
                break;
            }
            cap *= 2;
        }
        char* p = (char*)l_alloc(r, cap);
        if (!p)
        {
            return false;
        }
        if (t->length)
        {
            memcpy(p, t->data, t->length);
        }
        l_free(r, t->data);
        t->data = p;
        t->capacity = cap;
    }
    if (s.length)
    {
        memcpy(t->data + t->length, s.data, s.length);
    }
    t->length += s.length;
    t->data[t->length] = 0;
    return true;
}

static bool text(bl_Runtime* r, L_Text* t, const char* s)
{
    return l_appendText(r, t, {s, strlen(s)});
}

static bool format(bl_Runtime* r, L_Text* t, const char* fmt, ...)
{
    char p[128];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(p, sizeof(p), fmt, args);
    va_end(args);
    return n >= 0 && (size_t)n < sizeof(p) && l_appendText(r, t, {p, (size_t)n});
}

static bool prelude(bl_Runtime* r, L_Material* m)
{
    L_Text t = {};
    bool ok = text(r, &t, "struct ShaderSystemUniforms {\n");
    size_t systemCount = 0;
    size_t customCount = 0;
    uint32_t offsets[2] = {};
    for (size_t i = 0; ok && i < m->uniformCount; ++i)
    {
        if (m->uniforms[i].system)
        {
            L_Uniform* u = m->uniforms + i;
            uint32_t alignment = uniformAlign[u->type];
            offsets[0] = (offsets[0] + alignment - 1) & ~(alignment - 1);
            u->offset = offsets[0];
            offsets[0] += (uint32_t)uniformCounts[u->type] * 4;
            ok = l_appendText(r, &t, u->name) && format(r, &t, ": %s,\n", uniformTypes[u->type]);
            ++systemCount;
        }
    }
    if (!systemCount)
    {
        ok = ok && text(r, &t, "_pad: vec4<f32>,\n");
    }
    ok = ok &&
         text(r, &t, "}\n@group(1) @binding(0) var<uniform> shaderSystem: ShaderSystemUniforms;\n");
    for (size_t i = 0; i < m->uniformCount; ++i)
    {
        if (!m->uniforms[i].system)
        {
            ++customCount;
        }
    }
    if (customCount)
    {
        ok = ok && text(r, &t, "struct ShaderUniforms {\n");
        for (size_t i = 0; ok && i < m->uniformCount; ++i)
        {
            if (!m->uniforms[i].system)
            {
                L_Uniform* u = m->uniforms + i;
                uint32_t alignment = uniformAlign[u->type];
                offsets[1] = (offsets[1] + alignment - 1) & ~(alignment - 1);
                u->offset = offsets[1];
                offsets[1] += (uint32_t)uniformCounts[u->type] * 4;
                ok =
                    l_appendText(r, &t, u->name) && format(r, &t, ": %s,\n", uniformTypes[u->type]);
            }
        }
        ok = ok &&
             text(r, &t, "}\n@group(1) @binding(1) var<uniform> shaderUniforms: ShaderUniforms;\n");
    }
    uint32_t binding = customCount ? 2 : 1;
    for (size_t i = 0; ok && i < m->samplerCount; ++i)
    {
        ok = format(r, &t, "@group(1) @binding(%u) var ", binding++) &&
             l_appendText(r, &t, m->samplers[i].name) && text(r, &t, ": texture_2d<f32>;\n") &&
             format(r, &t, "@group(1) @binding(%u) var ", binding++) &&
             l_appendText(r, &t, m->samplers[i].name) && text(r, &t, "Sampler: sampler;\n");
    }
    for (size_t i = 0; ok && i < m->defineCount; ++i)
    {
        bl_ShaderDefine* d = m->defines + i;
        ok = text(r, &t, "const ") && l_appendText(r, &t, d->name);
        if (d->isBoolean)
        {
            ok = ok && format(r, &t, ": bool = %s;\n", d->booleanValue ? "true" : "false");
        }
        else
        {
            ok = ok && format(r, &t, ": f32 = %.17e;\n", d->numberValue);
        }
    }
    ok = ok && text(r, &t, "struct VertexInput {\n");
    for (size_t i = 0; ok && i < m->attributeCount; ++i)
    {
        unsigned a = (unsigned)m->attributes[i];
        ok = format(r, &t, "@location(%u) %s: vec%u<f32>,\n", (unsigned)i, attributeNames[a],
                    attributeComponents[a]);
    }
    ok = ok && text(r, &t, "}\n");
    if (!ok)
    {
        l_free(r, t.data);
        return false;
    }
    m->prelude = {t.data, t.length};
    return true;
}

void l_materialCleanup(bl_Runtime* r, L_Record* record)
{
    L_Material* m = (L_Material*)record;
    if (m->graphicsCleanup)
    {
        m->graphicsCleanup(r, m);
    }
    for (size_t i = 0; i < m->uniformCount; ++i)
    {
        l_free(r, (void*)m->uniforms[i].name.data);
    }
    for (size_t i = 0; i < m->samplerCount; ++i)
    {
        if (!L_NULL(m->samplers[i].texture))
        {
            L_Texture* t = (L_Texture*)l_peek(r, m->samplers[i].texture._id);
            if (t && t->references)
            {
                --t->references;
            }
        }
        l_free(r, (void*)m->samplers[i].name.data);
    }
    for (size_t i = 0; i < m->defineCount; ++i)
    {
        l_free(r, (void*)m->defines[i].name.data);
    }
    l_free(r, (void*)m->name.data);
    l_free(r, (void*)m->vertex.data);
    l_free(r, (void*)m->fragment.data);
    l_free(r, (void*)m->prelude.data);
    l_free(r, m->attributes);
    l_free(r, m->uniforms);
    l_free(r, m->samplers);
    l_free(r, m->defines);
    m->uniforms = NULL;
    m->samplers = NULL;
    m->defines = NULL;
    m->uniformCount = m->samplerCount = m->defineCount = 0;
}

bl_Status bl_createShaderMaterial(bl_Runtime* r, const bl_ShaderMaterialOptions* o,
                                  bl_ShaderMaterial* out)
{
    L_TRY(l_check(r));
    if (o && (!o->vertexSource.length || !o->fragmentSource.length))
    {
        return l_error(r, BL_INVALID_ARGUMENT, __func__, "Shader sources required", 389);
    }
    if (!o || !out || !l_string(o->name) || !l_string(o->vertexSource) || !o->vertexSource.length ||
        !l_string(o->fragmentSource) || !o->fragmentSource.length ||
        !l_typedSpan(o->attributes, o->attributeCount, alignof(bl_VertexSemantic)) ||
        !l_typedSpan(o->uniforms, o->uniformCount, alignof(bl_ShaderUniformDecl)) ||
        !l_typedSpan(o->samplers, o->samplerCount, alignof(bl_ShaderSamplerDecl)) ||
        !l_typedSpan(o->defines, o->defineCount, alignof(bl_ShaderDefine)) ||
        !boolOption(o->needAlphaBlending) || !boolOption(o->needAlphaTesting) ||
        !boolOption(o->backFaceCulling) || !boolOption(o->depthWrite) ||
        (unsigned)o->blendMode > BL_BLEND_ADDITIVE ||
        (unsigned)o->depthCompare > BL_COMPARE_ALWAYS ||
        (unsigned)o->topology > BL_TOPOLOGY_POINT_LIST ||
        (o->depthBias.present && !isfinite(o->depthBias.value)) ||
        (o->depthBiasSlopeScale.present && !isfinite(o->depthBiasSlopeScale.value)) ||
        o->attributeCount > 6 || o->uniformCount > UINT32_MAX / 64 || o->samplerCount > 128 ||
        o->defineCount > UINT32_MAX / sizeof(bl_ShaderDefine))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Invalid material declaration");
    }
    unsigned seen = 0;
    for (size_t i = 0; i < o->attributeCount; ++i)
    {
        unsigned a = (unsigned)o->attributes[i];
        if (a >= 6)
        {
            return l_error(r, BL_INVALID_ARGUMENT, __func__, "Unknown vertex attribute", 391);
        }
        if (seen & (1u << a))
        {
            return l_error(r, BL_INVALID_ARGUMENT, __func__, "Duplicate vertex attribute", 392);
        }
        seen |= 1u << a;
    }
    if (!(seen & 1))
    {
        return l_error(r, BL_INVALID_ARGUMENT, __func__, "Position attribute required", 393);
    }
    for (size_t i = 0; i < o->uniformCount; ++i)
    {
        const bl_ShaderUniformDecl* u = o->uniforms + i;
        bl_ShaderUniformType type = u->type;
        if (!l_identifier(u->name))
        {
            return l_error(r, BL_INVALID_ARGUMENT, __func__, "Invalid WGSL uniform identifier",
                           388);
        }
        if (u->system && !systemType(u->name, &type))
        {
            return l_error(r, BL_INVALID_ARGUMENT, __func__, "Unknown system uniform", 396);
        }
        if ((unsigned)type > BL_UNIFORM_MAT4)
        {
            return l_error(r, BL_INVALID_ARGUMENT, __func__, "Unknown uniform type", 397);
        }
        if (!l_typedSpan(u->defaultValue.data, u->defaultValue.count, alignof(double)) ||
            (u->defaultValue.count && u->defaultValue.count != uniformCounts[type]))
        {
            return l_error(r, BL_INVALID_ARGUMENT, __func__, "Uniform value count mismatch", 399);
        }
        for (size_t j = 0; j < u->defaultValue.count; ++j)
        {
            if (!isfinite(u->defaultValue.data[j]))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        for (size_t j = 0; j < i; ++j)
        {
            if (l_equal(u->name, o->uniforms[j].name))
            {
                return l_error(r, BL_INVALID_ARGUMENT, __func__, "Duplicate uniform", 398);
            }
        }
    }
    for (size_t i = 0; i < o->samplerCount; ++i)
    {
        bl_String n = o->samplers[i].name;
        if (!l_identifier(n))
        {
            return BL_INVALID_ARGUMENT;
        }
        for (size_t j = 0; j < o->uniformCount; ++j)
        {
            if (l_equal(n, o->uniforms[j].name) || l_samplerSuffix(o->uniforms[j].name, n))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        for (size_t j = 0; j < i; ++j)
        {
            if (l_equal(n, o->samplers[j].name) || l_samplerSuffix(n, o->samplers[j].name) ||
                l_samplerSuffix(o->samplers[j].name, n))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    for (size_t i = 0; i < o->defineCount; ++i)
    {
        bl_String n = o->defines[i].name;
        if (!l_identifier(n) || (!o->defines[i].isBoolean && !isfinite(o->defines[i].numberValue)))
        {
            return BL_INVALID_ARGUMENT;
        }
        for (size_t j = 0; j < i; ++j)
        {
            if (l_equal(n, o->defines[j].name))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        for (size_t j = 0; j < o->uniformCount; ++j)
        {
            if (l_equal(n, o->uniforms[j].name))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        for (size_t j = 0; j < o->samplerCount; ++j)
        {
            if (l_equal(n, o->samplers[j].name) || l_samplerSuffix(n, o->samplers[j].name))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    L_NEW(r, L_MATERIAL, 20, l_materialCleanup, L_Material, m);
    m->programIndex = BL_INVALID_BGFX_HANDLE;
    m->attributes = (bl_VertexSemantic*)l_alloc(r, o->attributeCount * sizeof(*m->attributes));
    m->uniforms = (L_Uniform*)l_alloc(r, o->uniformCount * sizeof(*m->uniforms));
    m->samplers = (L_Sampler*)l_alloc(r, o->samplerCount * sizeof(*m->samplers));
    m->defines = (bl_ShaderDefine*)l_alloc(r, o->defineCount * sizeof(*m->defines));
    bool ok = m->attributes && (!o->uniformCount || m->uniforms) &&
              (!o->samplerCount || m->samplers) && (!o->defineCount || m->defines);
    m->name = l_copyString(r, o->name);
    m->vertex = l_copyString(r, o->vertexSource);
    m->fragment = l_copyString(r, o->fragmentSource);
    ok = ok && m->name.data && m->vertex.data && m->fragment.data;
    if (ok)
    {
        memcpy(m->attributes, o->attributes, o->attributeCount * sizeof(*m->attributes));
        m->attributeCount = o->attributeCount;
        for (size_t i = 0; i < o->uniformCount; ++i)
        {
            L_Uniform* u = m->uniforms + i;
            const bl_ShaderUniformDecl* d = o->uniforms + i;
            u->name = l_copyString(r, d->name);
            u->type = d->type;
            u->system = d->system;
            if (u->system)
            {
                systemType(u->name, &u->type);
            }
            ++m->uniformCount;
            if (!u->name.data)
            {
                ok = false;
                break;
            }
            if (!d->defaultValue.count && l_name(u->name, "alphaCutoff"))
            {
                u->values[0] = .4f;
            }
            for (size_t j = 0; j < d->defaultValue.count; ++j)
            {
                u->values[j] = (float)d->defaultValue.data[j];
            }
        }
        for (size_t i = 0; ok && i < o->samplerCount; ++i)
        {
            m->samplers[i].name = l_copyString(r, o->samplers[i].name);
            ++m->samplerCount;
            ok = m->samplers[i].name.data != NULL;
        }
        for (size_t i = 0; ok && i < o->defineCount; ++i)
        {
            m->defines[i] = o->defines[i];
            m->defines[i].name = l_copyString(r, o->defines[i].name);
            ++m->defineCount;
            ok = m->defines[i].name.data != NULL;
        }
        for (size_t i = 1; ok && i < m->defineCount; ++i)
        {
            bl_ShaderDefine v = m->defines[i];
            size_t j = i;
            while (j && strcmp(m->defines[j - 1].name.data, v.name.data) > 0)
            {
                m->defines[j] = m->defines[j - 1];
                --j;
            }
            m->defines[j] = v;
        }
    }
    m->blending = o->needAlphaBlending == BL_BOOL_TRUE;
    m->testing = o->needAlphaTesting == BL_BOOL_TRUE;
    m->culling = o->backFaceCulling != BL_BOOL_FALSE;
    m->depthWrite = o->depthWrite == BL_BOOL_DEFAULT ? !m->blending : o->depthWrite == BL_BOOL_TRUE;
    m->blend = o->blendMode;
    m->depthCompare =
        o->depthCompare == BL_COMPARE_DEFAULT ? BL_COMPARE_GREATER_EQUAL : o->depthCompare;
    m->topology = o->topology;
    m->bias = o->depthBias.present ? o->depthBias.value : 0;
    m->slope = o->depthBiasSlopeScale.present ? o->depthBiasSlopeScale.value : 0;
    if (ok)
    {
        ok = prelude(r, m);
    }
    if (!ok)
    {
        l_materialCleanup(r, &m->record);
        m->record.disposed = true;
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Material allocation failed");
    }
    *out = {r, m->record.id};
    return BL_OK;
}

static bl_Status setUniform(bl_ShaderMaterial h, bl_String name, const void* p, size_t count,
                            bool f32)
{
    L_GET(h, L_MATERIAL, L_Material, m);
    if (!l_string(name) || !l_typedSpan(p, count, f32 ? alignof(float) : alignof(double)))
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t i = l_uniformSlot(m, name);
    if (i == SIZE_MAX)
    {
        return l_error(h._runtime, BL_INVALID_ARGUMENT, __func__, "Unknown uniform", 400);
    }
    L_Uniform* u = m->uniforms + i;
    if (count != uniformCounts[u->type])
    {
        return l_error(h._runtime, BL_INVALID_ARGUMENT, __func__, "Uniform value count mismatch",
                       399);
    }
    float temp[16];
    for (size_t j = 0; j < count; ++j)
    {
        double v = f32 ? ((const float*)p)[j] : ((const double*)p)[j];
        if (!isfinite(v))
        {
            return BL_INVALID_ARGUMENT;
        }
        temp[j] = (float)v;
    }
    memcpy(u->values, temp, count * sizeof(float));
    return BL_OK;
}

bl_Status bl_setShaderUniform(bl_ShaderMaterial h, bl_String n, bl_NumberSpan v)
{
    return setUniform(h, n, v.data, v.count, false);
}

bl_Status bl_setShaderUniformF32(bl_ShaderMaterial h, bl_String n, bl_F32Span v)
{
    return setUniform(h, n, v.data, v.count, true);
}

bl_Status bl_getShaderUniform(bl_ShaderMaterial h, bl_String name, bl_ShaderUniformView* out)
{
    L_GET(h, L_MATERIAL, L_Material, m);
    if (!out || !l_string(name))
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t i = l_uniformSlot(m, name);
    if (i == SIZE_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {m->uniforms[i].type, {m->uniforms[i].values, uniformCounts[m->uniforms[i].type]}};
    return BL_OK;
}

bl_Status bl_setShaderFloat(bl_ShaderMaterial h, bl_String n, double v)
{
    return bl_setShaderUniform(h, n, {&v, 1});
}

bl_Status bl_setShaderVector3(bl_ShaderMaterial h, bl_String n, bl_Vec3 v)
{
    double a[3] = {v.x, v.y, v.z};
    return bl_setShaderUniform(h, n, {a, 3});
}

bl_Status bl_setShaderMatrix(bl_ShaderMaterial h, bl_String n, const bl_Mat4* v)
{
    if (!v)
    {
        return BL_INVALID_ARGUMENT;
    }
    return bl_setShaderUniform(h, n, {v->values, 16});
}

bl_Status bl_setShaderMatrixF32(bl_ShaderMaterial h, bl_String n, bl_F32Span v)
{
    if (v.count != 16)
    {
        return BL_INVALID_ARGUMENT;
    }
    return bl_setShaderUniformF32(h, n, v);
}

bl_Status bl_setShaderTexture(bl_ShaderMaterial h, bl_String name, bl_Texture2D texture)
{
    L_GET(h, L_MATERIAL, L_Material, m);
    if (!l_string(name))
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t slot = l_samplerSlot(m, name);
    if (slot == SIZE_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    L_Texture* next = NULL;
    if (!L_NULL(texture))
    {
        if (texture._runtime != h._runtime)
        {
            return BL_WRONG_RUNTIME;
        }
        L_Record* record;
        L_TRY(l_get(h._runtime, texture._id, L_TEXTURE, &record));
        next = (L_Texture*)record;
        if (m->engine && next->engine != m->engine)
        {
            return BL_WRONG_ENGINE;
        }
    }
    bl_Texture2D old = m->samplers[slot].texture;
    if (old._id == texture._id)
    {
        return BL_OK;
    }
    if (!L_NULL(old))
    {
        L_Texture* previous = (L_Texture*)l_peek(h._runtime, old._id);
        if (previous && previous->references)
        {
            --previous->references;
        }
    }
    if (next)
    {
        ++next->references;
    }
    m->samplers[slot].texture = texture;
    return BL_OK;
}

bl_Status bl_getShaderTexture(bl_ShaderMaterial h, bl_String name, bl_Texture2D* out)
{
    L_GET(h, L_MATERIAL, L_Material, m);
    if (!out || !l_string(name))
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t slot = l_samplerSlot(m, name);
    if (slot == SIZE_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = m->samplers[slot].texture;
    return BL_OK;
}

bl_Status bl_disposeShaderMaterial(bl_ShaderMaterial h)
{
    L_RETIRED(h, L_MATERIAL, L_Material, m);
    if (!m || m->record.disposed)
    {
        return BL_OK;
    }
    if (m->meshReferences)
    {
        return BL_BUSY;
    }
    l_materialCleanup(h._runtime, &m->record);
    m->record.disposed = true;
    return BL_OK;
}
