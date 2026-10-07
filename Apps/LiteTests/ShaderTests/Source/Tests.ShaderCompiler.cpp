#include <babylon_lite_shader_compiler.h>
#include <gtest/gtest.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view Vertex = R"(
struct Input { @location(0) position: vec3f };
struct Output { @builtin(position) position: vec4f, @location(0) uv: vec2f };
@vertex fn customVertex(input: Input) -> Output {
    var output: Output;
    output.position = vec4f(input.position, 1.0);
    output.uv = input.position.xy;
    return output;
})";

    constexpr std::string_view Fragment = R"(
@fragment fn customFragment(@location(0) uv: vec2f) -> @location(0) vec4f {
    return vec4f(uv, 0.25, 1.0);
})";

    bl_String String(std::string_view value)
    {
        return {value.data(), value.size()};
    }

    struct Compiled
    {
        bl_ShaderCompilerService service{bl_shaderCompilerService()};
        bl_ShaderCompileResult result{};
        bl_Status status{};

        Compiled(std::string_view vertex = Vertex, std::string_view fragment = Fragment)
        {
            const bl_ShaderCompileRequest request{BL_CONTRACT_VERSION, BL_RENDERER_D3D11, 12,
                {BL_STAGE_VERTEX, String(vertex), String("customVertex")},
                {BL_STAGE_FRAGMENT, String(fragment), String("customFragment")}, false, false, {}};
            status = service.compile(service.userData, &request, &result);
        }

        ~Compiled()
        {
            service.release(service.userData, &result);
        }

        std::string Diagnostics() const
        {
            return result.diagnostics.length ? std::string(result.diagnostics.data, result.diagnostics.length) : "";
        }
    };

    uint32_t Read32(const uint8_t* bytes)
    {
        return static_cast<uint32_t>(bytes[0]) | static_cast<uint32_t>(bytes[1]) << 8 |
               static_cast<uint32_t>(bytes[2]) << 16 | static_cast<uint32_t>(bytes[3]) << 24;
    }

    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> ReflectBytecode(bl_Bytes bytes)
    {
        if (bytes.count < 22)
        {
            return {};
        }
        const uint16_t uniforms = static_cast<uint16_t>(bytes.data[20] | bytes.data[21] << 8);
        size_t offset = 22;
        for (uint16_t i = 0; i < uniforms; ++i)
        {
            if (offset >= bytes.count)
            {
                return {};
            }
            offset += 1 + bytes.data[offset] + 10;
        }
        if (offset + 4 > bytes.count)
        {
            return {};
        }
        const uint32_t length = Read32(bytes.data + offset);
        offset += 4;
        if (offset + length > bytes.count)
        {
            return {};
        }
        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
        if (FAILED(D3DReflect(bytes.data + offset, length, IID_ID3D11ShaderReflection,
                reinterpret_cast<void**>(reflection.GetAddressOf()))))
        {
            return {};
        }
        return reflection;
    }
}

TEST(LiteShaderCompiler, CompilesRuntimeWGSLWithArbitraryEntryPointNames)
{
    Compiled compiled;
    ASSERT_EQ(compiled.status, BL_OK) << compiled.Diagnostics();
    ASSERT_GT(compiled.result.vertexContainer.count, 32u);
    ASSERT_GT(compiled.result.fragmentContainer.count, 32u);
    EXPECT_EQ(std::memcmp(compiled.result.vertexContainer.data, "VSH\x0c", 4), 0);
    EXPECT_EQ(std::memcmp(compiled.result.fragmentContainer.data, "FSH\x0c", 4), 0);
    ASSERT_EQ(compiled.result.attributeCount, 1u);
    EXPECT_EQ(compiled.result.attributes[0].semantic, BL_ATTRIBUTE_POSITION);
    EXPECT_EQ(compiled.result.attributes[0].components, 3);
    EXPECT_NE(ReflectBytecode(compiled.result.vertexContainer), nullptr);
    EXPECT_NE(ReflectBytecode(compiled.result.fragmentContainer), nullptr);
}

TEST(LiteShaderCompiler, DifferentDynamicExpressionsProduceDifferentNativeShaders)
{
    const std::string firstFragment(Fragment);
    std::string secondFragment(Fragment);
    secondFragment.replace(secondFragment.find("0.25"), 4, "0.75");
    Compiled first(Vertex, firstFragment);
    Compiled second(Vertex, secondFragment);
    ASSERT_EQ(first.status, BL_OK) << first.Diagnostics();
    ASSERT_EQ(second.status, BL_OK) << second.Diagnostics();
    const auto a = first.result.fragmentContainer;
    const auto b = second.result.fragmentContainer;
    EXPECT_NE(std::vector<uint8_t>(a.data, a.data + a.count), std::vector<uint8_t>(b.data, b.data + b.count));
}

TEST(LiteShaderCompiler, RejectsInvalidWGSLWithRealSourceDiagnostics)
{
    Compiled compiled("@vertex fn customVertex() -> @builtin(position) vec4f { return unknownVariable; }");
    EXPECT_EQ(compiled.status, BL_SHADER_ERROR);
    EXPECT_NE(compiled.Diagnostics().find("unknownVariable"), std::string::npos);
    EXPECT_NE(compiled.Diagnostics().find("vertex.wgsl"), std::string::npos);
    EXPECT_EQ(compiled.result.vertexContainer.count, 0u);
}

TEST(LiteShaderCompiler, RejectsMismatchedStageInterfaces)
{
    const std::string fragment = R"(
@fragment fn customFragment(@location(0) uv: vec3f) -> @location(0) vec4f {
    return vec4f(uv, 1.0);
})";
    Compiled compiled(Vertex, fragment);
    EXPECT_EQ(compiled.status, BL_SHADER_ERROR);
    EXPECT_NE(compiled.Diagnostics().find("vertex outputs"), std::string::npos);
}

TEST(LiteShaderCompiler, ConsolidatesMultipleWGSLBlocksAndPreservesMemberLayouts)
{
    const std::string vertex = R"(
struct System { transform: mat4x4f };
struct Material { tint: vec3f, amount: f32, index: u32 };
@group(1) @binding(0) var<uniform> shaderSystem: System;
@group(1) @binding(1) var<uniform> shaderUniforms: Material;
struct Input { @location(0) position: vec3f };
@vertex fn customVertex(input: Input) -> @builtin(position) vec4f {
    let factor = shaderUniforms.amount + f32(shaderUniforms.index);
    return shaderSystem.transform * vec4f(input.position * shaderUniforms.tint * factor, 1.0);
})";
    const std::string fragment = R"(
@fragment fn customFragment() -> @location(0) vec4f { return vec4f(0.4, 0.6, 0.8, 1.0); }
)";
    Compiled compiled(vertex, fragment);
    ASSERT_EQ(compiled.status, BL_OK) << compiled.Diagnostics();
    ASSERT_EQ(compiled.result.uniformBlockCount, 2u);
    ASSERT_EQ(compiled.result.uniformCount, 4u);
    const bl_ReflectedUniform* tint = nullptr;
    const bl_ReflectedUniform* amount = nullptr;
    const bl_ReflectedUniform* index = nullptr;
    for (size_t i = 0; i < compiled.result.uniformCount; ++i)
    {
        const auto& uniform = compiled.result.uniforms[i];
        const std::string_view name(uniform.name.data, uniform.name.length);
        if (name == "tint")
            tint = &uniform;
        if (name == "amount")
            amount = &uniform;
        if (name == "index")
            index = &uniform;
    }
    ASSERT_NE(tint, nullptr);
    ASSERT_NE(amount, nullptr);
    ASSERT_NE(index, nullptr);
    EXPECT_EQ(tint->byteOffset, 0u);
    EXPECT_EQ(tint->byteSize, 12u);
    EXPECT_EQ(amount->byteOffset, 12u);
    EXPECT_EQ(index->byteOffset, 16u);
    EXPECT_EQ(index->type, BL_UNIFORM_U32);
    EXPECT_EQ(index->nativeCount, 2u);
    EXPECT_EQ(index->nativeByteOffset, 16u);
    EXPECT_EQ(std::string_view(tint->nativeName.data, tint->nativeName.length),
        std::string_view(index->nativeName.data, index->nativeName.length));

    auto native = ReflectBytecode(compiled.result.vertexContainer);
    ASSERT_NE(native, nullptr);
    D3D11_SHADER_DESC shader{};
    ASSERT_TRUE(SUCCEEDED(native->GetDesc(&shader)));
    EXPECT_EQ(shader.ConstantBuffers, 1u);
    D3D11_SHADER_INPUT_BIND_DESC binding{};
    ASSERT_TRUE(SUCCEEDED(native->GetResourceBindingDesc(0, &binding)));
    EXPECT_EQ(binding.Type, D3D_SIT_CBUFFER);
    EXPECT_EQ(binding.BindPoint, 0u);
    D3D11_SHADER_BUFFER_DESC buffer{};
    ASSERT_TRUE(SUCCEEDED(native->GetConstantBufferByIndex(0)->GetDesc(&buffer)));
    EXPECT_EQ(buffer.Size, 96u);
}

TEST(LiteShaderCompiler, ReflectsOriginalTextureAndSamplerBindings)
{
    const std::string fragment = R"(
@group(1) @binding(5) var atlasTex: texture_2d<f32>;
@group(1) @binding(6) var atlasTexSampler: sampler;
@fragment fn customFragment(@location(0) uv: vec2f) -> @location(0) vec4f {
    let pixel = textureSample(atlasTex, atlasTexSampler, uv);
    if (pixel.a < 0.1) { discard; }
    return pixel;
})";
    Compiled compiled(Vertex, fragment);
    ASSERT_EQ(compiled.status, BL_OK) << compiled.Diagnostics();
    ASSERT_EQ(compiled.result.textureCount, 1u);
    const auto& texture = compiled.result.textures[0];
    EXPECT_EQ(std::string_view(texture.name.data, texture.name.length), "atlasTex");
    EXPECT_EQ(std::string_view(texture.samplerName.data, texture.samplerName.length), "atlasTexSampler");
    EXPECT_EQ(texture.textureGroup, 1u);
    EXPECT_EQ(texture.textureBinding, 5u);
    EXPECT_EQ(texture.samplerBinding, 6u);
    EXPECT_EQ(texture.stages, static_cast<uint32_t>(BL_STAGE_FRAGMENT));
    EXPECT_EQ(texture.nativeTextureStage, 0);
}

TEST(LiteShaderCompiler, RejectsUnsupportedBackendAndReleasesFailureResult)
{
    auto service = bl_shaderCompilerService();
    bl_ShaderCompileRequest request{BL_CONTRACT_VERSION, BL_RENDERER_VULKAN, 12,
        {BL_STAGE_VERTEX, String(Vertex), String("customVertex")},
        {BL_STAGE_FRAGMENT, String(Fragment), String("customFragment")}, false, false, {}};
    bl_ShaderCompileResult result{};
    EXPECT_EQ(service.compile(service.userData, &request, &result), BL_UNSUPPORTED);
    EXPECT_GT(result.diagnostics.length, 0u);
    service.release(service.userData, &result);
    EXPECT_EQ(result.allocation, nullptr);
    EXPECT_EQ(result.diagnostics.length, 0u);
    service.release(service.userData, &result);
}
