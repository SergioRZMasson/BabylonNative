#include <babylon_lite.h>

bl_Status MinecraftUiContract(bl_UiContext context, bl_UiElement element, bl_String source,
                              bl_UiStats* stats)
{
    bl_Status (*sampling)(bl_UiContext, bl_String, bool) = bl_setUiImageSampling;
    bl_Status (*property)(bl_UiElement, bl_String, bl_String) = bl_setUiProperty;
    bl_Status (*observe)(bl_UiContext, bl_UiStats*) = bl_getUiStats;
    bl_Status (*difference)(bl_UiContext, bool) = bl_setUiWhiteDifference;
    bl_Status status = sampling(context, source, true);
    if (status == BL_OK)
    {
        status = property(element, source, source);
    }
    if (status == BL_OK)
    {
        status = difference(context, true);
    }
    if (status == BL_OK)
    {
        status = observe(context, stats);
    }
    return status;
}
