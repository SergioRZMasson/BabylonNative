#ifndef BL_SCENE_INTERNAL_H
#define BL_SCENE_INTERNAL_H

#include "RuntimeInternal.h"

struct L_Node
{
    L_Record record;
    bl_String name;
    bl_Vec3 position;
    bl_Vec3 scale;
    bl_Vec3 euler;
    bl_Vec3 target;
    bl_Quat quaternion;
    bool eulerValid;
    bool visible;
    bool dirty;
    bool highPrecision;
    L_Node* parent;
    bl_SceneNode* children;
    size_t childCount;
    size_t childCapacity;
    size_t sceneCount;
    bl_Mat4 world;
    uint64_t version;
    uint64_t parentVersion;
    bl_CameraProperties camera;
};

struct L_Callback
{
    L_Callback* next;
    uint64_t id;
    bl_BeforeRenderCallback before;
    bl_SceneDisposeCallback dispose;
    void* user;
    struct L_Control* control;
};

struct L_Scene
{
    L_Record record;
    struct L_Engine* engine;
    bl_SceneProperties properties;
    bl_Camera activeCamera;
    L_Node** members;
    bool* memberOwners;
    size_t memberCount;
    size_t memberCapacity;
    L_Callback* before;
    L_Callback* dispose;
    bool registered;
    L_Scene* next;
    void* drawScratch;
    size_t drawCapacity;
    struct L_DrawGroup* groupScratch;
    size_t groupCapacity;
    uint64_t lightListVersion;
    struct L_StandardPacket* standardPackets;
    size_t standardCapacity;
    float lightData[260];
    uint64_t lightVersions[16];
    uint64_t lightIdentities[16];
    uint64_t packedListVersion;
    size_t packedLightCount;
    bool lightsReady;
    size_t standardMemberCount;
    bool disposing;
};

struct L_Engine
{
    L_Record record;
    bl_Runtime* runtime;
    bl_NativeEngineOptions native;
    bl_RendererBackend backend;
    bool highPrecision;
    bool running;
    bool first;
    bool inFrame;
    L_Scene* scenes;
    L_Scene* lastScene;
    L_Engine* globalNext;
    bl_CompletionCallback completion;
    void* completionUser;
    bl_EngineStats stats;
    struct L_ViewReservation* reservedViews;
};

struct L_ViewReservation
{
    L_ViewReservation* next;
    uint16_t first;
    uint16_t count;
};

static_assert(__is_trivial(L_Node) && __is_standard_layout(L_Node), "Node storage must be POD");
static_assert(__is_trivial(L_Scene) && __is_standard_layout(L_Scene), "Scene storage must be POD");
static_assert(__is_trivial(L_Engine) && __is_standard_layout(L_Engine),
              "Engine storage must be POD");

bl_Status l_node(bl_SceneNode handle, L_Node** out, bool disposedOK = false);
bool l_vec(bl_Vec3 p);
bool l_quat(bl_Quat p);
void l_initNode(L_Node* n);
void l_cleanNode(bl_Runtime* r, L_Node* n);
void l_parent(L_Node* n, L_Node* parent);
void l_multiply(const bl_Mat4* a, const bl_Mat4* b, bl_Mat4* out, bool precise);
void l_identity(bl_Mat4* m);
bl_Status l_world(bl_Runtime* r, L_Node* n);
void l_inverse(const bl_Mat4* m, bl_Mat4* out, bool precise);
void l_unregisterScene(L_Scene* s);
void l_sceneCleanup(bl_Runtime* r, L_Record* record);
bl_Status l_removeSceneNode(bl_Runtime* r, L_Scene* s, L_Node* n);
bl_Status l_retire(L_Engine* e);
bl_Status l_reserveViews(L_Engine* engine, L_ViewReservation* reservation);
void l_releaseViews(L_Engine* engine, L_ViewReservation* reservation);
bool l_isNodeKind(unsigned kind);
bl_Status l_cameraFamily(bl_Camera camera, L_Node** node);
void l_arcEye(L_Node* node, bl_Vec3* eye);

#define L_NODE_GET(handle, variable) \
    L_Node* variable;                \
    L_TRY(l_node((handle), &variable))

#endif
