#include "babylon_lite.h"

int main()
{
    bl_EngineContext engine = {};
    bl_UiContext context = {};
    bl_UiElement element = {};
    bl_UiListenerToken token = {};
    unsigned failures = 0;
    failures += bl_createUiContext(engine, nullptr, nullptr) != BL_UNSUPPORTED;
    failures += bl_disposeUiContext(context) != BL_UNSUPPORTED;
    failures += bl_setUiViewport(context, 1, 1, 1) != BL_UNSUPPORTED;
    failures += bl_getUiRoot(context, nullptr) != BL_UNSUPPORTED;
    failures += bl_createUiElement(context, {}, nullptr) != BL_UNSUPPORTED;
    failures += bl_appendUiChild(element, element) != BL_UNSUPPORTED;
    failures += bl_removeUiChild(element, element) != BL_UNSUPPORTED;
    failures += bl_disposeUiElement(element) != BL_UNSUPPORTED;
    failures += bl_setUiProperty(element, {}, {}) != BL_UNSUPPORTED;
    failures += bl_removeUiProperty(element, {}) != BL_UNSUPPORTED;
    failures += bl_setUiText(element, {}) != BL_UNSUPPORTED;
    failures += bl_setUiAttribute(element, {}, {}) != BL_UNSUPPORTED;
    failures += bl_setUiMarkup(element, {}) != BL_UNSUPPORTED;
    failures += bl_loadUiFont(context, {}, {}, 400, false, false) != BL_UNSUPPORTED;
    failures += bl_registerUiImage(context, {}, {}, 1, 1) != BL_UNSUPPORTED;
    failures += bl_unregisterUiImage(context, {}) != BL_UNSUPPORTED;
    failures += bl_setUiImageSampling(context, {}, true) != BL_UNSUPPORTED;
    failures += bl_processUiInput(context, nullptr, nullptr) != BL_UNSUPPORTED;
    failures += bl_updateUi(context, 0) != BL_UNSUPPORTED;
    failures += bl_renderUi(context) != BL_UNSUPPORTED;
    failures += bl_setUiWhiteDifference(context, true) != BL_UNSUPPORTED;
    failures += bl_getUiStats(context, nullptr) != BL_UNSUPPORTED;
    failures += bl_addUiEventListener(element, BL_UI_EVENT_CLICK, nullptr, nullptr, nullptr) !=
                BL_UNSUPPORTED;
    failures += bl_removeUiEventListener(element, token) != BL_UNSUPPORTED;
    return failures ? 1 : 0;
}
