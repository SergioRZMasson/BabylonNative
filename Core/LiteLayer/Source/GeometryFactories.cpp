#include "LiteInternal.h"
#include "GeometryDataInternal.h"

static bl_Status factoryMesh(bl_EngineContext engine, bl_String name, bl_GeometryData* data,
                             bl_Mesh* out)
{
    bl_MeshGeometry geometry = {{data->positions, data->vertexCount * 3},
                                {data->normals, data->vertexCount * 3},
                                {data->indices, data->indexCount},
                                {data->uvs, data->vertexCount * 2},
                                {},
                                {},
                                {}};
    bl_Mesh mesh;
    bl_Status status = bl_createMeshFromData(engine, name, &geometry, &mesh);
    bl_freeGeometryData(engine._runtime, data);
    if (status == BL_OK)
    {
        *out = mesh;
    }
    return status;
}

bl_Status bl_createCylinder(bl_EngineContext h, const bl_CylinderOptions* options, bl_Mesh* out)
{
    L_GET(h, L_ENGINE, L_Engine, engine);
    (void)engine;
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_GeometryData data;
    L_TRY(l_createCylinderData(h._runtime, options, true, &data));
    return factoryMesh(h, {"cylinder", 8}, &data, out);
}

bl_Status bl_createPlane(bl_EngineContext h, const bl_PlaneOptions* options, bl_Mesh* out)
{
    L_GET(h, L_ENGINE, L_Engine, engine);
    (void)engine;
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_GeometryData data;
    L_TRY(l_createPlaneData(h._runtime, options, true, &data));
    return factoryMesh(h, {"plane", 5}, &data, out);
}

bl_Status bl_createDisc(bl_EngineContext h, const bl_DiscOptions* options, bl_Mesh* out)
{
    L_GET(h, L_ENGINE, L_Engine, engine);
    (void)engine;
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_GeometryData data;
    L_TRY(l_createDiscData(h._runtime, options, true, &data));
    return factoryMesh(h, {"disc", 4}, &data, out);
}

bl_Status bl_createPolyhedron(bl_EngineContext h, const bl_PolyhedronOptions* options, bl_Mesh* out)
{
    L_GET(h, L_ENGINE, L_Engine, engine);
    (void)engine;
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_GeometryData data;
    L_TRY(l_createPolyhedronData(h._runtime, options, true, &data));
    return factoryMesh(h, {"polyhedron", 10}, &data, out);
}

bl_Status bl_createRibbon(bl_EngineContext h, const bl_RibbonOptions* options, bl_Mesh* out)
{
    L_GET(h, L_ENGINE, L_Engine, engine);
    (void)engine;
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_GeometryData data;
    L_TRY(l_createRibbonData(h._runtime, options, true, &data));
    return factoryMesh(h, {"ribbon", 6}, &data, out);
}

bl_Status bl_createTube(bl_EngineContext h, const bl_TubeOptions* options, bl_Mesh* out)
{
    L_GET(h, L_ENGINE, L_Engine, engine);
    (void)engine;
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_GeometryData data;
    L_TRY(l_createTubeData(h._runtime, options, true, &data));
    return factoryMesh(h, {"tube", 4}, &data, out);
}

bl_Status bl_createExtrudeShape(bl_EngineContext h, const bl_ExtrudeShapeOptions* options,
                                bl_Mesh* out)
{
    L_GET(h, L_ENGINE, L_Engine, engine);
    (void)engine;
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_GeometryData data;
    L_TRY(l_createExtrudeShapeData(h._runtime, options, true, &data));
    return factoryMesh(h, {"extrude", 7}, &data, out);
}
