#include "CompilerBackend.h"

#include <src/tint/api/tint.h>
#include <src/tint/lang/wgsl/inspector/inspector.h>
#include <src/tint/lang/wgsl/reader/reader.h>
#include <src/tint/lang/wgsl/sem/struct.h>
#include <src/tint/lang/wgsl/sem/variable.h>
#include <src/tint/lang/wgsl/ast/module.h>
#include <src/tint/lang/core/type/f32.h>
#include <src/tint/lang/core/type/u32.h>
#include <src/tint/lang/core/type/i32.h>
#include <src/tint/lang/core/type/vector.h>
#include <src/tint/lang/core/type/matrix.h>
#include <src/tint/lang/core/type/array.h>
#include <src/tint/lang/core/type/struct.h>
#include <src/tint/lang/spirv/writer/writer.h>
#include <spirv_hlsl.hpp>
#include <bgfx/bgfx.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>

namespace bgfx
{
    uint16_t attribToId(Attrib::Enum attribute);
}

namespace Babylon::LiteShaderCompiler
{
    namespace
    {
        using Resource = tint::inspector::ResourceBinding;
        using Binding = std::pair<uint32_t, uint32_t>;

        uint32_t Align16(uint32_t size)
        {
            return (size + 15u) & ~15u;
        }

        Binding BindingOf(const spirv_cross::Compiler& compiler, uint32_t id)
        {
            return {compiler.get_decoration(id, spv::DecorationDescriptorSet),
                compiler.get_decoration(id, spv::DecorationBinding)};
        }

        std::string UniformName(const Resource& resource)
        {
            return "bl_g" + std::to_string(resource.bind_group) + "_b" +
                   std::to_string(resource.binding) + "_" + resource.variable_name;
        }

        void Instruction(std::vector<uint32_t>& words, spv::Op opcode, std::initializer_list<uint32_t> operands)
        {
            words.push_back((static_cast<uint32_t>(operands.size() + 1) << 16) | static_cast<uint32_t>(opcode));
            words.insert(words.end(), operands);
        }

        void Name(std::vector<uint32_t>& words, spv::Op opcode, std::initializer_list<uint32_t> operands, const std::string& name)
        {
            const size_t stringWords = (name.size() + 4) / 4;
            words.push_back((static_cast<uint32_t>(operands.size() + stringWords + 1) << 16) | static_cast<uint32_t>(opcode));
            words.insert(words.end(), operands);
            const size_t start = words.size();
            words.resize(start + stringWords, 0);
            std::memcpy(words.data() + start, name.data(), name.size());
        }

        const Resource& FindResource(const std::vector<Resource>& resources, Binding binding)
        {
            const auto found = std::find_if(resources.begin(), resources.end(), [&](const Resource& resource) {
                return Binding{resource.bind_group, resource.binding} == binding;
            });
            if (found == resources.end())
            {
                throw std::runtime_error("Compiler reflection lost a WGSL resource binding.");
            }
            return *found;
        }

        uint32_t UniformType(const tint::core::type::Type* type)
        {
            if (type->Is<tint::core::type::F32>())
                return 0;
            if (type->Is<tint::core::type::U32>())
                return 1;
            if (type->Is<tint::core::type::I32>())
                return 2;
            if (const auto* vector = type->As<tint::core::type::Vector>();
                vector && vector->Type()->Is<tint::core::type::F32>())
            {
                return vector->Width() + 1;
            }
            if (const auto* matrix = type->As<tint::core::type::Matrix>();
                matrix && matrix->Columns() == 4 && matrix->Rows() == 4 &&
                matrix->Type()->Is<tint::core::type::F32>())
            {
                return 6;
            }
            throw std::runtime_error("WGSL uniform type is outside the scalar/vector/mat4 reflection contract.");
        }

        uint32_t CheckedOffset(uint32_t base, uint64_t offset, uint32_t blockSize)
        {
            const uint64_t absolute = static_cast<uint64_t>(base) + offset;
            if (absolute > blockSize || absolute > (std::numeric_limits<uint32_t>::max)())
            {
                throw std::runtime_error("WGSL semantic uniform layout offset overflow.");
            }
            return static_cast<uint32_t>(absolute);
        }

        void ReflectLeaves(const tint::core::type::Type* type, const std::string& path,
            uint32_t offset, uint32_t blockSize, uint32_t depth, std::vector<UniformMember>& result)
        {
            if (depth > 64 || result.size() >= 65520u / 4u || type->Size() > blockSize - offset)
            {
                throw std::runtime_error("WGSL semantic uniform layout exceeds reflection limits.");
            }
            if (const auto* structure = type->As<tint::core::type::Struct>())
            {
                for (const auto* member : structure->Members())
                {
                    const std::string name = path.empty() ? member->Name().Name()
                                                          : path + "." + member->Name().Name();
                    ReflectLeaves(member->Type(), name, CheckedOffset(offset, member->Offset(), blockSize),
                        blockSize, depth + 1, result);
                }
                return;
            }
            if (const auto* array = type->As<tint::core::type::Array>())
            {
                const auto count = array->ConstantCount();
                if (!count || !*count)
                {
                    throw std::runtime_error("WGSL uniform reflection requires fixed-size arrays.");
                }
                const uint32_t stride = array->ImplicitStride();
                if (stride < array->ElemType()->Size() ||
                    static_cast<uint64_t>(stride) * *count > array->Size())
                {
                    throw std::runtime_error("WGSL uniform array semantic stride is inconsistent.");
                }
                for (uint32_t i = 0; i < *count; ++i)
                {
                    ReflectLeaves(array->ElemType(), path + "[" + std::to_string(i) + "]",
                        CheckedOffset(offset, static_cast<uint64_t>(stride) * i, blockSize),
                        blockSize, depth + 1, result);
                }
                return;
            }
            if (const auto* vector = type->As<tint::core::type::Vector>();
                vector && (vector->Type()->Is<tint::core::type::U32>() ||
                           vector->Type()->Is<tint::core::type::I32>()))
            {
                for (uint32_t i = 0; i < vector->Width(); ++i)
                {
                    ReflectLeaves(vector->Type(), path + "[" + std::to_string(i) + "]",
                        CheckedOffset(offset, static_cast<uint64_t>(4) * i, blockSize),
                        blockSize, depth + 1, result);
                }
                return;
            }
            const uint32_t kind = UniformType(type);
            const uint32_t size = type->Size();
            for (const auto& leaf : result)
            {
                if (leaf.name == path || (offset < leaf.byteOffset + leaf.byteSize &&
                                          leaf.byteOffset < offset + size))
                {
                    throw std::runtime_error("WGSL semantic uniform leaf alias/overlap.");
                }
            }
            result.push_back({path, offset, size, kind});
        }

        std::vector<UniformMember> ReflectMembers(const tint::Program& program, Binding binding)
        {
            for (const auto* variable : program.AST().GlobalVariables())
            {
                const auto* global = program.Sem().Get<tint::sem::GlobalVariable>(variable);
                if (!global || !global->Attributes().binding_point)
                {
                    continue;
                }
                const auto point = *global->Attributes().binding_point;
                if (Binding{point.group, point.binding} != binding)
                {
                    continue;
                }
                const auto* type = global->Type()->UnwrapRef()->As<tint::sem::Struct>();
                if (!type)
                {
                    throw std::runtime_error("WGSL uniform bindings must be declared as structs.");
                }
                std::vector<UniformMember> result;
                ReflectLeaves(type, "", 0, type->Size(), 0, result);
                return result;
            }
            throw std::runtime_error("WGSL uniform binding has no semantic declaration.");
        }

        struct MergedBlock
        {
            uint32_t variable{};
            uint32_t pointerType{};
            uint32_t structType{};
            uint32_t indexConstant{};
        };

        // bgfx's public D3D11 API uploads one b0 per stage. Consolidate the
        // compiler's raw UBO structs in SPIR-V, retaining their byte layouts.
        std::vector<uint32_t> MergeUniformBlocks(std::vector<uint32_t> words,
            const std::vector<Resource>& sourceResources, std::vector<UniformBlock>& reflection)
        {
            spirv_cross::Compiler compiler(words);
            const auto resources = compiler.get_shader_resources();
            if (resources.uniform_buffers.empty())
            {
                return words;
            }

            std::vector<MergedBlock> blocks;
            uint32_t totalSize = 0;
            uint32_t nextId = words[3];
            uint32_t unsignedType = 0;
            for (size_t i = 5; i < words.size(); i += words[i] >> 16)
            {
                if ((words[i] & 0xffff) == spv::OpTypeInt && words[i + 2] == 32 && words[i + 3] == 0)
                {
                    unsignedType = words[i + 1];
                }
            }

            std::vector<uint32_t> declarations;
            if (!unsignedType)
            {
                unsignedType = nextId++;
                Instruction(declarations, spv::OpTypeInt, {unsignedType, 32, 0});
            }

            for (const auto& block : resources.uniform_buffers)
            {
                const auto& source = FindResource(sourceResources, BindingOf(compiler, block.id));
                const auto& type = compiler.get_type(block.base_type_id);
                const uint32_t byteSize = static_cast<uint32_t>(compiler.get_declared_struct_size(type));
                const uint32_t paddedSize = Align16(byteSize);
                if (paddedSize > 255u * 16u || totalSize + paddedSize > 65520u)
                {
                    throw std::runtime_error("WGSL uniform block exceeds the bgfx D3D11 uniform-array limit.");
                }

                reflection.push_back({source.variable_name, UniformName(source) + "_v" + std::to_string(paddedSize / 16u), source.bind_group,
                    source.binding, Align16(static_cast<uint32_t>(source.size)), totalSize, paddedSize / 16u, {}});
                blocks.push_back({block.id, block.type_id, block.base_type_id, nextId++});
                Instruction(declarations, spv::OpConstant,
                    {unsignedType, blocks.back().indexConstant, static_cast<uint32_t>(blocks.size() - 1)});
                totalSize += paddedSize;
            }

            const uint32_t mergedType = nextId++;
            const uint32_t mergedPointer = nextId++;
            const uint32_t mergedVariable = nextId++;
            declarations.push_back((static_cast<uint32_t>(blocks.size() + 2) << 16) | spv::OpTypeStruct);
            declarations.push_back(mergedType);
            for (const auto& block : blocks)
            {
                declarations.push_back(block.structType);
            }
            Instruction(declarations, spv::OpTypePointer, {mergedPointer, spv::StorageClassUniform, mergedType});
            Instruction(declarations, spv::OpVariable, {mergedPointer, mergedVariable, spv::StorageClassUniform});

            std::vector<uint32_t> debug;
            Name(debug, spv::OpName, {mergedType}, "BabylonLiteUniforms");
            Name(debug, spv::OpName, {mergedVariable}, "BabylonLiteUniforms");
            std::vector<uint32_t> annotations;
            Instruction(annotations, spv::OpDecorate, {mergedType, spv::DecorationBlock});
            Instruction(annotations, spv::OpDecorate, {mergedVariable, spv::DecorationDescriptorSet, 0});
            Instruction(annotations, spv::OpDecorate, {mergedVariable, spv::DecorationBinding, 0});
            for (uint32_t i = 0; i < blocks.size(); ++i)
            {
                Name(debug, spv::OpMemberName, {mergedType, i}, reflection[i].name);
                Instruction(annotations, spv::OpMemberDecorate, {mergedType, i, spv::DecorationOffset, reflection[i].byteOffset});
            }

            const auto findBlock = [&](uint32_t id) -> const MergedBlock* {
                const auto found = std::find_if(blocks.begin(), blocks.end(), [id](const auto& block) {
                    return block.variable == id;
                });
                return found == blocks.end() ? nullptr : &*found;
            };

            std::vector<uint32_t> result(words.begin(), words.begin() + 5);
            bool debugInserted = false;
            bool annotationsInserted = false;
            bool declarationsInserted = false;
            for (size_t i = 5; i < words.size();)
            {
                const uint32_t count = words[i] >> 16;
                const auto opcode = static_cast<spv::Op>(words[i] & 0xffff);
                const bool typeInstruction = opcode >= spv::OpTypeVoid && opcode <= spv::OpTypeForwardPointer;
                if (!debugInserted && (opcode == spv::OpDecorate || opcode == spv::OpMemberDecorate || typeInstruction))
                {
                    result.insert(result.end(), debug.begin(), debug.end());
                    debugInserted = true;
                }
                if (!annotationsInserted && typeInstruction)
                {
                    result.insert(result.end(), annotations.begin(), annotations.end());
                    annotationsInserted = true;
                }
                if (!declarationsInserted && opcode == spv::OpFunction)
                {
                    result.insert(result.end(), declarations.begin(), declarations.end());
                    declarationsInserted = true;
                }

                bool skip = (opcode == spv::OpName || opcode == spv::OpDecorate) && findBlock(words[i + 1]);
                skip = skip || (opcode == spv::OpVariable && findBlock(words[i + 2]));
                if (opcode == spv::OpDecorate && words[i + 2] == spv::DecorationBlock)
                {
                    skip = skip || std::any_of(blocks.begin(), blocks.end(), [&](const auto& block) {
                        return block.structType == words[i + 1];
                    });
                }

                if ((opcode == spv::OpAccessChain || opcode == spv::OpInBoundsAccessChain) && findBlock(words[i + 3]))
                {
                    const auto& block = *findBlock(words[i + 3]);
                    result.push_back(((count + 1) << 16) | static_cast<uint32_t>(opcode));
                    result.insert(result.end(), words.begin() + i + 1, words.begin() + i + 3);
                    result.push_back(mergedVariable);
                    result.push_back(block.indexConstant);
                    result.insert(result.end(), words.begin() + i + 4, words.begin() + i + count);
                    skip = true;
                }
                else if (opcode == spv::OpLoad && findBlock(words[i + 3]))
                {
                    const auto& block = *findBlock(words[i + 3]);
                    const uint32_t pointer = nextId++;
                    Instruction(result, spv::OpAccessChain,
                        {block.pointerType, pointer, mergedVariable, block.indexConstant});
                    result.insert(result.end(), words.begin() + i, words.begin() + i + count);
                    result[result.size() - count + 3] = pointer;
                    skip = true;
                }
                else if ((opcode == spv::OpPtrAccessChain || opcode == spv::OpInBoundsPtrAccessChain ||
                             opcode == spv::OpCopyObject) &&
                         findBlock(words[i + 3]))
                {
                    throw std::runtime_error("Unsupported WGSL uniform-pointer operation in D3D11 compilation.");
                }
                else if (opcode == spv::OpEntryPoint)
                {
                    size_t firstInterface = i + 3;
                    while (firstInterface < i + count)
                    {
                        const uint32_t word = words[firstInterface++];
                        if ((word & 0xffu) == 0 || (word & 0xff00u) == 0 ||
                            (word & 0xff0000u) == 0 || (word & 0xff000000u) == 0)
                        {
                            break;
                        }
                    }
                    std::vector<uint32_t> entry(words.begin() + i, words.begin() + firstInterface);
                    bool hasUniformInterface = false;
                    for (size_t j = firstInterface; j < i + count; ++j)
                    {
                        if (findBlock(words[j]))
                        {
                            hasUniformInterface = true;
                        }
                        else
                        {
                            entry.push_back(words[j]);
                        }
                    }
                    if (hasUniformInterface)
                    {
                        entry.push_back(mergedVariable);
                    }
                    entry[0] = (static_cast<uint32_t>(entry.size()) << 16) | spv::OpEntryPoint;
                    result.insert(result.end(), entry.begin(), entry.end());
                    skip = true;
                }

                if (!skip)
                {
                    result.insert(result.end(), words.begin() + i, words.begin() + i + count);
                }
                i += count;
            }
            result[3] = nextId;
            return result;
        }

        uint32_t AttributeSemantic(const std::string& name, const std::set<uint32_t>& used)
        {
            const std::string field = name.substr(name.find_last_of('.') == std::string::npos ? 0 : name.find_last_of('.') + 1);
            const std::map<std::string, bgfx::Attrib::Enum> conventional{
                {"position", bgfx::Attrib::Position},
                {"normal", bgfx::Attrib::Normal},
                {"tangent", bgfx::Attrib::Tangent},
                {"color", bgfx::Attrib::Color0},
                {"uv", bgfx::Attrib::TexCoord0},
                {"uv2", bgfx::Attrib::TexCoord1},
                {"matricesIndices", bgfx::Attrib::Indices},
                {"matricesWeights", bgfx::Attrib::Weight},
            };
            const auto known = conventional.find(field);
            if (known != conventional.end() && !used.contains(known->second))
            {
                return known->second;
            }
            for (uint32_t slot = bgfx::Attrib::TexCoord0; slot <= bgfx::Attrib::TexCoord15; ++slot)
            {
                if (!used.contains(slot))
                {
                    return slot;
                }
            }
            throw std::runtime_error("WGSL vertex attributes exceed the supported bgfx attribute layout.");
        }

        template<typename T>
        void Append(std::vector<uint8_t>& bytes, T value)
        {
            for (size_t i = 0; i < sizeof(T); ++i)
            {
                bytes.push_back(static_cast<uint8_t>(static_cast<uint64_t>(value) >> (i * 8)));
            }
        }

        void AppendUniform(std::vector<uint8_t>& bytes, const std::string& name, uint8_t type,
            uint8_t count, uint16_t offset, uint16_t registers, uint8_t dimension = 0)
        {
            if (name.empty() || name.size() > 255)
            {
                throw std::runtime_error("WGSL resource name exceeds the bgfx shader-container limit.");
            }
            Append(bytes, static_cast<uint8_t>(name.size()));
            bytes.insert(bytes.end(), name.begin(), name.end());
            Append(bytes, type);
            Append(bytes, count);
            Append(bytes, offset);
            Append(bytes, registers);
            Append(bytes, uint8_t{0});
            Append(bytes, dimension);
            Append(bytes, uint16_t{0});
        }

        void Package(Stage& stage, ID3DBlob& bytecode, bool vertex)
        {
            auto& bytes = stage.bytes;
            bytes.insert(bytes.end(), {static_cast<uint8_t>(vertex ? 'V' : 'F'), 'S', 'H', 12});
            Append(bytes, uint32_t{0x424c4954});
            Append(bytes, uint32_t{0x424c4954});
            Append(bytes, uint32_t{0});
            Append(bytes, uint32_t{0});
            Append(bytes, static_cast<uint16_t>(stage.blocks.size() + stage.textures.size()));
            uint32_t uniformSize = 0;
            for (const auto& block : stage.blocks)
            {
                AppendUniform(bytes, block.uniformName, static_cast<uint8_t>(bgfx::UniformType::Vec4 | (vertex ? 0 : 0x10)),
                    static_cast<uint8_t>(block.registerCount), static_cast<uint16_t>(block.byteOffset),
                    static_cast<uint16_t>(block.registerCount));
                uniformSize = block.byteOffset + block.registerCount * 16;
            }
            for (const auto& texture : stage.textures)
            {
                AppendUniform(bytes, texture.uniformName, static_cast<uint8_t>(bgfx::UniformType::Sampler | 0x20),
                    1, static_cast<uint16_t>(texture.stage), 1, static_cast<uint8_t>(texture.dimension));
            }
            Append(bytes, static_cast<uint32_t>(bytecode.GetBufferSize()));
            const auto* data = static_cast<const uint8_t*>(bytecode.GetBufferPointer());
            bytes.insert(bytes.end(), data, data + bytecode.GetBufferSize());
            Append(bytes, uint8_t{0});
            Append(bytes, static_cast<uint8_t>(stage.attributes.size()));
            for (const auto& attribute : stage.attributes)
            {
                Append(bytes, bgfx::attribToId(static_cast<bgfx::Attrib::Enum>(attribute.semantic)));
            }
            Append(bytes, static_cast<uint16_t>(uniformSize));
        }

        uint32_t TextureDimension(Resource::TextureDimension dimension)
        {
            switch (dimension)
            {
                case Resource::TextureDimension::k1d:
                    return 1;
                case Resource::TextureDimension::k2d:
                    return 2;
                case Resource::TextureDimension::k2dArray:
                    return 3;
                case Resource::TextureDimension::kCube:
                    return 4;
                case Resource::TextureDimension::kCubeArray:
                    return 5;
                case Resource::TextureDimension::k3d:
                    return 6;
                default:
                    throw std::runtime_error("Unsupported WGSL texture dimension.");
            }
        }

        Stage CompileStage(std::string_view source, std::string_view entryName, bool vertex,
            std::vector<tint::inspector::StageVariable>& inputs,
            std::vector<tint::inspector::StageVariable>& outputs,
            std::map<Binding, uint32_t>& textureStages)
        {
            const char* phase = "WGSL parsing";
            try
            {
                const std::string entry(entryName);
                tint::Source::File file(vertex ? "vertex.wgsl" : "fragment.wgsl", std::string(source));
                tint::wgsl::reader::Options readerOptions;
                readerOptions.allowed_features = tint::wgsl::AllowedFeatures::Everything();
                auto program = tint::wgsl::reader::Parse(&file, readerOptions);
                if (!program.IsValid())
                {
                    throw std::runtime_error(program.Diagnostics().Str());
                }
                tint::inspector::Inspector inspector(program);
                const auto entryPoint = inspector.GetEntryPoint(entry);
                if (inspector.has_error())
                {
                    throw std::runtime_error(inspector.error());
                }
                if (entryPoint.stage != (vertex ? tint::inspector::PipelineStage::kVertex : tint::inspector::PipelineStage::kFragment))
                {
                    throw std::runtime_error("WGSL entry point has the wrong shader stage.");
                }
                inputs = entryPoint.input_variables;
                outputs = entryPoint.output_variables;
                const auto sourceResources = inspector.GetResourceBindings(entry);
                const auto samplerUses = inspector.GetSamplerTextureUses(entry);
                for (const auto& resource : sourceResources)
                {
                    using Type = Resource::ResourceType;
                    if (resource.resource_type != Type::kUniformBuffer && resource.resource_type != Type::kSampler &&
                        resource.resource_type != Type::kSampledTexture && resource.resource_type != Type::kDepthTexture)
                    {
                        throw std::runtime_error("WGSL storage, multisampled or external resources are not supported by this D3D11 service.");
                    }
                }

                phase = "Tint IR lowering";
                auto ir = tint::wgsl::reader::ProgramToLoweredIR(program);
                if (ir != tint::Success)
                {
                    throw std::runtime_error(ir.Failure().reason);
                }
                tint::spirv::writer::Options options;
                options.entry_point_name = entry;
                options.emit_vertex_point_size = false;
                phase = "Tint SPIR-V generation";
                auto generated = tint::spirv::writer::Generate(ir.Get(), options);
                if (generated != tint::Success)
                {
                    throw std::runtime_error(generated.Failure().reason);
                }

                phase = "SPIR-V uniform consolidation and reflection";
                Stage result;
                auto words = MergeUniformBlocks(std::move(generated.Get().spirv), sourceResources, result.blocks);
                for (auto& block : result.blocks)
                {
                    block.members = ReflectMembers(program, {block.group, block.binding});
                }
                spirv_cross::CompilerHLSL compiler(words);
                compiler.set_entry_point(entry, vertex ? spv::ExecutionModelVertex : spv::ExecutionModelFragment);
                compiler.set_hlsl_options({50, true});
                const auto resources = compiler.get_shader_resources();

                if (vertex)
                {
                    std::set<uint32_t> used;
                    for (const auto& input : entryPoint.input_variables)
                    {
                        if (!input.attributes.location)
                        {
                            continue;
                        }
                        const uint32_t semantic = AttributeSemantic(input.name, used);
                        used.insert(semantic);
                        const uint8_t components = input.composition_type == tint::inspector::CompositionType::kVec2 ? 2 : input.composition_type == tint::inspector::CompositionType::kVec3 ? 3
                                                                                                                       : input.composition_type == tint::inspector::CompositionType::kVec4   ? 4
                                                                                                                                                                                             : 1;
                        result.attributes.push_back({input.variable_name, *input.attributes.location, semantic, components});
                        const std::map<uint32_t, std::string> semantics{
                            {bgfx::Attrib::Position, "POSITION"},
                            {bgfx::Attrib::Normal, "NORMAL"},
                            {bgfx::Attrib::Tangent, "TANGENT"},
                            {bgfx::Attrib::Color0, "COLOR"},
                            {bgfx::Attrib::Indices, "BLENDINDICES"},
                            {bgfx::Attrib::Weight, "BLENDWEIGHT"},
                        };
                        const auto known = semantics.find(semantic);
                        const std::string hlslSemantic = known == semantics.end()
                                                             ? "TEXCOORD" + std::to_string(semantic - bgfx::Attrib::TexCoord0)
                                                             : known->second;
                        compiler.add_vertex_attribute_remap({*input.attributes.location, hlslSemantic});
                    }
                }

                std::map<Binding, uint32_t> samplerStages;
                for (const auto& image : resources.separate_images)
                {
                    const auto binding = BindingOf(compiler, image.id);
                    const auto& sourceResource = FindResource(sourceResources, binding);
                    if (!textureStages.contains(binding))
                    {
                        textureStages[binding] = static_cast<uint32_t>(textureStages.size());
                    }
                    const uint32_t stage = textureStages.at(binding);
                    if (stage >= 16)
                    {
                        throw std::runtime_error("WGSL textures exceed the D3D11 bgfx sampler-stage limit.");
                    }
                    const auto pair = std::find_if(samplerUses.begin(), samplerUses.end(), [&](const auto& use) {
                        return Binding{use.texture_binding_point.group, use.texture_binding_point.binding} == binding;
                    });
                    Binding samplerBinding{UINT32_MAX, UINT32_MAX};
                    std::string samplerName;
                    if (pair != samplerUses.end())
                    {
                        samplerBinding = {pair->sampler_binding_point.group, pair->sampler_binding_point.binding};
                        samplerName = FindResource(sourceResources, samplerBinding).variable_name;
                        const bool multipleSamplers = std::any_of(samplerUses.begin(), samplerUses.end(), [&](const auto& use) {
                            return Binding{use.texture_binding_point.group, use.texture_binding_point.binding} == binding &&
                                   Binding{use.sampler_binding_point.group, use.sampler_binding_point.binding} != samplerBinding;
                        });
                        if (multipleSamplers)
                        {
                            throw std::runtime_error("One WGSL texture sampled through multiple samplers requires unsupported binding duplication.");
                        }
                        if (!samplerStages.contains(samplerBinding))
                        {
                            samplerStages[samplerBinding] = stage;
                        }
                    }
                    result.textures.push_back({sourceResource.variable_name, samplerName, UniformName(sourceResource), binding.first, binding.second,
                        samplerBinding.first, samplerBinding.second, stage, TextureDimension(sourceResource.dim)});
                    compiler.set_decoration(image.id, spv::DecorationDescriptorSet, 0);
                    compiler.set_decoration(image.id, spv::DecorationBinding, stage);
                }
                for (const auto& sampler : resources.separate_samplers)
                {
                    const auto binding = BindingOf(compiler, sampler.id);
                    if (!samplerStages.contains(binding))
                    {
                        throw std::runtime_error("WGSL sampler has no reflected texture pairing.");
                    }
                    compiler.set_decoration(sampler.id, spv::DecorationDescriptorSet, 0);
                    compiler.set_decoration(sampler.id, spv::DecorationBinding, samplerStages.at(binding));
                }

                phase = "SPIRV-Cross HLSL generation";
                const std::string hlsl = compiler.compile();
                const std::string nativeEntry = compiler.get_cleansed_entry_point_name(entry,
                    vertex ? spv::ExecutionModelVertex : spv::ExecutionModelFragment);
                Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
                Microsoft::WRL::ComPtr<ID3DBlob> diagnostics;
                phase = "FXC native shader compilation";
                const HRESULT status = D3DCompile(hlsl.data(), hlsl.size(), file.path.c_str(), nullptr, nullptr,
                    nativeEntry.c_str(), vertex ? "vs_5_0" : "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &diagnostics);
                if (FAILED(status))
                {
                    throw std::runtime_error(diagnostics
                                                 ? std::string(static_cast<const char*>(diagnostics->GetBufferPointer()), diagnostics->GetBufferSize())
                                                 : "D3DCompile failed without a diagnostic.");
                }
                result.diagnostics = program.Diagnostics().Str();
                if (diagnostics)
                {
                    result.diagnostics.append(static_cast<const char*>(diagnostics->GetBufferPointer()), diagnostics->GetBufferSize());
                }
                Package(result, *bytecode.Get(), vertex);
                return result;
            }
            catch (const std::exception& error)
            {
                throw std::runtime_error(std::string(vertex ? "vertex: " : "fragment: ") + phase + ": " + error.what());
            }
        }
    }

    Program CompileD3D11(std::string_view vertexSource, std::string_view vertexEntry,
        std::string_view fragmentSource, std::string_view fragmentEntry)
    {
        static std::once_flag initialized;
        std::call_once(initialized, [] { tint::Initialize(); });
        std::vector<tint::inspector::StageVariable> vertexInputs, vertexOutputs, fragmentInputs, fragmentOutputs;
        std::map<Binding, uint32_t> textureStages;
        Program result;
        result.vertex = CompileStage(vertexSource, vertexEntry, true, vertexInputs, vertexOutputs, textureStages);
        result.fragment = CompileStage(fragmentSource, fragmentEntry, false, fragmentInputs, fragmentOutputs, textureStages);
        for (const auto& input : fragmentInputs)
        {
            if (!input.attributes.location)
            {
                continue;
            }
            const auto output = std::find_if(vertexOutputs.begin(), vertexOutputs.end(), [&](const auto& candidate) {
                return candidate.attributes.location == input.attributes.location;
            });
            if (output == vertexOutputs.end() || output->component_type != input.component_type ||
                output->composition_type != input.composition_type)
            {
                throw std::runtime_error("WGSL vertex outputs do not match fragment input locations and types.");
            }
        }
        std::sort(fragmentInputs.begin(), fragmentInputs.end(), [](const auto& a, const auto& b) {
            return a.attributes.location < b.attributes.location;
        });
        uint32_t interfaceHash = 2166136261u;
        for (const auto& input : fragmentInputs)
        {
            if (!input.attributes.location)
            {
                continue;
            }
            const uint32_t values[]{
                *input.attributes.location,
                static_cast<uint32_t>(input.component_type),
                static_cast<uint32_t>(input.composition_type),
                static_cast<uint32_t>(input.interpolation_type),
                static_cast<uint32_t>(input.interpolation_sampling),
            };
            for (const auto value : values)
            {
                for (uint32_t byte = 0; byte < 4; ++byte)
                {
                    interfaceHash = (interfaceHash ^ static_cast<uint8_t>(value >> (byte * 8))) * 16777619u;
                }
            }
        }
        for (uint32_t byte = 0; byte < 4; ++byte)
        {
            result.vertex.bytes[8 + byte] = static_cast<uint8_t>(interfaceHash >> (byte * 8));
            result.fragment.bytes[4 + byte] = static_cast<uint8_t>(interfaceHash >> (byte * 8));
        }
        return result;
    }
}
