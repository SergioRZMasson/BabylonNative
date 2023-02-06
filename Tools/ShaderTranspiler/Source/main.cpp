#include <iostream>

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <iostream>
#include <fstream>

#include "ShaderCompiler.h"

#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Plugins/NativeEngine.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>

#include <winrt/base.h>
#include <regex>

namespace fs = std::filesystem;

struct HLSLResult{
    std::string VertexShader;
    std::string FragmentShader;
};

std::unique_ptr<Babylon::Graphics::Device> CreateGraphicsDevice()
{
    Babylon::Graphics::DeviceConfiguration config{};
    std::unique_ptr<Babylon::Graphics::Device> device = Babylon::Graphics::Device::Create(config);
    return device;
}

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

std::string ParseBabylonjsShader(std::string shaderCode, bool isFragmentShader, Babylon::AppRuntime* runtime)
{
    std::promise<std::string> done{};

    runtime->Dispatch([&shaderCode, &isFragmentShader, &done](Napi::Env env) 
    { 
        auto parseShaderFunction = env.Global().Get("ParseVertexShader").As<Napi::Function>();

        auto callback = Napi::Function::New(env, [&done](const Napi::CallbackInfo& info)
        {
            auto nativeTexture = info[0].As<Napi::String>().Utf8Value();
            done.set_value(nativeTexture);
        });

        if (parseShaderFunction.IsNull() || parseShaderFunction.IsUndefined())
        {
            done.set_value("");
            return;
        }

        auto jsPromise = parseShaderFunction.Call({Napi::Value::From(env, shaderCode.c_str()), Napi::Value::From(env, isFragmentShader), callback});       
    });

    return done.get_future().get();
}

void ClearSpecialCharacters(std::string& strText)
{
    const std::string cAllowed = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890,.:_|;-#/()\\!?@#$%\n\r\t ";

    auto new_end = std::remove_if(strText.begin(), strText.end(),
        [cAllowed](std::string::value_type c)
        { return cAllowed.find(c) == std::string::npos; });

    strText.erase(new_end, strText.end());
}

std::string DefineStandards(std::string strText)
{
    strText = "#define NUM_BONE_INFLUENCERS0 1 \r\n" + strText;
    strText = "#define NUM_BONE_INFLUENCERS4 1 \r\n" + strText;
    strText = "#define USE_EXACT_SRGB_CONVERSIONS 1 \r\n" + strText;
    strText = "#define SM_NORMALBIAS1 1 \r\n" + strText;
    strText = "#define SM_USEDISTANCE1 1 \r\n" + strText;
    return strText;
}

inline std::string Replace(std::string str, const std::string& from, const std::string& to)
{
    size_t start_pos = str.find(from);
    
    if (start_pos == std::string::npos)
        return str;

    str.replace(start_pos, from.length(), to);
    return str;
}

static std::string RemoveEmptyLines(std::string str)
{
    size_t pos;
    while ((pos = str.find("\n\n", 0)) != std::string::npos)
    {
        str = str.erase(pos, 1);
    }

    while ((pos = str.find("\r\n", 0)) != std::string::npos)
    {
        str = str.erase(pos, 1);
    }

    return str;
}

std::string ReplaceReverseDefine(std::string strText)
{
    std::regex definedRegex{"!defined([])"};
    strText = Replace(strText, "#if !defined(WORLD_UBO)", "#ifndef WORLD_UBO");
    strText = Replace(strText, "#if defined(THIN_INSTANCES)  !defined(WORLD_UBO)", "#ifdef THIN_INSTANCES_NOR_WORLD_UBO");
    strText = Replace(strText, "#if defined(THIN_INSTANCES) !defined(WORLD_UBO)", "#ifdef THIN_INSTANCES_NOR_WORLD_UBO");
    strText = Replace(strText, "#if defined(VELOCITY) || defined(PREPASS_VELOCITY)", "#ifdef VELOCITY_OR_PREPASS_VELOCITY");
    strText = Replace(strText, "#if defined(WEBGL2) || defined(WEBGPU) || defined(NATIVE)", "#ifdef WEBGL2_OR_WEBGPU_OR_NATIVE");
    strText = Replace(strText, "#if defined(SM_DEPTHCLAMP) SM_DEPTHCLAMP1", "#ifdef SM_DEPTHCLAMP_SM_DEPTHCLAMP1");
    strText = Replace(strText, "#if defined(PREPASS_VELOCITY) || defined(VELOCITY)", "#ifdef PREPASS_VELOCITY_OR_VELOCITY");
    strText = Replace(strText, "#if defined(INSTANCES) defined(THIN_INSTANCES)", "#ifdef INSTANCES_THIN_INSTANCES");
    strText = Replace(strText, "#if defined(SM_DEPTHCLAMP) SM_DEPTHCLAMP1", "#ifdef SM_DEPTHCLAMP_SM_DEPTHCLAMP1");
    strText = Replace(strText, "#if defined(SM_DEPTHCLAMP)  SM_DEPTHCLAMP1", "#ifdef SM_DEPTHCLAMP_SM_DEPTHCLAMP1");
    strText = Replace(strText, "#if defined(CLIPPLANE) || defined(CLIPPLANE2) || defined(CLIPPLANE3) || defined(CLIPPLANE4) || defined(CLIPPLANE5) || defined(CLIPPLANE6)", "#ifdef CLIPPLANE_OR_CLIPPLANE2_OR_CLIPPLANE3_OR_CLIPPLANE4_OR_CLIPPLANE5_OR_CLIPPLANE6");
    return strText;
}

int main(int argc, char ** argv)
{
    // Create the Babylon Native graphics device and update.
    auto device = CreateGraphicsDevice();
    auto deviceUpdate = std::make_unique<Babylon::Graphics::DeviceUpdate>(device->GetUpdate("update"));

    // Start rendering a frame to unblock the JavaScript from queuing graphics
    // commands.
    device->StartRenderingCurrentFrame();
    deviceUpdate->Start();

    auto runtime = std::make_unique<Babylon::AppRuntime>();

    runtime->Dispatch([&device](Napi::Env env)
        {
            // Add the Babylon Native graphics device to the JavaScript environment.
            device->AddToJavaScript(env);

            // Initialize the console polyfill.
            Babylon::Polyfills::Console::Initialize(env, [](const char* message, auto)
                { std::cout << message; });

            // Initialize the window, XMLHttpRequest, and NativeEngine polyfills.          
            Babylon::Polyfills::XMLHttpRequest::Initialize(env);
            Babylon::Plugins::NativeEngine::Initialize(env);
        });

    // Load the scripts for Babylon.js core and loaders plus this app's index.js.
    Babylon::ScriptLoader loader{*runtime};
    
    loader.LoadScript("app:///Scripts/babylon.max.js");
    loader.LoadScript("app:///Scripts/experience.js");

    std::promise<void> startup{};
    loader.Dispatch([&startup](Napi::Env env){ startup.set_value(); });

    deviceUpdate->Finish();
    device->FinishRenderingCurrentFrame();

    startup.get_future().wait();

    if( argc < 3 )
    {
        std::cout << "Invalid number of arguments." << std::endl;
        return -1;
    }

    fs::path vertexShaderPath { argv[ 1 ] };
    fs::path fragmentShaderPath { argv[ 2 ] };

    if(!fs::exists(vertexShaderPath)) {
        std::printf("Vertex shader not found at path: %s \n", vertexShaderPath.c_str());
        return -1;
    }

    if(!fs::exists(fragmentShaderPath)) {
        std::printf("Frament shader not found at path: %s \n", fragmentShaderPath.c_str());
        return -1;
    }

    auto vertexShader = readFile(vertexShaderPath);
    auto fragmentShader = readFile(fragmentShaderPath);
    
    auto isFXVertex = vertexShaderPath.extension() == ".fx";
    auto isFXFrag = fragmentShaderPath.extension() == ".fx";

    if (isFXVertex)
    {
        vertexShader = ParseBabylonjsShader(vertexShader, false, runtime.get());
        ClearSpecialCharacters(vertexShader);
        vertexShader = DefineStandards(vertexShader);
        vertexShader = ReplaceReverseDefine(vertexShader);
        vertexShader = RemoveEmptyLines(vertexShader);
    }
    
    if (isFXFrag)
    {
        fragmentShader = ParseBabylonjsShader(fragmentShader, true, runtime.get());
        ClearSpecialCharacters(fragmentShader);
        fragmentShader = DefineStandards(fragmentShader);
        fragmentShader = ReplaceReverseDefine(fragmentShader);
        fragmentShader = RemoveEmptyLines(fragmentShader);
    }

    ShaderTranspiler::ShaderTranspiler shaderCompiler{};

    try
    {
        if (isFXVertex)
            writeFile(vertexShaderPath.replace_extension("glsl"), vertexShader);
        
        if (isFXFrag)
            writeFile(fragmentShaderPath.replace_extension("glsl"), fragmentShader);

        auto [vertHLSL, fragHLSL] = shaderCompiler.Transpile(vertexShader, fragmentShader);
        
        writeFile(vertexShaderPath.replace_extension("hlsl"), vertHLSL);
        writeFile(fragmentShaderPath.replace_extension("hlsl"), fragHLSL);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return -1;
    }

    std::cout << " \n SUCCEED \n";
}


