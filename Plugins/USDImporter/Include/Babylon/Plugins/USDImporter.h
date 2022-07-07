#pragma once

#include <napi/napi.h>

namespace Babylon::Plugins
{
    class USDImporter final : public Napi::ObjectWrap<USDImporter>
    {
    public:
        static void Initialize(Napi::Env env);
    };
}
