#ifndef BL_LIGHT_INTERNAL_H
#define BL_LIGHT_INTERNAL_H

#include "SceneInternal.h"

enum L_LightKind
{
    L_LIGHT_HEMISPHERIC = 1,
    L_LIGHT_DIRECTIONAL
};

struct L_Light
{
    L_Node node;
    unsigned kind; /* Raw tag storage permits rejecting corrupt/unknown discriminator values. */
    uint64_t dataVersion;
};

static_assert(__is_trivial(L_Light) && __is_standard_layout(L_Light), "POD common light");
static_assert(offsetof(L_Light, node) == 0, "Checked first-member common light conversion");
static_assert(offsetof(L_Node, record) == 0, "Checked node record conversion");

bl_Status l_lightFamily(bl_Light light, L_Light** record);
bl_Status l_writeLight(bl_Runtime* runtime, L_Light* light, float* data);

#endif
