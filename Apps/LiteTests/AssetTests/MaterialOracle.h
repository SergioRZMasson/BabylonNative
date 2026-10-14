#ifndef LITE_ASSET_MATERIAL_ORACLE_H
#define LITE_ASSET_MATERIAL_ORACLE_H

#include <babylon_lite_shader_compiler.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

struct MaterialOracle
{
    bl_ShaderCompilerService2 original = {};
    std::string mode;
    std::filesystem::path output;
    size_t calls = 0;
    size_t releases = 0;
};

static bl_Status compileMaterialOracle(void* user, const bl_ShaderCompileRequest2* request,
                                      bl_ShaderCompileResult2* result)
{
    auto* oracle = static_cast<MaterialOracle*>(user);
    ++oracle->calls;
    try
    {
        std::string fragment(request->base.fragment.wgsl.data, request->base.fragment.wgsl.length);
        std::string final = "return vec4f(color,alpha*material.materialAlpha);";
        size_t offset = fragment.find(final);
        if (offset == std::string::npos)
        {
            final = "return vec4f(color,1.0);";
            offset = fragment.find(final);
        }
        if (offset == std::string::npos &&
            fragment.find("struct MaterialUniforms") != std::string::npos)
        {
            return BL_SHADER_ERROR;
        }
        if (offset != std::string::npos)
        {
            std::ofstream source(oracle->output / ("production-" + std::to_string(oracle->calls) + ".wgsl"));
            source << fragment;
            std::string alphaOutput;
            if (oracle->mode == "emissive-alpha")
            {
                const char* uv = fragment.find("emissiveSampler,input.uv2") == std::string::npos
                                     ? "input.uv" : "input.uv2";
                alphaOutput = "return vec4f(vec3f(textureSample(emissiveTexture,emissiveSampler," +
                    std::string(uv) + ").a*material.surfaceFactors.w),1.0);";
            }
            const char* output = oracle->mode == "normal" ? "return vec4f(N*0.5+0.5,1.0);"
                                 : oracle->mode == "geometric" ? "return vec4f(N_geom*material.normalScale*0.5+0.5,1.0);"
                                 : oracle->mode == "horizon" ? "return vec4f(vec3f(eho),1.0);"
                                 : oracle->mode == "occlusion" ? "return vec4f(vec3f(occlusion),1.0);"
                                 : oracle->mode == "emissive" ? "return vec4f(emissive,1.0);"
                                 : oracle->mode == "emissive-alpha" ? alphaOutput.c_str()
                                                             : nullptr;
            if (!output)
            {
                return BL_INVALID_ARGUMENT;
            }
            fragment.replace(offset, final.length(), output);
            std::ofstream diagnostic(oracle->output / ("diagnostic-" + std::to_string(oracle->calls) + ".wgsl"));
            diagnostic << fragment;
        }
        bl_ShaderCompileRequest2 observed = *request;
        observed.base.fragment.wgsl = {fragment.data(), fragment.length()};
        return oracle->original.compile(oracle->original.userData, &observed, result);
    }
    catch (const std::bad_alloc&)
    {
        return BL_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return BL_HOST_ERROR;
    }
}

static void releaseMaterialOracle(void* user, bl_ShaderCompileResult2* result)
{
    auto* oracle = static_cast<MaterialOracle*>(user);
    ++oracle->releases;
    oracle->original.release(oracle->original.userData, result);
}

static bl_ShaderCompilerService2 materialOracleService(MaterialOracle& oracle)
{
    char mode[64] = {};
    wchar_t output[32768] = {};
    DWORD modeLength = GetEnvironmentVariableA("LITE_TEST_MATERIAL_ORACLE", mode, 64);
    DWORD outputLength = GetEnvironmentVariableW(L"LITE_TEST_MATERIAL_OUTPUT", output, 32768);
    if (!modeLength || modeLength >= 64 || !outputLength || outputLength >= 32768)
    {
        throw std::runtime_error("Test-only material oracle requires explicit mode/output");
    }
    oracle.mode = mode;
    oracle.output = std::filesystem::path(output);
    std::filesystem::create_directories(oracle.output);
    oracle.original = bl_shaderCompilerService2();
    auto service = oracle.original;
    service.userData = &oracle;
    service.compile = compileMaterialOracle;
    service.release = releaseMaterialOracle;
    return service;
}

#endif
