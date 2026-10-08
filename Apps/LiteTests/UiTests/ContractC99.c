#include "babylon_lite.h"

int lite_ui_header_contract(void)
{
    bl_UiContext context = {0};
    bl_UiInput input = {0};
    bl_UiElement element = {0};
    bl_UiStats stats = {0};
    bl_UiListenerToken token = {0};
    bl_Status status = BL_UNSUPPORTED;
    return (int)status + (int)context._id + (int)input.kind + (int)element._id +
           (int)stats.drawCount + (int)token.value;
}
