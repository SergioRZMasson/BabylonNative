#include "babylon_lite.h"

bl_Status bl_createUiContext(bl_EngineContext, const bl_UiContextOptions*, bl_UiContext*)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_disposeUiContext(bl_UiContext)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_setUiViewport(bl_UiContext, uint32_t, uint32_t, double)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_getUiRoot(bl_UiContext, bl_UiElement*)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_createUiElement(bl_UiContext, bl_String, bl_UiElement*)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_appendUiChild(bl_UiElement, bl_UiElement)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_removeUiChild(bl_UiElement, bl_UiElement)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_disposeUiElement(bl_UiElement)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_setUiProperty(bl_UiElement, bl_String, bl_String)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_removeUiProperty(bl_UiElement, bl_String)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_setUiText(bl_UiElement, bl_String)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_setUiAttribute(bl_UiElement, bl_String, bl_String)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_setUiMarkup(bl_UiElement, bl_String)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_loadUiFont(bl_UiContext, bl_Bytes, bl_String, uint32_t, bool, bool)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_registerUiImage(bl_UiContext, bl_String, bl_Bytes, uint32_t, uint32_t)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_unregisterUiImage(bl_UiContext, bl_String)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_setUiImageSampling(bl_UiContext, bl_String, bool)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_processUiInput(bl_UiContext, const bl_UiInput*, bool*)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_updateUi(bl_UiContext, double)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_renderUi(bl_UiContext)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_setUiWhiteDifference(bl_UiContext, bool)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_getUiStats(bl_UiContext, bl_UiStats*)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_addUiEventListener(bl_UiElement, bl_UiEventKind, bl_UiEventCallback, void*,
                                bl_UiListenerToken*)
{
    return BL_UNSUPPORTED;
}

bl_Status bl_removeUiEventListener(bl_UiElement, bl_UiListenerToken)
{
    return BL_UNSUPPORTED;
}
