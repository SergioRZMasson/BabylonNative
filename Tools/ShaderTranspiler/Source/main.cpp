#include <iostream>

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <iostream>
#include <fstream>

#include "ShaderCompiler.h"

namespace fs = std::filesystem;

struct HLSLResult{
    std::string VertexShader;
    std::string FragmentShader;
};

std::string readFile(fs::path path)
{
    // Open the stream to 'lock' the file.
    std::ifstream f(path, std::ios::in | std::ios::binary);

    // Obtain the size of the file.
    const auto sz = std::filesystem::file_size(path);

    // Create a buffer.
    std::string result(sz, '\0');

    // Read the whole file into the buffer.
    f.read(result.data(), sz);

    return result;
}

void writeFile(fs::path path, std::string content)
{
    std::ofstream file {path};
    file << content.c_str();
    file.close();
}

int main(int argc, char ** argv)
{
    if( argc < 3 )
    {
        std::cout << "Invalid number of arguments." << std::endl;
        return -1;
    }

    fs::path vertexShaderPath { argv[ 1 ] };
    fs::path fragmentShaderPath { argv[ 2 ] };

    if(!fs::exists(vertexShaderPath)){
        std::cout << "Vertex shader not found at path: " << vertexShaderPath.c_str() << std::endl;
        return -1;
    }

    if(!fs::exists(fragmentShaderPath)){
        std::cout << "Frament shader not found at path: " << fragmentShaderPath.c_str() << std::endl;
        return -1;
    }

    auto vertexShader = "#extension GL_GOOGLE_include_directive:require \n" + readFile(vertexShaderPath);
    auto fragmentShader = "#extension GL_GOOGLE_include_directive:require \n" + readFile(fragmentShaderPath);

    Babylon::ShaderTranspiler shaderCompiler{};
    std::string info;

     try
    {
         auto result = shaderCompiler.Compile(vertexShader, fragmentShader, info);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return -1;
    }
}
