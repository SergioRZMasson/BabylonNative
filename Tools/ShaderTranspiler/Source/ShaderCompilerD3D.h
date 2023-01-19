#pragma once

#include "ShaderCompiler.h"
#include "ShaderCompilerCommon.h"
#include "ShaderCompilerTraversers.h"
#include "ResourceLimits.h"
#include <arcana/experimental/array.h>
#include <bgfx/bgfx.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_parser.hpp>
#include <spirv_hlsl.hpp>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <iostream>
#include <filesystem>

static std::string headerContent = "void HEADER_NAME() { return; }";

class BabylonIncluder : public glslang::TShader::Includer
{
public:
    BabylonIncluder(std::filesystem::path rootIncludePath)
        : m_rootIncludePath{rootIncludePath}
    {

    }

    IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t inclusionDepth) override 
    { 
        std::cout << "PARNSING SYSTEM INCLUDE \n"
                  << "HEADER NAME: " << headerName << std::endl
                  << "INCLUDE NAME " << includerName << std::endl
                  << "INCLUSION DEPTH " << inclusionDepth << std::endl;

        return new IncludeResult{headerName, headerContent.c_str(), headerContent.size(), nullptr};
    }

    virtual IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t inclusionDepth) override 
    { 
        std::cout << "PARNSING LOCAL INCLUDE \n"
                  << "HEADER NAME: " << headerName << std::endl
                  << "INCLUDE NAME " << includerName << std::endl
                  << "INCLUSION DEPTH " << inclusionDepth << std::endl;

        return new IncludeResult{headerName, headerContent.c_str(), headerContent.size(), nullptr};
    }

    void releaseInclude(IncludeResult* result) override 
    {
        delete result;
    }

private:
    std::filesystem::path m_rootIncludePath;
};


namespace Babylon
{
    namespace
    {
        void AddShader(glslang::TProgram& program, glslang::TShader& shader, BabylonIncluder& includer, std::string_view source)
        {
            const std::array<const char*, 1> sources{source.data()};
            shader.setStrings(sources.data(), gsl::narrow_cast<int>(sources.size()));

            if (!shader.parse(&DefaultTBuiltInResource, 310, EProfile::EEsProfile, true, true, EShMsgDefault, includer))
            {
                throw std::runtime_error{shader.getInfoLog()};
            }

            program.addShader(&shader);
        }

        std::pair<std::unique_ptr<spirv_cross::Parser>, std::unique_ptr<spirv_cross::Compiler>> CompileShader(glslang::TProgram& program, EShLanguage stage, gsl::span<const spirv_cross::HLSLVertexAttributeRemap> attributes, ID3DBlob** blob)
        {
            std::vector<uint32_t> spirv;
            glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);

            auto parser = std::make_unique<spirv_cross::Parser>(std::move(spirv));
            parser->parse();

            auto compiler = std::make_unique<spirv_cross::CompilerHLSL>(parser->get_parsed_ir());

            compiler->set_hlsl_options({40, true});

            for (const auto& attribute : attributes)
            {
                compiler->add_vertex_attribute_remap(attribute);
            }

            std::string hlsl = compiler->compile();

            Microsoft::WRL::ComPtr<ID3DBlob> errorMsgs;
            const char* target = stage == EShLangVertex ? "vs_4_0" : "ps_4_0";

            UINT flags = 0;

#ifdef _DEBUG
            flags |= D3DCOMPILE_DEBUG;
#endif

            if (FAILED(D3DCompile(hlsl.data(), hlsl.size(), nullptr, nullptr, nullptr, "main", target, flags, 0, blob, &errorMsgs)))
            {
                throw std::runtime_error{static_cast<const char*>(errorMsgs->GetBufferPointer())};
            }

            return{std::move(parser), std::move(compiler)};
        }
    }

    ShaderTranspiler::ShaderTranspiler()
    {
        glslang::InitializeProcess();
    }

    ShaderTranspiler::~ShaderTranspiler()
    {
        glslang::FinalizeProcess();
    }

    ShaderTranspiler::BgfxShaderInfo ShaderTranspiler::Compile(std::string_view vertexSource, std::string_view fragmentSource, std::string_view rootIncludePath)
    {
        glslang::TProgram program;
        BabylonIncluder includer{rootIncludePath};
        ShaderProcessor preprocessor{};

        std::string vertexString{vertexSource.cbegin(), vertexSource.cend()};
        preprocessor.attributeProcessor(vertexString);
        preprocessor.varyingProcessor(vertexString, false);

        std::string fragmentString{fragmentSource.cbegin(), fragmentSource.cend()};
        preprocessor.attributeProcessor(fragmentString);
        preprocessor.varyingProcessor(fragmentString, false);

        glslang::TShader vertexShader{EShLangVertex};
        AddShader(program, vertexShader, includer, vertexString);

        glslang::TShader fragmentShader{EShLangFragment};
        AddShader(program, fragmentShader, includer, fragmentString);

        glslang::SpvVersion spv{};
        spv.spv = 0x10000;
        vertexShader.getIntermediate()->setSpv(spv);
        fragmentShader.getIntermediate()->setSpv(spv);

        if (!program.link(EShMsgDefault))
        {
            throw std::runtime_error{program.getInfoLog()};
        }

        ShaderCompilerTraversers::IdGenerator ids{};
        auto cutScope = ShaderCompilerTraversers::ChangeUniformTypes(program, ids);
        auto utstScope = ShaderCompilerTraversers::MoveNonSamplerUniformsIntoStruct(program, ids);
        std::unordered_map<std::string, std::string> vertexAttributeRenaming = {};
        ShaderCompilerTraversers::AssignLocationsAndNamesToVertexVaryingsD3D(program, ids, vertexAttributeRenaming);
        ShaderCompilerTraversers::SplitSamplersIntoSamplersAndTextures(program, ids);
        ShaderCompilerTraversers::InvertYDerivativeOperands(program);

        // clang-format off
        static const spirv_cross::HLSLVertexAttributeRemap attributes[] = {
            {bgfx::Attrib::Position,  "POSITION"    },
            {bgfx::Attrib::Normal,    "NORMAL"      },
            {bgfx::Attrib::Tangent,   "TANGENT"     },
            {bgfx::Attrib::Color0,    "COLOR"       },
            {bgfx::Attrib::Indices,   "BLENDINDICES"},
            {bgfx::Attrib::Weight,    "BLENDWEIGHT" },
            {bgfx::Attrib::TexCoord0, "TEXCOORD0"   },
            {bgfx::Attrib::TexCoord1, "TEXCOORD1"   },
            {bgfx::Attrib::TexCoord2, "TEXCOORD2"   },
            {bgfx::Attrib::TexCoord3, "TEXCOORD3"   },
            {bgfx::Attrib::TexCoord4, "TEXCOORD4"   },
            {bgfx::Attrib::TexCoord5, "TEXCOORD5"   },
            {bgfx::Attrib::TexCoord6, "TEXCOORD6"   },
            {bgfx::Attrib::TexCoord7, "TEXCOORD7"   },
        };
        // clang-format on

        Microsoft::WRL::ComPtr<ID3DBlob> vertexBlob;
        auto [vertexParser, vertexCompiler] = CompileShader(program, EShLangVertex, attributes, &vertexBlob);
        ShaderCompilerCommon::ShaderInfo vertexShaderInfo{
            std::move(vertexParser),
            std::move(vertexCompiler),
            gsl::make_span(static_cast<uint8_t*>(vertexBlob->GetBufferPointer()), vertexBlob->GetBufferSize()),
            std::move(vertexAttributeRenaming)};

        Microsoft::WRL::ComPtr<ID3DBlob> fragmentBlob;
        auto [fragmentParser, fragmentCompiler] = CompileShader(program, EShLangFragment, {}, &fragmentBlob);
        ShaderCompilerCommon::ShaderInfo fragmentShaderInfo{
            std::move(fragmentParser),
            std::move(fragmentCompiler),
            gsl::make_span(static_cast<uint8_t*>(fragmentBlob->GetBufferPointer()), fragmentBlob->GetBufferSize()),
            {}};

        return ShaderCompilerCommon::CreateBgfxShader(std::move(vertexShaderInfo), std::move(fragmentShaderInfo));
    }
}
