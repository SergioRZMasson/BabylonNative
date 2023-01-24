#pragma once

#include <vector>
#include <string>
#include <regex>

#include "Babylon/ShaderProcessor/StringUtils.h"

namespace Babylon
{
    class ShaderCodeCursor
    {
    public:
        ShaderCodeCursor() = default;

        int lineIndex;

        std::string currentLine()
        {
            return m_lines[lineIndex];
        }

        bool canRead()
        {
            return lineIndex < m_lines.size()- 1;
        }

        void SetLines(std::vector<std::string> lines)
        {
            m_lines = {};

            for (auto& line : lines)
            {
                // Prevent removing line break in macros.
                if (line[0] == '#')
                {
                    m_lines.push_back(line);
                    continue;
                }

                auto trimed = std::regex_replace(line, std::regex{"^\\s+|\\s+$/gm"}, "");

                // Do not split single line comments
                if (trimed.starts_with("//"))
                {
                    m_lines.push_back(line);
                    continue;
                }

                auto split = String::Split(line,";");

                for (int index = 0; index < split.size(); index++)
                {
                    auto subLine = split[index];
                    subLine = String::Trim(subLine);

                    if (subLine.empty())
                    {
                        continue;
                    }

                    m_lines.push_back(subLine + (index != split.size() - 1 ? ";" : ""));
                }
            }
        }

        

    private:
        std::vector<std::string> m_lines;
    };
}