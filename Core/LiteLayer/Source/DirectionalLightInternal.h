#ifndef BL_DIRECTIONAL_LIGHT_INTERNAL_H
#define BL_DIRECTIONAL_LIGHT_INTERNAL_H

#include "LightInternal.h"

struct L_DirectionalLight
{
    L_Light light;
    bl_DirectionalLightProperties properties;
};

static_assert(__is_trivial(L_DirectionalLight) && __is_standard_layout(L_DirectionalLight),
              "POD directional light");
static_assert(offsetof(L_DirectionalLight, light) == 0,
              "Checked first-member directional conversion");

bl_Status l_directionalLight(bl_DirectionalLight light, L_DirectionalLight** record);
bl_Status l_writeDirectionalLight(bl_Runtime* runtime, L_DirectionalLight* light, float* data);

#endif
