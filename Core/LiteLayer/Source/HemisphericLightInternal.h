#ifndef BL_HEMISPHERIC_LIGHT_INTERNAL_H
#define BL_HEMISPHERIC_LIGHT_INTERNAL_H

#include "SceneInternal.h"

struct L_HemisphericLight
{
    L_Node node;
    bl_HemisphericLightProperties properties;
    uint64_t dataVersion;
};

static_assert(__is_trivial(L_HemisphericLight) && __is_standard_layout(L_HemisphericLight),
              "POD light");
static_assert(offsetof(L_HemisphericLight, node) == 0, "Checked first-member light conversion");

bl_Status l_writeHemisphericLight(bl_Runtime* runtime, L_HemisphericLight* light, float* data);

#endif
