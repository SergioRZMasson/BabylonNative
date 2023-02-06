#pragma once

#include <vector>
#include <string>
#include <sstream>
#include <stdarg.h>
#include <regex>
#include <format>

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

        inline bool ReplaceInplace(std::string& str, const std::string& from, const std::string& to)
        {
            size_t start_pos = str.find(from);
            if (start_pos == std::string::npos)
                return false;
            str.replace(start_pos, from.length(), to);
            return true;
        }

        inline std::string Replace(std::string str, const std::string& from, const std::string& to)
        {
            size_t start_pos = str.find(from);
            if (start_pos == std::string::npos)
                return str;

            str.replace(start_pos, from.length(), to);
            return str;
        }

        bool IsNumber(const std::string& s)
        {
            std::string::const_iterator it = s.begin();
            while (it != s.end() && std::isdigit(*it))
                ++it;
            return !s.empty() && it == s.end();
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

        inline std::string EscapeRegExp(std::string s) 
        { 
            return std::regex_replace(s, std::regex{"[.*+?^${}()|[\\]\\]"}, std::string("\\\$&"));
        }

        inline int FindBackward(std::string s, int index, char c)
        {
            while (index >= 0 && s[index] != c)
            {
                index--;
            }

            return index;
        }

        inline bool IsIdentifierChar(std::string c)
        {
            char v = c[0];
            return (
                (v >= 48 && v <= 57) ||  // 0-9
                (v >= 65 && v <= 90) ||  // A-Z
                (v >= 97 && v <= 122) || // a-z
                v == 95);                // _
        }

        inline int SkipWhitespaces(std::string s, int index)
        {
            while (index < s.size())
            {
                char c = s[index];
                if (c != ' ' && c != '\n' && c != '\r' && c != '\t' && c != '\u000a' && c != '\u00a0')
                {
                    break;
                }
                index++;
            }

            return index;
        }

        inline std::string ToLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c)
                { return std::tolower(c); });

            return value;
        }

        int ExtractBetweenMarkers(std::string markerOpen, std::string markerClose, std::string block, int startIndex);
        std::string RemoveComments(std::string s);
    }
}