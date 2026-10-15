#ifndef BL_PBR_INTERNAL_H
#define BL_PBR_INTERNAL_H

#include "EnvironmentInternal.h"

struct L_PbrPacket
{
    L_NativePipeline pipeline;
    L_Environment* environment;
    L_Material* material;
    bool instances;
    bool tangents;
    bool colors;
    bool flatNormal;
    bool cpuSkin;
};

bl_Status l_preparePbrScene(bl_Runtime* runtime, L_Scene* scene);
bl_Status l_drawPbr(bl_Runtime* runtime, L_Scene* scene, L_Mesh* mesh, L_Material* material,
                    const bl_Mat4* view, const bl_Mat4* projection, bl_Vec3 eye, uint16_t viewId);
void l_cleanupPbrScene(bl_Runtime* runtime, L_Scene* scene);
uint64_t l_pbrDrawState(const L_Material* material, double determinant, bool depth);
bl_Status l_pbrSources(bl_Runtime* runtime, bool instances, L_Text* vertex, L_Text* fragment,
                       bool doubleSided = false, bool baseImage = false, bool baseUv1 = false,
                       bool ormUv1 = false, const L_Material* material = NULL,
                       bool tangents = false, bool colors = false, bool flatNormal = false,
                       bool cpuSkin = false);

#endif
