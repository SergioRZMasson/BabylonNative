#include <babylon_lite_shader_compiler.h>
#include <gtest/gtest.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <cstring>
#include <string>

namespace
{
    bl_String Text(const char* value)
    {
        return {value, std::strlen(value)};
    }

    constexpr const char* Compute = R"(
struct Params { factor:u32, count:u32, pad:vec2u };
@group(0) @binding(3) var<uniform> params:Params;
@group(0) @binding(0) var<storage,read> source:array<u32>;
@group(0) @binding(5) var<storage,read_write> destination:array<atomic<u32>>;
@compute @workgroup_size(64)
fn arbitraryEntry(@builtin(global_invocation_id) id:vec3u) {
    if(id.x<params.count) { atomicAdd(&destination[id.x],source[id.x]*params.factor); }
})";

    uint32_t Word(const uint8_t* bytes)
    {
        return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 |
            uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
    }
}

TEST(LiteComputeCompiler, RuntimeRawStorageUniformsWorkgroupAndRealContainer)
{
    auto compiler = bl_computeShaderCompilerService();
    const bl_ComputeShaderCompileRequest request{sizeof(request), BL_COMPUTE_SHADER_VERSION,
        BL_RENDERER_D3D11, 12, Text(Compute), Text("arbitraryEntry"), Text("compute-test")};
    bl_ComputeShaderCompileResult result{};
    const auto status = compiler.compile(compiler.userData, &request, &result);
    ASSERT_EQ(status, BL_OK) << std::string(result.diagnostics.data ? result.diagnostics.data : "",
        result.diagnostics.length);
    ASSERT_GT(result.container.count, 30u);
    EXPECT_EQ(std::memcmp(result.container.data, "CSH\x0c", 4), 0);
    EXPECT_EQ(Word(result.container.data + 4), 0u);
    EXPECT_EQ(Word(result.container.data + 8), 0u);
    EXPECT_EQ(result.workgroupSize[0], 64u);
    EXPECT_EQ(result.workgroupSize[1], 1u);
    EXPECT_EQ(result.workgroupSize[2], 1u);
    ASSERT_EQ(result.storageBufferCount, 2u);
    uint32_t srv{};
    uint32_t uav{};
    for (size_t i = 0; i < result.storageBufferCount; ++i)
    {
        const auto& binding = result.storageBuffers[i];
        EXPECT_EQ(binding.group, 0u);
        EXPECT_EQ(binding.binding, binding.readOnly ? 0u : 5u);
        EXPECT_GE(binding.minimumBytes, 4u);
        (binding.readOnly ? srv : uav) |= 1u << binding.nativeStage;
    }
    EXPECT_EQ(Word(result.container.data + 12), srv);
    EXPECT_EQ(Word(result.container.data + 16), uav);
    ASSERT_EQ(result.uniformBlockCount, 1u);
    EXPECT_EQ(result.uniformBlocks[0].binding, 3u);
    EXPECT_EQ(result.uniformBlocks[0].byteSize, 16u);
    for (size_t i = 0; i < result.uniformCount; ++i)
        EXPECT_EQ(result.uniforms[i].stages, uint32_t(BL_STAGE_COMPUTE));
    const auto* bytes = result.container.data;
    size_t offset = 22;
    const uint16_t uniforms = bytes[20] | uint16_t(bytes[21]) << 8;
    for (uint16_t i = 0; i < uniforms; ++i)
        offset += 1 + bytes[offset] + 10;
    ASSERT_LT(offset + 4, result.container.count);
    const auto size = Word(bytes + offset);
    offset += 4;
    ASSERT_LE(offset + size, result.container.count);
    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
    ASSERT_TRUE(SUCCEEDED(D3DReflect(bytes + offset, size, IID_PPV_ARGS(&reflection))));
    UINT x{}, y{}, z{};
    reflection->GetThreadGroupSize(&x, &y, &z);
    EXPECT_EQ(x, 64u);
    compiler.release(compiler.userData, &result);
    EXPECT_EQ(result.allocation, nullptr);
    EXPECT_EQ(result.container.data, nullptr);
}

TEST(LiteComputeCompiler, FailureResultsRemainReleasableAndRetrySucceeds)
{
    auto compiler = bl_computeShaderCompilerService();
    bl_ComputeShaderCompileRequest request{sizeof(request), BL_COMPUTE_SHADER_VERSION,
        BL_RENDERER_D3D11, 12, Text("@compute @workgroup_size(64) fn main(){ bad(); }"),
        Text("main"), Text("bad-compute")};
    bl_ComputeShaderCompileResult result{};
    EXPECT_EQ(compiler.compile(compiler.userData, &request, &result), BL_SHADER_ERROR);
    EXPECT_GT(result.diagnostics.length, 0u);
    EXPECT_EQ(result.container.count, 0u);
    compiler.release(compiler.userData, &result);
    request.backend = BL_RENDERER_VULKAN;
    EXPECT_EQ(compiler.compile(compiler.userData, &request, &result), BL_UNSUPPORTED);
    compiler.release(compiler.userData, &result);
    request.backend = BL_RENDERER_D3D11;
    request.wgsl = Text(Compute);
    request.entryPoint = Text("arbitraryEntry");
    EXPECT_EQ(compiler.compile(compiler.userData, &request, &result), BL_OK);
    compiler.release(compiler.userData, &result);
}

TEST(LiteComputeCompiler, NonfiniteConstantsFailBeforeBackendCompilation)
{
    auto compiler=bl_computeShaderCompilerService();
    const bl_ComputeShaderCompileRequest request{sizeof(request),BL_COMPUTE_SHADER_VERSION,
        BL_RENDERER_D3D11,12,
        Text("const invalid:f32=1.0/0.0; @compute @workgroup_size(1) fn main(){}"),
        Text("main"),Text("nonfinite-constant")};
    bl_ComputeShaderCompileResult result{};
    EXPECT_EQ(compiler.compile(compiler.userData,&request,&result),BL_SHADER_ERROR);
    EXPECT_GT(result.diagnostics.length,0u);
    EXPECT_EQ(result.container.count,0u);
    compiler.release(compiler.userData,&result);
    EXPECT_EQ(result.allocation,nullptr);
}

TEST(LiteComputeCompiler, FiveSixteenByteInstanceSlotsPreserveColorReflection)
{
    auto compiler = bl_shaderCompilerService2();
    const char* vertex =
        "struct Input{@location(0) position:vec3f,@location(1) world0:vec4f,"
        "@location(2) world1:vec4f,@location(3) world2:vec4f,@location(4) world3:vec4f,"
        "@location(5) instanceColor:vec4f};"
        "struct Out{@builtin(position) position:vec4f,@location(0) color:vec4f};"
        "@vertex fn mainVertex(i:Input)->Out{var o:Out;"
        "o.position=mat4x4f(i.world0,i.world1,i.world2,i.world3)*vec4f(i.position,1);"
        "o.color=i.instanceColor;return o;}";
    const char* fragment =
        "@fragment fn mainFragment(@location(0) color:vec4f)->@location(0) vec4f{return color;}";
    bl_InstanceAttributeDeclaration instances[] = {
        {Text("world0"),1,0,0,80}, {Text("world1"),2,1,16,80}, {Text("world2"),3,2,32,80},
        {Text("world3"),4,3,48,80}, {Text("instanceColor"),5,4,64,80}};
    bl_ShaderCompileRequest2 request{sizeof(request), BL_SHADER_COMPILE_VERSION_2,
        {BL_CONTRACT_VERSION,BL_RENDERER_D3D11,12,
            {BL_STAGE_VERTEX,Text(vertex),Text("mainVertex")},
            {BL_STAGE_FRAGMENT,Text(fragment),Text("mainFragment")},false,false,{}}, instances,5};
    bl_ShaderCompileResult2 result{};
    ASSERT_EQ(compiler.compile(compiler.userData,&request,&result),BL_OK);
    ASSERT_EQ(result.instanceAttributeCount,5u);
    EXPECT_EQ(result.instanceAttributes[4].matrixColumn,4u);
    EXPECT_EQ(result.instanceAttributes[4].byteOffset,64u);
    EXPECT_EQ(result.instanceAttributes[4].byteStride,80u);
    compiler.release(compiler.userData,&result);
    instances[4].byteStride=64;
    EXPECT_EQ(compiler.compile(compiler.userData,&request,&result),BL_INVALID_ARGUMENT);
    compiler.release(compiler.userData,&result);
}
