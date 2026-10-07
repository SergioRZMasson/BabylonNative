#include "C99Projection.h"
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

namespace
{
    thread_local bl_Runtime* s_runtime{};
    thread_local uint32_t s_nextToken{1};
    thread_local std::map<uint32_t, bl_GeometryData> s_geometry;
    thread_local std::map<uint32_t, bl_SceneNode> s_nodes;

    void Check(bl_Status status)
    {
        if (status != BL_OK)
        {
            throw std::runtime_error("Projected C99 operation failed: " + std::to_string(status));
        }
    }

    uint32_t Token(double value)
    {
        if (!std::isfinite(value) || value < 1 || value > UINT32_MAX || value != std::floor(value))
        {
            throw std::runtime_error("Invalid test-application identity.");
        }
        return static_cast<uint32_t>(value);
    }
}

namespace LiteProjection
{
    void BindRuntime(bl_Runtime* runtime)
    {
        if (!s_geometry.empty() || !s_nodes.empty())
        {
            throw std::runtime_error("Projection runtime cannot change while application tokens remain.");
        }
        s_runtime = runtime;
        s_nextToken = 1;
    }

    size_t RetainedApplicationTokens()
    {
        return s_geometry.size() + s_nodes.size();
    }
}

namespace bblscene
{
    double projectionBox(double height)
    {
        bl_BoxOptions options{};
        options.height = {true, height};
        bl_GeometryData geometry{};
        Check(bl_createBoxData(s_runtime, &options, &geometry));
        const uint32_t token = s_nextToken++;
        s_geometry.emplace(token, geometry);
        return token;
    }

    double projectionPositionCount(double handle)
    {
        return static_cast<double>(s_geometry.at(Token(handle)).vertexCount * 3);
    }

    double projectionIndexCount(double handle)
    {
        return static_cast<double>(s_geometry.at(Token(handle)).indexCount);
    }

    double projectionPosition(double handle, double offset)
    {
        const auto& geometry = s_geometry.at(Token(handle));
        if (!std::isfinite(offset) || offset < 0 || offset >= geometry.vertexCount * 3 || offset != std::floor(offset))
        {
            throw std::runtime_error("Projected geometry span index is out of range.");
        }
        return geometry.positions[static_cast<size_t>(offset)];
    }

    void projectionReleaseGeometry(double handle)
    {
        const uint32_t token = Token(handle);
        Check(bl_freeGeometryData(s_runtime, &s_geometry.at(token)));
        s_geometry.erase(token);
    }

    double projectionNode()
    {
        bl_SceneNode node{};
        Check(bl_createTransformNode(s_runtime, {"projected-test-node", 19}, nullptr, &node));
        const uint32_t token = s_nextToken++;
        s_nodes.emplace(token, node);
        return token;
    }

    void projectionPositionNode(double handle, double x, double y, double z)
    {
        Check(bl_setNodePosition(s_nodes.at(Token(handle)), {x, y, z}));
    }

    void projectionParent(double child, double parent)
    {
        Check(bl_setNodeParent(s_nodes.at(Token(child)), parent == 0 ? bl_SceneNode{} : s_nodes.at(Token(parent))));
    }

    void projectionAppend(double parent, double child)
    {
        Check(bl_appendNodeChild(s_nodes.at(Token(parent)), s_nodes.at(Token(child))));
    }

    void projectionRemove(double parent, double child)
    {
        Check(bl_removeNodeChild(s_nodes.at(Token(parent)), s_nodes.at(Token(child))));
    }

    double projectionWorldX(double handle)
    {
        bl_Mat4 world{};
        uint64_t version{};
        Check(bl_getNodeWorldMatrix(s_nodes.at(Token(handle)), &world, &version));
        return world.values[12];
    }

    void projectionDisposeNode(double handle)
    {
        const uint32_t token = Token(handle);
        Check(bl_disposeNode(s_nodes.at(token)));
        s_nodes.erase(token);
    }
}
