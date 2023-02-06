#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <memory>

namespace Babylon
{
    class ProcessingOptions;

    class ShaderCodeNode
    {
    public:
        ShaderCodeNode() : Line{}, Children{}, AdditionalDefineKey{nullptr}, AdditionalDefineValue{nullptr} {};
        virtual bool IsValid(std::map<std::string, std::string> preprocessors);
        virtual std::string Process(std::map<std::string, std::string> preprocessors, ProcessingOptions& options);
        std::string Line;
        std::vector<std::shared_ptr<ShaderCodeNode>> Children;
        std::optional<std::string>  AdditionalDefineKey;
        std::optional<std::string>  AdditionalDefineValue;
    };
}