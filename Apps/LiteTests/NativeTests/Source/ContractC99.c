#include <babylon_lite.h>

uint32_t bl_testC99ContractVersion(void)
{
    bl_EngineContext engine = {0};
    bl_ShaderCompileResult result = {0};
    bl_RuntimeOptions options = {0};
    (void)engine;
    (void)result;
    (void)options;
    return BL_CONTRACT_VERSION;
}
