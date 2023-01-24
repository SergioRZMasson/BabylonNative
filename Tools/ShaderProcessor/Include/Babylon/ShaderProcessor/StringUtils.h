#pragma once

#include <vector>
#include <string>
#include <sstream>
#include <stdarg.h>
#include <regex>

namespace Babylon
{
    namespace String
    {
        inline std::string Format(const char* format, ...)
        {
            char buffer[512];
            va_list ap;
            va_start(ap, format);
            vsprintf_s(buffer, 512, format, ap);
            va_end(ap);
            return buffer;
        }

        inline std::string Trim(std::string value)
        {
            return std::regex_replace(value, std::regex{"^\\s+|\\s+$/gm"}, "");
        }

        inline std::vector<std::string> Split(std::string s, std::string delimiter)
        {
            size_t pos = 0;
            std::string token;
            std::vector<std::string> result{};

            while ((pos = s.find(delimiter)) != std::string::npos)
            {
                token = s.substr(0, pos);
                result.push_back(token);
                s.erase(0, pos + delimiter.length());
            }

            return result;
        }
    }
}