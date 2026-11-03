#include <babylon_lite.h>

uint32_t bl_testC99ContractVersion(void)
{
    bl_EngineContext engine = {0};
    bl_ShaderCompileResult result = {0};
    bl_RuntimeOptions options = {0};
    bl_Status (*containerMeshes)(bl_AssetContainer, bl_Mesh*, size_t, size_t*) = bl_getContainerMeshes;
    bl_Status (*sceneMeshes)(bl_SceneContext, bl_Mesh*, size_t, size_t*) = bl_getSceneMeshes;
    bl_Status (*sceneLights)(bl_SceneContext, bl_Light*, size_t, size_t*) = bl_getSceneLights;
    bl_Status (*meshBounds)(bl_Mesh, bl_Vec3*, bl_Vec3*) = bl_getMeshBounds;
    bl_Status (*materialName)(bl_Material, bl_String*) = bl_getMaterialName;
    (void)containerMeshes;
    (void)sceneMeshes;
    (void)sceneLights;
    (void)meshBounds;
    (void)materialName;
    (void)engine;
    (void)result;
    (void)options;
    return BL_CONTRACT_VERSION;
}
