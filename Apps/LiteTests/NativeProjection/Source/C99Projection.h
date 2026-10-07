#pragma once

#include <babylon_lite.h>
#include <cstddef>

namespace LiteProjection
{
    void BindRuntime(bl_Runtime* runtime);
    size_t RetainedApplicationTokens();
}

namespace bblscene
{
    double projectionBox(double height);
    double projectionPositionCount(double handle);
    double projectionIndexCount(double handle);
    double projectionPosition(double handle, double offset);
    void projectionReleaseGeometry(double handle);
    double projectionNode();
    void projectionPositionNode(double handle, double x, double y, double z);
    void projectionParent(double child, double parent);
    void projectionAppend(double parent, double child);
    void projectionRemove(double parent, double child);
    double projectionWorldX(double handle);
    void projectionDisposeNode(double handle);
}
