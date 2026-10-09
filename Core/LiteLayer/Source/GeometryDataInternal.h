#ifndef BL_GEOMETRY_DATA_INTERNAL_H
#define BL_GEOMETRY_DATA_INTERNAL_H

#include "RuntimeInternal.h"

struct L_GeometryWork
{
    double* positions;
    double* normals;
    double* uvs;
    uint32_t* indices;
    size_t vertices;
    size_t indexCount;
};

struct L_Path3D
{
    bl_Vec3* tangents;
    bl_Vec3* normals;
    bl_Vec3* binormals;
    double* distances;
    size_t count;
};

static_assert(__is_trivial(L_GeometryWork) && __is_standard_layout(L_GeometryWork),
              "Geometry work must be POD");
static_assert(__is_trivial(L_Path3D) && __is_standard_layout(L_Path3D),
              "Path frame work must be POD");

bl_Status l_createGeometryData(bl_Runtime* runtime, size_t vertices, size_t indices,
                               bl_GeometryData* data);
double l_geometryOption(bl_OptionalNumber option, double fallback);
double l_geometryInt32(double value);
bool l_geometryAdd(size_t first, size_t second, size_t* out);
bl_Status l_geometryBudget(bl_Runtime* runtime, size_t vertices, size_t indices, bool gpu,
                           size_t extraScratch = 0);
bl_Status l_geometryScratchBytes(bl_Runtime* runtime, size_t count, size_t size, size_t* out);
bl_Status l_geometryWork(bl_Runtime* runtime, size_t vertices, size_t indices,
                         L_GeometryWork* work);
void l_geometryFree(bl_Runtime* runtime, L_GeometryWork* work);
bl_Status l_geometryFinish(bl_Runtime* runtime, const L_GeometryWork* work, bl_GeometryData* data);
bl_Status l_geometryNormals(bl_Runtime* runtime, L_GeometryWork* work);
bl_Status l_geometryPoints(bl_Runtime* runtime, bl_Vec3Span points);
bl_Vec3 l_pathSubtract(bl_Vec3 first, bl_Vec3 second);
bl_Vec3 l_pathCross(bl_Vec3 first, bl_Vec3 second);
double l_pathLength(bl_Vec3 vector);
bl_Vec3 l_pathNormalize(bl_Vec3 vector);
bl_Vec3 l_pathRotate(bl_Vec3 vector, bl_Vec3 axis, double angle);
bl_Status l_computePath3D(bl_Runtime* runtime, bl_Vec3Span curve, L_Path3D* path);
void l_freePath3D(bl_Runtime* runtime, L_Path3D* path);
bl_Status l_ribbonBudget(bl_Runtime* runtime, size_t paths, size_t points, bool closeArray,
                         bool closePath, bool gpu, size_t extraScratch, size_t* vertices,
                         size_t* indices);
bl_Status l_ribbonWork(bl_Runtime* runtime, bl_Vec3PathSpan paths, bool closeArray, bool closePath,
                       L_GeometryWork* work);
bl_Status l_createCylinderData(bl_Runtime* runtime, const bl_CylinderOptions* options, bool gpu,
                               bl_GeometryData* data);
bl_Status l_createPlaneData(bl_Runtime* runtime, const bl_PlaneOptions* options, bool gpu,
                            bl_GeometryData* data);
bl_Status l_createDiscData(bl_Runtime* runtime, const bl_DiscOptions* options, bool gpu,
                           bl_GeometryData* data);
bl_Status l_createPolyhedronData(bl_Runtime* runtime, const bl_PolyhedronOptions* options, bool gpu,
                                 bl_GeometryData* data);
bl_Status l_createRibbonData(bl_Runtime* runtime, const bl_RibbonOptions* options, bool gpu,
                             bl_GeometryData* data);
bl_Status l_createTubeData(bl_Runtime* runtime, const bl_TubeOptions* options, bool gpu,
                           bl_GeometryData* data);
bl_Status l_createExtrudeShapeData(bl_Runtime* runtime, const bl_ExtrudeShapeOptions* options,
                                   bool gpu, bl_GeometryData* data);

#endif
