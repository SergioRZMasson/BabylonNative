#include <babylon_lite.h>
#include <type_traits>

static_assert(std::is_standard_layout<bl_Mesh>::value, "C99 identities remain plain records.");
static_assert(std::is_same<decltype(&bl_getContainerMeshes),
    bl_Status (*)(bl_AssetContainer, bl_Mesh*, size_t, size_t*)>::value, "Container count/fill ABI.");
static_assert(std::is_same<decltype(&bl_getSceneMeshes),
    bl_Status (*)(bl_SceneContext, bl_Mesh*, size_t, size_t*)>::value, "Scene count/fill ABI.");
static_assert(std::is_same<decltype(&bl_getSceneLights),
    bl_Status (*)(bl_SceneContext, bl_Light*, size_t, size_t*)>::value, "Light count/fill ABI.");
static_assert(std::is_same<decltype(&bl_getMeshBounds),
    bl_Status (*)(bl_Mesh, bl_Vec3*, bl_Vec3*)>::value, "Local bounds ABI.");
static_assert(std::is_same<decltype(&bl_getMaterialName),
    bl_Status (*)(bl_Material, bl_String*)>::value, "Borrowed material name ABI.");
