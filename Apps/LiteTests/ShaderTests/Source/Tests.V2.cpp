#include <babylon_lite_shader_compiler.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <cstring>

TEST(LiteShaderCompiler2, ActiveAndDeadInstancesKeepOwnedNamesUntilRelease)
{
    const char* fragments = R"(
@fragment fn main()->@location(0) vec4f {return vec4f(1);}
)";
    const char* vertices[] = {R"(
@vertex fn main(@location(0) position:vec3f,@location(1) world0:vec4f,
@location(2) world1:vec4f,@location(3) world2:vec4f,@location(4) world3:vec4f)
->@builtin(position) vec4f {
return mat4x4f(world0,world1,world2,world3)*vec4f(position,1);
})", R"(
@vertex fn main(@location(0) position:vec3f,@location(1) world0:vec4f,
@location(2) world1:vec4f,@location(3) world2:vec4f,@location(4) world3:vec4f)
->@builtin(position) vec4f {return vec4f(position,1)+world0;}
)", R"(
@vertex fn main(@location(0) position:vec3f,@location(1) world0:vec4f,
@location(2) world1:vec4f,@location(3) world2:vec4f,@location(4) world3:vec4f)
->@builtin(position) vec4f {return vec4f(position,1);}
)"};
    const size_t active[] = {4,1,0};
    for (size_t test=0;test<3;++test)
    {
        std::vector<std::string> names{"world0","world1","world2","world3"};
        bl_InstanceAttributeDeclaration columns[4]{};
        for(uint32_t i=0;i<4;++i)
            columns[i]={{names[i].data(),names[i].size()},i+1,i,i*16,64};
        bl_ShaderCompileRequest2 request{};
        request.structSize=sizeof(request); request.version=2;
        request.base.contractVersion=1;request.base.backend=BL_RENDERER_D3D11;
        request.base.bgfxShaderBinaryVersion=12;
        request.base.vertex={BL_STAGE_VERTEX,{vertices[test],strlen(vertices[test])},{"main",4}};
        request.base.fragment={BL_STAGE_FRAGMENT,{fragments,strlen(fragments)},{"main",4}};
        request.instanceAttributes=columns;request.instanceAttributeCount=4;
        auto service=bl_shaderCompilerService2();
        bl_ShaderCompileResult2 result{};
        const auto status=service.compile(service.userData,&request,&result);
        ASSERT_EQ(status,BL_OK)<<std::string(result.base.diagnostics.data,result.base.diagnostics.length);
        names.clear();
        // Reuse freed small-string storage aggressively; result strings must belong to its allocation.
        std::vector<std::string> churn(1024,std::string(80,'x'));
        ASSERT_EQ(result.instanceAttributeCount,active[test]);
        for(size_t i=0;i<result.instanceAttributeCount;++i)
        {
            const auto& attribute=result.instanceAttributes[i];
            EXPECT_EQ(std::string(attribute.name.data,attribute.name.length),"world"+std::to_string(attribute.matrixColumn));
            EXPECT_EQ(attribute.components,4);
            EXPECT_EQ(attribute.byteStride,64u);
            EXPECT_EQ(attribute.byteOffset,attribute.matrixColumn*16);
        }
        service.release(service.userData,&result);
        EXPECT_EQ(result.base.allocation,nullptr);
    }
}

TEST(LiteShaderCompiler2, RawTextureTypesAndOperationsComeFromActiveIR)
{
    const char* vertex=R"(
@vertex fn main(@builtin(vertex_index) index:u32)->@builtin(position) vec4f {
return vec4f(f32(index),0,0,1);
})";
    const char* fragment=R"(
@group(0) @binding(0) var signedImage:texture_2d<i32>;
@group(0) @binding(1) var unsignedImage:texture_2d<u32>;
@group(0) @binding(2) var unusedImage:texture_2d<f32>;
@fragment fn main()->@location(0) vec4f {
return vec4f(textureLoad(signedImage,vec2u(0),0))+
       vec4f(textureLoad(unsignedImage,vec2u(0),0));
})";
    bl_ShaderCompileRequest2 request{};
    request.structSize=sizeof(request);request.version=2;
    request.base.contractVersion=1;request.base.backend=BL_RENDERER_D3D11;
    request.base.bgfxShaderBinaryVersion=12;
    request.base.vertex={BL_STAGE_VERTEX,{vertex,strlen(vertex)},{"main",4}};
    request.base.fragment={BL_STAGE_FRAGMENT,{fragment,strlen(fragment)},{"main",4}};
    auto service=bl_shaderCompilerService2();
    bl_ShaderCompileResult2 result{};
    const auto status=service.compile(service.userData,&request,&result);
    ASSERT_EQ(status,BL_OK)<<std::string(result.base.diagnostics.data,result.base.diagnostics.length);
    ASSERT_EQ(result.textureInfoCount,2u);
    bool signedType=false,unsignedType=false;
    for(size_t i=0;i<result.textureInfoCount;++i)
    {
        const auto& texture=result.textureInfo[i];
        signedType|=texture.sampleType==BL_SHADER_TEXTURE_SINT;
        unsignedType|=texture.sampleType==BL_SHADER_TEXTURE_UINT;
        EXPECT_EQ(texture.samplerKind,BL_SHADER_SAMPLER_NONE);
        EXPECT_NE(texture.samplingOperations & BL_SHADER_TEXTURE_USE_LOAD,0u);
        EXPECT_EQ(texture.samplingOperations &
            ~(BL_SHADER_TEXTURE_USE_LOAD | BL_SHADER_TEXTURE_USE_DIMENSIONS),0u);
        EXPECT_EQ(texture.binding.samplerGroup,UINT32_MAX);
        EXPECT_EQ(texture.binding.samplerBinding,UINT32_MAX);
    }
    EXPECT_TRUE(signedType && unsignedType);
    service.release(service.userData,&result);
}
