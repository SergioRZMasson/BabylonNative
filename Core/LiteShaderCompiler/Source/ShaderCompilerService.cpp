#include <babylon_lite_shader_compiler.h>
#include "CompilerBackend.h"

#include <bgfx/bgfx.h>
#include <map>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using namespace Babylon::LiteShaderCompiler;

    bl_String String(const std::string& value)
    {
        return {value.data(), value.size()};
    }

    std::string_view View(bl_String value)
    {
        if ((!value.data && value.length) || !value.length ||
            (value.data && std::string_view(value.data, value.length).find('\0') != std::string_view::npos))
        {
            throw std::invalid_argument("Shader strings must be nonempty UTF-8 spans without embedded NUL.");
        }
        return {value.data, value.length};
    }

    struct Storage
    {
        Program program;
        std::vector<bl_ReflectedAttribute> attributes;
        std::vector<bl_ReflectedUniformBlock> blocks;
        std::vector<bl_ReflectedUniform> uniforms;
        std::vector<bl_ReflectedTexture> textures;
        std::string diagnostics;
    };

    bl_VertexSemantic Semantic(uint32_t semantic)
    {
        switch (semantic)
        {
            case bgfx::Attrib::Position:
                return BL_ATTRIBUTE_POSITION;
            case bgfx::Attrib::Normal:
                return BL_ATTRIBUTE_NORMAL;
            case bgfx::Attrib::TexCoord0:
                return BL_ATTRIBUTE_UV;
            case bgfx::Attrib::TexCoord1:
                return BL_ATTRIBUTE_UV2;
            case bgfx::Attrib::Tangent:
                return BL_ATTRIBUTE_TANGENT;
            case bgfx::Attrib::Color0:
                return BL_ATTRIBUTE_COLOR;
            default:
                throw std::runtime_error("Vertex semantic is outside the Babylon Lite reflection contract.");
        }
    }

    void Reflect(Storage& storage)
    {
        using Binding = std::pair<uint32_t, uint32_t>;
        std::map<Binding, uint32_t> blockIndices;
        std::map<Binding, uint32_t> textureIndices;
        for (const auto& attribute : storage.program.vertex.attributes)
        {
            if (attribute.components < 2 || attribute.components > 4)
            {
                throw std::runtime_error("Vertex attribute must be a canonical vec2/vec3/vec4 stream.");
            }
            storage.attributes.push_back({String(attribute.name), attribute.location,
                Semantic(attribute.semantic), attribute.components});
        }
        const auto reflectStage = [&](const Stage& stage, uint32_t stages) {
            for (const auto& block : stage.blocks)
            {
                const Binding binding{block.group, block.binding};
                if (blockIndices.contains(binding))
                {
                    const uint32_t index = blockIndices.at(binding);
                    auto& previous = storage.blocks[index];
                    if (previous.byteSize != block.byteSize ||
                        std::string_view(previous.name.data, previous.name.length) != block.name)
                    {
                        throw std::runtime_error("WGSL stages disagree about a shared uniform block layout.");
                    }
                    previous.stages |= stages;
                    size_t memberIndex = 0;
                    for (auto& uniform : storage.uniforms)
                    {
                        if (uniform.blockIndex != index)
                        {
                            continue;
                        }
                        if (memberIndex >= block.members.size())
                        {
                            throw std::runtime_error("WGSL stages disagree about uniform block members.");
                        }
                        const auto& member = block.members[memberIndex++];
                        if (uniform.byteOffset != member.byteOffset || uniform.byteSize != member.byteSize ||
                            static_cast<uint32_t>(uniform.type) != member.type ||
                            std::string_view(uniform.name.data, uniform.name.length) != member.name ||
                            uniform.nativeCount != block.registerCount)
                        {
                            throw std::runtime_error("WGSL stages disagree about uniform block members.");
                        }
                        uniform.stages |= stages;
                    }
                    if (memberIndex != block.members.size())
                    {
                        throw std::runtime_error("WGSL stages disagree about uniform block members.");
                    }
                    continue;
                }

                const uint32_t index = static_cast<uint32_t>(storage.blocks.size());
                blockIndices[binding] = index;
                storage.blocks.push_back({String(block.name), block.group, block.binding, block.byteSize, stages});
                for (const auto& member : block.members)
                {
                    storage.uniforms.push_back({String(member.name), index, member.byteOffset, member.byteSize,
                        static_cast<bl_ShaderUniformType>(member.type), stages, String(block.uniformName),
                        BL_NATIVE_UNIFORM_VEC4, static_cast<uint16_t>(block.registerCount), member.byteOffset});
                }
            }
            for (const auto& texture : stage.textures)
            {
                const Binding binding{texture.group, texture.binding};
                if (textureIndices.contains(binding))
                {
                    auto& previous = storage.textures[textureIndices.at(binding)];
                    if (previous.samplerGroup != texture.samplerGroup || previous.samplerBinding != texture.samplerBinding ||
                        previous.nativeTextureStage != texture.stage)
                    {
                        throw std::runtime_error("WGSL stages disagree about a shared texture/sampler pairing.");
                    }
                    previous.stages |= stages;
                    continue;
                }
                textureIndices[binding] = static_cast<uint32_t>(storage.textures.size());
                storage.textures.push_back({String(texture.name), String(texture.samplerName), texture.group, texture.binding,
                    texture.samplerGroup, texture.samplerBinding, stages, String(texture.uniformName),
                    static_cast<uint8_t>(texture.stage)});
            }
        };
        reflectStage(storage.program.vertex, BL_STAGE_VERTEX);
        reflectStage(storage.program.fragment, BL_STAGE_FRAGMENT);
        storage.diagnostics = storage.program.vertex.diagnostics + storage.program.fragment.diagnostics;
    }

    bl_Status Diagnostic(bl_ShaderCompileResult& result, bl_Status status, const char* message)
    {
        try
        {
            auto storage = std::make_unique<Storage>();
            storage->diagnostics = message;
            result.diagnostics = String(storage->diagnostics);
            result.allocation = storage.release();
        }
        catch (const std::bad_alloc&)
        {
            static const char fallback[] = "Runtime shader compiler ran out of memory.";
            result.diagnostics = {fallback, sizeof(fallback) - 1};
            status = BL_OUT_OF_MEMORY;
        }
        return status;
    }

    bl_Status Compile(void*, const bl_ShaderCompileRequest* request, bl_ShaderCompileResult* result)
    {
        if (!result)
        {
            return BL_INVALID_ARGUMENT;
        }
        *result = {};
        if (!request || request->contractVersion != BL_CONTRACT_VERSION ||
            request->vertex.stage != BL_STAGE_VERTEX || request->fragment.stage != BL_STAGE_FRAGMENT)
        {
            return Diagnostic(*result, BL_INVALID_ARGUMENT, "Invalid shader compile request or contract version.");
        }
        if (request->backend != BL_RENDERER_D3D11 || request->bgfxShaderBinaryVersion != 12)
        {
            return Diagnostic(*result, BL_UNSUPPORTED, "This runtime compiler supports D3D11 bgfx container v12 only.");
        }
        if (request->homogeneousDepth || request->originBottomLeft)
        {
            return Diagnostic(*result, BL_INVALID_ARGUMENT, "D3D11 requires [0,1] depth and a top-left render target origin.");
        }
        try
        {
            auto storage = std::make_unique<Storage>();
            storage->program = CompileD3D11(View(request->vertex.wgsl), View(request->vertex.entryPoint),
                View(request->fragment.wgsl), View(request->fragment.entryPoint));
            Reflect(*storage);
            result->vertexContainer = {storage->program.vertex.bytes.data(), storage->program.vertex.bytes.size()};
            result->fragmentContainer = {storage->program.fragment.bytes.data(), storage->program.fragment.bytes.size()};
            result->attributes = storage->attributes.data();
            result->attributeCount = storage->attributes.size();
            result->uniformBlocks = storage->blocks.data();
            result->uniformBlockCount = storage->blocks.size();
            result->uniforms = storage->uniforms.data();
            result->uniformCount = storage->uniforms.size();
            result->textures = storage->textures.data();
            result->textureCount = storage->textures.size();
            result->diagnostics = String(storage->diagnostics);
            result->allocation = storage.release();
            return BL_OK;
        }
        catch (const std::invalid_argument& error)
        {
            return Diagnostic(*result, BL_INVALID_ARGUMENT, error.what());
        }
        catch (const std::bad_alloc&)
        {
            return Diagnostic(*result, BL_OUT_OF_MEMORY, "Runtime shader compiler ran out of memory.");
        }
        catch (const std::exception& error)
        {
            return Diagnostic(*result, BL_SHADER_ERROR, error.what());
        }
        catch (...)
        {
            return Diagnostic(*result, BL_SHADER_ERROR, "Unexpected runtime shader compiler failure.");
        }
    }

    void Release(void*, bl_ShaderCompileResult* result)
    {
        if (result)
        {
            delete static_cast<Storage*>(result->allocation);
            *result = {};
        }
    }
}

extern "C" bl_ShaderCompilerService bl_shaderCompilerService(void)
{
    return {nullptr, Compile, Release};
}
