#include "MaterialInternal.h"
#include <stdio.h>

static const char* standardDeclarations = R"WGSL(
struct SceneUniforms {
    viewProjection: mat4x4<f32>, view: mat4x4<f32>, vEyePosition: vec4<f32>,
    envRotationY: f32, _envPad0: f32, _envPad1: f32, _envPad2: f32,
    vSphericalL00: vec4<f32>, vSphericalL1_1: vec4<f32>, vSphericalL10: vec4<f32>,
    vSphericalL11: vec4<f32>, vSphericalL2_2: vec4<f32>, vSphericalL2_1: vec4<f32>,
    vSphericalL20: vec4<f32>, vSphericalL21: vec4<f32>, vSphericalL22: vec4<f32>,
    vImageInfos: vec4<f32>, vFogInfos: vec4<f32>, vFogColor: vec4<f32>, clipPlane: vec4<f32>
};
@group(0) @binding(0) var<uniform> scene: SceneUniforms;
struct LightEntry {
    vLightData: vec4<f32>, vLightDiffuse: vec4<f32>,
    vLightSpecular: vec4<f32>, vLightDirection: vec4<f32>
};
struct LightsUniforms {
    count: u32, _p0: u32, _p1: u32, _p2: u32, lights: array<LightEntry, 16>
};
@group(0) @binding(1) var<uniform> lights: LightsUniforms;
struct MeshUniforms { world: mat4x4<f32>, lc: u32, li: array<vec4<u32>, 4> };
@group(1) @binding(0) var<uniform> mesh: MeshUniforms;
struct MaterialUniforms {
    dc: vec4<f32>, sc: vec4<f32>, ec: vec3<f32>, bs: f32, ac: vec3<f32>, tl: f32,
    ambTexLvl: f32, lmLvl: f32, opLvl: f32, aCut: f32,
    rLvl: f32, rCm: f32, _0: f32, _1: f32
};
@group(1) @binding(1) var<uniform> mat: MaterialUniforms;
)WGSL";

static const char* standardVertex = R"WGSL(
struct VertexOutput {
    @builtin(position) clipPos: vec4<f32>,
    @location(0) vp: vec3<f32>, @location(1) vn: vec3<f32>
};
@vertex fn main(@location(0) position: vec3<f32>, @location(1) normal: vec3<f32>)
    -> VertexOutput {
    var out: VertexOutput;
    let worldPos4 = mesh.world * vec4<f32>(position, 1.0);
    out.vp = worldPos4.xyz;
    let normalWorld = mat3x3<f32>(mesh.world[0].xyz, mesh.world[1].xyz, mesh.world[2].xyz);
    out.vn = normalize(normalWorld * normal);
    out.clipPos = scene.viewProjection * worldPos4;
    return out;
}
)WGSL";

static const char* standardLighting = R"WGSL(
fn computeHemisphericLighting(viewDir: vec3<f32>, N: vec3<f32>, L: LightEntry, g: f32)
    -> array<vec3<f32>, 2> {
    let direction = normalize(L.vLightData.xyz);
    let nl = 0.5 + 0.5 * dot(N, direction);
    let diff = mix(L.vLightDirection.xyz, L.vLightDiffuse.rgb, nl);
    let h = normalize(viewDir + direction);
    let s = pow(max(0.0, dot(N, h)), max(1.0, g));
    return array<vec3<f32>, 2>(diff, s * L.vLightSpecular.rgb);
}
)WGSL";

static const char* litFragment = R"WGSL(
@fragment fn main(@location(0) vp: vec3<f32>, @location(1) vn: vec3<f32>)
    -> @location(0) vec4<f32> {
    let viewDirectionW = normalize(scene.vEyePosition.xyz - vp);
    let normalW = normalize(vn);
    var diffuseBase = vec3<f32>(0.0);
    var specularBase = vec3<f32>(0.0);
    let count = min(mesh.lc & 255u, 16u);
    for (var index = 0u; index < count; index++) {
        let lightIndex = mesh.li[index / 4u][index % 4u];
        let result = computeHemisphericLighting(viewDirectionW, normalW,
                                               lights.lights[lightIndex], mat.sc.a);
        diffuseBase += result[0];
        specularBase += result[1];
    }
    let finalDiffuse = clamp(diffuseBase * mat.dc.rgb + mat.ec + mat.ac,
                             vec3<f32>(0.0), vec3<f32>(1.0));
    let finalSpecular = specularBase * mat.sc.rgb;
    return vec4<f32>(max(finalDiffuse + finalSpecular, vec3<f32>(0.0)), mat.dc.a);
}
)WGSL";

static const char* unlitFragment = R"WGSL(
@fragment fn main(@location(0) vp: vec3<f32>, @location(1) vn: vec3<f32>)
    -> @location(0) vec4<f32> {
    return vec4<f32>(max(clamp(mat.ec * mat.dc.rgb, vec3<f32>(0.0), vec3<f32>(1.0)),
                        vec3<f32>(0.0)), mat.dc.a);
}
)WGSL";

static bool append(bl_Runtime* r, L_Text* text, const char* value)
{
    return l_appendText(r, text, {value, strlen(value)});
}

static bool addUniform(bl_Runtime* r, L_Material* m, const char* name, unsigned block,
                       uint32_t offset, bl_ShaderUniformType type)
{
    L_Uniform* uniform = m->uniforms + m->uniformCount;
    uniform->name = l_copyString(r, {name, strlen(name)});
    if (!uniform->name.data)
    {
        return false;
    }
    uniform->block = block;
    uniform->offset = offset;
    uniform->type = type;
    uniform->rawBits = type == BL_UNIFORM_U32 || type == BL_UNIFORM_I32;
    ++m->uniformCount;
    return true;
}

static bool layout(bl_Runtime* r, L_Material* m)
{
    const char* sceneVectors[14] = {
        "vEyePosition",   "vSphericalL00",  "vSphericalL1_1", "vSphericalL10", "vSphericalL11",
        "vSphericalL2_2", "vSphericalL2_1", "vSphericalL20",  "vSphericalL21", "vSphericalL22",
        "vImageInfos",    "vFogInfos",      "vFogColor",      "clipPlane"};
    if (!addUniform(r, m, "viewProjection", 0, 0, BL_UNIFORM_MAT4) ||
        !addUniform(r, m, "view", 0, 64, BL_UNIFORM_MAT4) ||
        !addUniform(r, m, sceneVectors[0], 0, 128, BL_UNIFORM_VEC4))
    {
        return false;
    }
    const char* pads[4] = {"envRotationY", "_envPad0", "_envPad1", "_envPad2"};
    for (unsigned i = 0; i < 4; ++i)
    {
        if (!addUniform(r, m, pads[i], 0, 144 + i * 4, BL_UNIFORM_F32))
        {
            return false;
        }
    }
    for (unsigned i = 1; i < 14; ++i)
    {
        if (!addUniform(r, m, sceneVectors[i], 0, 160 + (i - 1) * 16, BL_UNIFORM_VEC4))
        {
            return false;
        }
    }
    const char* header[4] = {"count", "_p0", "_p1", "_p2"};
    for (unsigned i = 0; i < 4; ++i)
    {
        if (!addUniform(r, m, header[i], 1, i * 4, BL_UNIFORM_U32))
        {
            return false;
        }
    }
    const char* fields[4] = {"vLightData", "vLightDiffuse", "vLightSpecular", "vLightDirection"};
    for (unsigned i = 0; i < 16; ++i)
    {
        for (unsigned j = 0; j < 4; ++j)
        {
            char name[64];
            int count = snprintf(name, sizeof(name), "lights[%u].%s", i, fields[j]);
            if (count < 0 || (size_t)count >= sizeof(name) ||
                !addUniform(r, m, name, 1, 16 + i * 64 + j * 16, BL_UNIFORM_VEC4))
            {
                return false;
            }
        }
    }
    if (!addUniform(r, m, "world", 2, 0, BL_UNIFORM_MAT4) ||
        !addUniform(r, m, "lc", 2, 64, BL_UNIFORM_U32))
    {
        return false;
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        for (unsigned j = 0; j < 4; ++j)
        {
            char name[32];
            int count = snprintf(name, sizeof(name), "li[%u][%u]", i, j);
            if (count < 0 || (size_t)count >= sizeof(name) ||
                !addUniform(r, m, name, 2, 80 + i * 16 + j * 4, BL_UNIFORM_U32))
            {
                return false;
            }
        }
    }
    const char* materialNames[14] = {"dc",    "sc",    "ec",   "bs",   "ac",  "tl", "ambTexLvl",
                                     "lmLvl", "opLvl", "aCut", "rLvl", "rCm", "_0", "_1"};
    const uint32_t offsets[14] = {0, 16, 32, 44, 48, 60, 64, 68, 72, 76, 80, 84, 88, 92};
    for (unsigned i = 0; i < 14; ++i)
    {
        bl_ShaderUniformType type = i < 2              ? BL_UNIFORM_VEC4
                                    : i == 2 || i == 4 ? BL_UNIFORM_VEC3
                                                       : BL_UNIFORM_F32;
        if (!addUniform(r, m, materialNames[i], 3, offsets[i], type))
        {
            return false;
        }
    }
    return true;
}

bl_Status l_standardSources(bl_Runtime* r, L_Material* m, bool disableLighting)
{
    m->record.kind = L_STANDARD;
    m->programIndex = BL_INVALID_BGFX_HANDLE;
    m->sourceLightingDisabled = disableLighting;
    m->blocks[0] = {{"scene", 5}, 0, 0, 368};
    m->blocks[1] = {{"lights", 6}, 0, 1, 1040};
    m->blocks[2] = {{"mesh", 4}, 1, 0, 144};
    m->blocks[3] = {{"mat", 3}, 1, 1, 96};
    m->blockCount = 4;
    m->attributes = (bl_VertexSemantic*)l_alloc(r, 2 * sizeof(*m->attributes));
    m->uniforms = (L_Uniform*)l_alloc(r, 120 * sizeof(*m->uniforms));
    if (!m->attributes || !m->uniforms)
    {
        return BL_OUT_OF_MEMORY;
    }
    m->attributeCount = 2;
    m->attributes[0] = BL_ATTRIBUTE_POSITION;
    m->attributes[1] = BL_ATTRIBUTE_NORMAL;
    if (!layout(r, m))
    {
        return BL_OUT_OF_MEMORY;
    }
    L_Text vertex = {};
    L_Text fragment = {};
    bool valid = append(r, &vertex, standardDeclarations) && append(r, &vertex, standardVertex) &&
                 append(r, &fragment, standardDeclarations) &&
                 (disableLighting || append(r, &fragment, standardLighting)) &&
                 append(r, &fragment, disableLighting ? unlitFragment : litFragment);
    if (!valid)
    {
        l_free(r, vertex.data);
        l_free(r, fragment.data);
        return BL_OUT_OF_MEMORY;
    }
    m->vertex = {vertex.data, vertex.length};
    m->fragment = {fragment.data, fragment.length};
    m->culling = true;
    m->depthWrite = true;
    return BL_OK;
}
