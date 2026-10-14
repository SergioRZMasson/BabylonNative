#include "PbrInternal.h"

static const char declarations[] = R"(
struct SceneUniforms {
 viewProjection:mat4x4f,
 view:mat4x4f,
 vEyePosition:vec4f,
 envRotationY:f32,_envPad0:f32,_envPad1:f32,_envPad2:f32,
 vSphericalL00:vec4f,vSphericalL1_1:vec4f,vSphericalL10:vec4f,vSphericalL11:vec4f,
 vSphericalL2_2:vec4f,vSphericalL2_1:vec4f,vSphericalL20:vec4f,vSphericalL21:vec4f,
 vSphericalL22:vec4f,vImageInfos:vec4f,vFogInfos:vec4f,vFogColor:vec4f,clipPlane:vec4f
};
struct MeshUniforms {world:mat4x4f,lc:u32,li:array<vec4u,4>};
struct MaterialUniforms {
 environmentIntensity:f32,directIntensity:f32,reflectance:f32,materialAlpha:f32,
 metallicFactor:f32,roughnessFactor:f32,normalScale:f32,alphaCutoff:f32
};
@group(0)@binding(0)var<uniform>scene:SceneUniforms;
@group(1)@binding(0)var<uniform>mesh:MeshUniforms;
@group(1)@binding(1)var<uniform>material:MaterialUniforms;
struct VertexOutput {
 @builtin(position)clipPos:vec4f,@location(0)worldPos:vec3f,
 @location(1)worldNormal:vec3f,@location(2)uv:vec2f
};
)";

static const char vertexPlain[] = R"(
@vertex fn main(@location(0)position:vec3f,@location(1)normal:vec3f,
@location(2)uv:vec2f)->VertexOutput{
var out:VertexOutput;var finalWorld=mesh.world;
let worldPos4=finalWorld*vec4f(position,1);
out.worldPos=worldPos4.xyz;out.clipPos=scene.viewProjection*worldPos4;
out.worldNormal=(finalWorld*vec4f(normalize(normal),0)).xyz;out.uv=uv;return out;
})";

static const char vertexInstanced[] = R"(
@vertex fn main(@location(0)position:vec3f,@location(1)normal:vec3f,
@location(2)uv:vec2f,@location(3)world0:vec4f,@location(4)world1:vec4f,
@location(5)world2:vec4f,@location(6)world3:vec4f)->VertexOutput{
var out:VertexOutput;var finalWorld=mesh.world;
let instanceWorld=mat4x4f(world0,world1,world2,world3);
finalWorld=mesh.world*instanceWorld;
let worldPos4=finalWorld*vec4f(position,1);
out.worldPos=worldPos4.xyz;out.clipPos=scene.viewProjection*worldPos4;
out.worldNormal=(finalWorld*vec4f(normalize(normal),0)).xyz;out.uv=uv;return out;
})";

static const char fragmentBody[] = R"(
@group(1)@binding(2)var baseColorTexture:texture_2d<f32>;
@group(1)@binding(3)var baseColorSampler:sampler;
@group(1)@binding(4)var ormTexture:texture_2d<f32>;
@group(1)@binding(5)var ormSampler:sampler;
@group(1)@binding(6)var brdfLUT:texture_2d<f32>;
@group(1)@binding(7)var brdfSampler_:sampler;
@group(1)@binding(8)var iblTexture:texture_cube<f32>;
@group(1)@binding(9)var iblSampler:sampler;
fn getEnergyConservationFactor(F0:vec3f,brdfY:f32)->vec3f{
return 1.0+F0*(1.0/brdfY-1.0);
}
fn rotateY(v:vec3f,angle:f32)->vec3f{
let c=cos(angle);let s=sin(angle);
return vec3f(v.x*c+v.z*s,v.y,-v.x*s+v.z*c);
}
@fragment fn main(input:VertexOutput)->@location(0)vec4f{
let baseColorSample=textureSample(baseColorTexture,baseColorSampler,input.uv);
var baseColor=baseColorSample.rgb;var alpha=baseColorSample.a;
let orm=textureSample(ormTexture,ormSampler,input.uv).rgb;
var occlusion=1.0;
var roughness=clamp(orm.g*material.roughnessFactor,0.0,1.0);
var metallic=orm.b*material.metallicFactor;var emissive:vec3f;
var N_geom=normalize(input.worldNormal);var N=N_geom;
let V=normalize(scene.vEyePosition.xyz-input.worldPos);
var NdotVUnclamped=dot(N,V);var NdotV=abs(NdotVUnclamped)+0.0000001;
let dielectricF0=material.reflectance;
var colorF0=mix(vec3f(dielectricF0),baseColor,metallic);
let colorF90=vec3f(1.0);
var surfaceAlbedo=baseColor*(1.0-dielectricF0)*(1.0-metallic);
var alphaG=roughness*roughness+0.0005;
var AA_factor_x=0.0;var AA_factor_y=0.0;
{let nDfdx_AA=dpdx(N);let nDfdy_AA=dpdy(N);
let slopeSquare_AA=max(dot(nDfdx_AA,nDfdx_AA),dot(nDfdy_AA,nDfdy_AA));
AA_factor_x=pow(saturate(slopeSquare_AA),0.333);
AA_factor_y=sqrt(slopeSquare_AA)*0.75;alphaG+=AA_factor_y;}
var directDiffuse=vec3f(0.0);var directSpecular=vec3f(0.0);
var color=directDiffuse+directSpecular+emissive;
let R_raw=reflect(-V,N);let R=rotateY(R_raw,scene.envRotationY);
let N_env=rotateY(N,scene.envRotationY);
let brdf=textureSample(brdfLUT,brdfSampler_,vec2f(NdotV,roughness));
let environmentBrdf=brdf.rgb;
let specularEnvironmentReflectance=(colorF90-colorF0)*environmentBrdf.x+colorF0*environmentBrdf.y;
let seo=clamp((NdotVUnclamped+occlusion)*(NdotVUnclamped+occlusion)-1.0+occlusion,0.0,1.0);
let eho=1.0;let colorSpecularEnvReflectance=specularEnvironmentReflectance*seo*eho;
let energyConservation=getEnergyConservationFactor(colorF0,max(environmentBrdf.y,0.001));
let environmentIrradiance=(scene.vSphericalL00.rgb
+scene.vSphericalL1_1.rgb*N_env.y+scene.vSphericalL10.rgb*N_env.z+scene.vSphericalL11.rgb*N_env.x
+scene.vSphericalL2_2.rgb*(N_env.y*N_env.x)+scene.vSphericalL2_1.rgb*(N_env.y*N_env.z)
+scene.vSphericalL20.rgb*(3.0*N_env.z*N_env.z-1.0)+scene.vSphericalL21.rgb*(N_env.z*N_env.x)
+scene.vSphericalL22.rgb*(N_env.x*N_env.x-N_env.y*N_env.y))*material.environmentIntensity;
let maxLod=f32(textureNumLevels(iblTexture)-1);
let cubemapDim=f32(textureDimensions(iblTexture).x);
var specLod=log2(cubemapDim*alphaG)*scene.vImageInfos.z;
var environmentRadiance=textureSampleLevel(iblTexture,iblSampler,R,clamp(specLod,0.0,maxLod)).rgb
*material.environmentIntensity;
environmentRadiance=mix(environmentRadiance,environmentIrradiance,alphaG);
let finalIrradiance=environmentIrradiance*surfaceAlbedo*occlusion;
let finalSpecularScaled=directSpecular*energyConservation;
let finalRadianceScaled=environmentRadiance*colorSpecularEnvReflectance*energyConservation;
color=finalIrradiance+finalRadianceScaled+finalSpecularScaled+directDiffuse+emissive;
color*=scene.vImageInfos.x;color=1.0-exp2(-1.590579*color);
color=pow(color,vec3f(1.0/2.2));color=clamp(color,vec3f(0.0),vec3f(1.0));
let highContrast=color*color*(3.0-2.0*color);
if(scene.vImageInfos.y<1.0){color=mix(vec3f(0.5),color,scene.vImageInfos.y);}
else{color=mix(color,highContrast,scene.vImageInfos.y-1.0);}
color=max(color,vec3f(0.0));return vec4f(color,alpha*material.materialAlpha);
})";

static bool appendDoubleSidedBody(bl_Runtime* runtime, L_Text* fragment)
{
    const char* signature = strstr(fragmentBody, "@fragment fn main");
    const char* open = strchr(signature, '{');
    const char* normal = strstr(open, "var N_geom=normalize(input.worldNormal);");
    const char* afterNormal = normal + strlen("var N_geom=normalize(input.worldNormal);");
    return l_appendText(runtime, fragment, {fragmentBody, (size_t)(signature - fragmentBody)}) &&
           l_appendText(runtime, fragment,
                        {"@fragment fn main(input:VertexOutput,@builtin(front_facing)front:bool)"
                         "->@location(0)vec4f{",
                         strlen("@fragment fn main(input:VertexOutput,"
                                "@builtin(front_facing)front:bool)->@location(0)vec4f{")}) &&
           l_appendText(runtime, fragment, {open + 1, (size_t)(normal - open - 1)}) &&
           l_appendText(runtime, fragment,
                        {"var N_geom=normalize(input.worldNormal);"
                         "let mirrored=determinant(mat3x3f(mesh.world[0].xyz,"
                         "mesh.world[1].xyz,mesh.world[2].xyz))>0.0;"
                         "if(front!=mirrored){N_geom=-N_geom;}",
                         strlen("var N_geom=normalize(input.worldNormal);"
                                "let mirrored=determinant(mat3x3f(mesh.world[0].xyz,"
                                "mesh.world[1].xyz,mesh.world[2].xyz))>0.0;"
                                "if(front!=mirrored){N_geom=-N_geom;}")}) &&
           l_appendText(runtime, fragment, {afterNormal, strlen(afterNormal)});
}

static bool replacePbrText(bl_Runtime* runtime, L_Text* text, const char* needle,
                           const char* replacement)
{
    const char* found = strstr(text->data, needle);
    if (!found)
    {
        return false;
    }
    L_Text result = {};
    size_t prefix = (size_t)(found - text->data);
    size_t replaced = strlen(needle);
    if (!l_appendText(runtime, &result, {text->data, prefix}) ||
        !l_appendText(runtime, &result, {replacement, strlen(replacement)}) ||
        !l_appendText(runtime, &result, {found + replaced, text->length - prefix - replaced}))
    {
        l_free(runtime, result.data);
        return false;
    }
    l_free(runtime, text->data);
    *text = result;
    return true;
}

static bool appendNormalSource(bl_Runtime* runtime, L_Text* vertex, L_Text* fragment, bool tangents,
                               bool doubleSided)
{
    if (tangents)
    {
        const char* output = "@location(2)uv:vec2f";
        const char* varyings = "@location(4)worldTangent:vec3f,"
                               "@location(5)worldBitangent:vec3f,@location(2)uv:vec2f";
        if (!replacePbrText(runtime, vertex, output, varyings) ||
            !replacePbrText(runtime, fragment, output, varyings) ||
            !replacePbrText(runtime, vertex, "@location(1)normal:vec3f,\n@location(2)uv:vec2f",
                            "@location(1)normal:vec3f,\n"
                            "@location(8)tangent:vec4f,@location(2)uv:vec2f") ||
            !replacePbrText(runtime, vertex, "out.uv=uv;",
                            "let T_local=normalize(tangent.xyz);"
                            "let B_local=cross(normalize(normal),T_local)*tangent.w;"
                            "out.worldTangent=(finalWorld*vec4f(T_local,0)).xyz;"
                            "out.worldBitangent=(finalWorld*vec4f(B_local,0)).xyz;out.uv=uv;"))
        {
            return false;
        }
    }
    const char* tangentNormal =
        "let normalMapRaw=textureSample(normalTexture,normalSampler_,normalUv).rgb*2.0-1.0;"
        "let scaledNormal=vec3f(normalMapRaw.xy*material.normalScale,normalMapRaw.z);"
        "let TBN=mat3x3f(input.worldTangent,input.worldBitangent,input.worldNormal);"
        "var N=normalize(TBN*normalize(scaledNormal));";
    // The source cotangent frame uses +dpdy and the negated bitangent, not an image-channel flip.
    const char* cotangentNormal =
        "let normalMapSample=textureSample(normalTexture,normalSampler_,normalUv).rgb*2.0-1.0;"
        "let scaledNormalCT=vec3f(normalMapSample.xy*material.normalScale,normalMapSample.z);"
        "let frameNormal=normalize(input.worldNormal);"
        "let dp1=dpdx(input.worldPos);let dp2=dpdy(input.worldPos);"
        "let duv1=dpdx(normalUv);let duv2=dpdy(normalUv);"
        "let dp2perp=cross(dp2,frameNormal);let dp1perp=cross(frameNormal,dp1);"
        "let tangent_ct=dp2perp*duv1.x+dp1perp*duv2.x;"
        "let bitangent_ct=-(dp2perp*duv1.y+dp1perp*duv2.y);"
        "let det=max(dot(tangent_ct,tangent_ct),dot(bitangent_ct,bitangent_ct));"
        "let invmax=select(inverseSqrt(det),0.0,det==0.0);"
        "let cotangentFrame=mat3x3f(tangent_ct*invmax,bitangent_ct*invmax,frameNormal);"
        "var N=normalize(cotangentFrame*normalize(scaledNormalCT));";
    L_Text normal = {};
    bool appended = l_appendText(runtime, &normal, {"let normalUv=input.uv;", 22}) &&
                    l_appendText(runtime, &normal,
                                 {tangents ? tangentNormal : cotangentNormal,
                                  strlen(tangents ? tangentNormal : cotangentNormal)});
    if (appended && doubleSided)
    {
        const char* flip = "if(front!=mirrored){N=-N;}";
        appended = l_appendText(runtime, &normal, {flip, strlen(flip)});
    }
    bool result = appended && replacePbrText(runtime, fragment, "var N=N_geom;", normal.data);
    l_free(runtime, normal.data);
    return result;
}

bl_Status l_pbrSources(bl_Runtime* runtime, bool instances, L_Text* vertex, L_Text* fragment,
                       bool doubleSided, bool baseImage, bool baseUv1, bool ormUv1,
                       const L_Material* material, bool tangents)
{
    bool extra = material && material->pbrExtra;
    bool normal = material && !L_NULL(material->pbrNormalTexture);
    bool occlusion = material && !L_NULL(material->pbrOcclusionTexture);
    bool emissive = material && !L_NULL(material->pbrEmissiveTexture);
    const char* body = instances ? vertexInstanced : vertexPlain;
    if (!l_appendText(runtime, vertex, {declarations, sizeof(declarations) - 1}) ||
        !l_appendText(runtime, vertex, {body, strlen(body)}) ||
        !l_appendText(runtime, fragment, {declarations, sizeof(declarations) - 1}))
    {
        return BL_OUT_OF_MEMORY;
    }
    if (!(doubleSided ? appendDoubleSidedBody(runtime, fragment)
                      : l_appendText(runtime, fragment, {fragmentBody, sizeof(fragmentBody) - 1})))
    {
        return BL_OUT_OF_MEMORY;
    }
    if (baseImage || extra)
    {
        const char* needle = "normalScale:f32,alphaCutoff:f32";
        const char* replacement = "normalScale:f32,alphaCutoff:f32,baseColorFactor:vec4f";
        if (!replacePbrText(runtime, vertex, needle, replacement) ||
            !replacePbrText(runtime, fragment, needle, replacement))
        {
            return BL_OUT_OF_MEMORY;
        }
    }
    if (baseImage && !replacePbrText(runtime, fragment,
                                     "textureSample(baseColorTexture,baseColorSampler,input.uv);",
                                     "textureSample(baseColorTexture,baseColorSampler,input.uv)"
                                     "*material.baseColorFactor;"))
    {
        return BL_OUT_OF_MEMORY;
    }
    if (extra)
    {
        const char* field = "baseColorFactor:vec4f";
        const char* fields = "baseColorFactor:vec4f,surfaceFactors:vec4f";
        if (!replacePbrText(runtime, vertex, field, fields) ||
            !replacePbrText(runtime, fragment, field, fields) ||
            !replacePbrText(runtime, fragment, "var emissive:vec3f;",
                            "var emissive=material.surfaceFactors.xyz;"))
        {
            return BL_OUT_OF_MEMORY;
        }
    }
    if (normal)
    {
        const char* binding = "@group(1)@binding(10)var normalTexture:texture_2d<f32>;"
                              "@group(1)@binding(11)var normalSampler_:sampler;";
        if (!l_appendText(runtime, fragment, {binding, strlen(binding)}) ||
            !appendNormalSource(runtime, vertex, fragment, tangents, doubleSided) ||
            !replacePbrText(runtime, fragment, "let eho=1.0;",
                            "let horizonR=reflect(-V,N);"
                            "let horizonTemp=saturate(1.0+1.1*dot(horizonR,N_geom));"
                            "let eho=horizonTemp*horizonTemp;"))
        {
            return BL_OUT_OF_MEMORY;
        }
    }
    if (occlusion)
    {
        const char* binding = "@group(1)@binding(12)var occlusionTexture:texture_2d<f32>;"
                              "@group(1)@binding(13)var occlusionSampler_:sampler;";
        if (!l_appendText(runtime, fragment, {binding, strlen(binding)}) ||
            !replacePbrText(runtime, fragment, "var occlusion=1.0;",
                            "let sampledOcclusion=textureSample(occlusionTexture,"
                            "occlusionSampler_,input.uv).r;"
                            "var occlusion=mix(1.0,sampledOcclusion,material.surfaceFactors.w);"))
        {
            return BL_OUT_OF_MEMORY;
        }
    }
    if (emissive)
    {
        const char* binding = "@group(1)@binding(14)var emissiveTexture:texture_2d<f32>;"
                              "@group(1)@binding(15)var emissiveSampler:sampler;";
        if (!l_appendText(runtime, fragment, {binding, strlen(binding)}) ||
            !replacePbrText(runtime, fragment, "var emissive=material.surfaceFactors.xyz;",
                            "var emissive=material.surfaceFactors.xyz*"
                            "textureSample(emissiveTexture,emissiveSampler,input.uv).rgb;"))
        {
            return BL_OUT_OF_MEMORY;
        }
    }
    if (baseUv1 || ormUv1 ||
        (extra &&
         (material->pbrNormalUv1 || material->pbrOcclusionUv1 || material->pbrEmissiveUv1)))
    {
        const char* output = "@location(2)uv:vec2f";
        const char* extended = "@location(2)uv:vec2f,@location(3)uv2:vec2f";
        if (!replacePbrText(runtime, vertex, output, extended) ||
            !replacePbrText(runtime, fragment, output, extended) ||
            !replacePbrText(runtime, vertex,
                            instances ? "@location(2)uv:vec2f,@location(3)world0"
                                      : "@location(2)uv:vec2f)->VertexOutput",
                            instances
                                ? "@location(2)uv:vec2f,@location(7)uv2:vec2f,"
                                  "@location(3)world0"
                                : "@location(2)uv:vec2f,@location(7)uv2:vec2f)->VertexOutput") ||
            !replacePbrText(runtime, vertex, "out.uv=uv;", "out.uv=uv;out.uv2=uv2;"))
        {
            return BL_OUT_OF_MEMORY;
        }
    }
    if (baseUv1 && !replacePbrText(runtime, fragment, "baseColorSampler,input.uv",
                                   "baseColorSampler,input.uv2"))
    {
        return BL_OUT_OF_MEMORY;
    }
    if (ormUv1 && !replacePbrText(runtime, fragment, "ormSampler,input.uv", "ormSampler,input.uv2"))
    {
        return BL_OUT_OF_MEMORY;
    }
    if ((normal && material->pbrNormalUv1 &&
         !replacePbrText(runtime, fragment, "normalUv=input.uv;", "normalUv=input.uv2;")) ||
        (occlusion && material->pbrOcclusionUv1 &&
         !replacePbrText(runtime, fragment, "occlusionSampler_,input.uv",
                         "occlusionSampler_,input.uv2")) ||
        (emissive && material->pbrEmissiveUv1 &&
         !replacePbrText(runtime, fragment, "emissiveSampler,input.uv",
                         "emissiveSampler,input.uv2")))
    {
        return BL_OUT_OF_MEMORY;
    }
    if (material)
    {
        // Factor textures retain source RGB bytes, but never quantize alpha classification.
        if ((!baseImage || (!material->testing && !material->blending)) &&
            !replacePbrText(runtime, fragment, "var alpha=baseColorSample.a;", "var alpha=1.0;"))
        {
            return BL_OUT_OF_MEMORY;
        }
        if (material->testing &&
            !replacePbrText(runtime, fragment, "return vec4f(color,alpha*material.materialAlpha);",
                            "if(alpha*material.materialAlpha<material.alphaCutoff){discard;}"
                            "return vec4f(color,1.0);"))
        {
            return BL_OUT_OF_MEMORY;
        }
    }
    return BL_OK;
}
