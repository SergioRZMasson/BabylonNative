#ifndef BL_ARC_ROTATE_INTERNAL_H
#define BL_ARC_ROTATE_INTERNAL_H

#include "SceneInternal.h"

struct L_ArcCamera
{
    L_Node node;
    bl_ArcRotateCameraProperties properties;
    bl_ArcRotateCameraLimits limits;
    uint64_t limitOwner;
};

struct L_Limit
{
    L_Record record;
    L_ArcCamera* camera;
};

struct L_Control
{
    L_Record record;
    bl_Runtime* runtime;
    L_ArcCamera* camera;
    L_Scene* scene;
    L_Callback* hook;
    bl_ArcRotateControlOptions options;
    bl_ArcRotateTouch* touches;
    bl_ArcRotateTouch* touchScratch;
    size_t touchCount;
    size_t touchCapacity;
    bool dragging;
    bool panning;
    bool inInput;
    double lastX;
    double lastY;
    double pinchDistance;
    double pinchRadius;
    bl_Status failure;
    bool poseWritten;
};

static_assert(__is_trivial(L_ArcCamera) && __is_standard_layout(L_ArcCamera), "POD camera");
static_assert(__is_trivial(L_Control) && __is_standard_layout(L_Control), "POD controls");
static_assert(offsetof(L_ArcCamera, node) == 0, "Checked first-member camera conversion");

void l_arcClamp(L_ArcCamera* camera);
bool l_arcProperties(const bl_ArcRotateCameraProperties* properties);
void l_arcCommit(L_ArcCamera* camera, const bl_ArcRotateCameraProperties* properties);
void l_arcView(const L_Node* camera, bl_Mat4* view);

#endif
