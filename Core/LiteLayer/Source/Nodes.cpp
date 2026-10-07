#include "LiteInternal.h"

bool l_vec(bl_Vec3 p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

bool l_quat(bl_Quat p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z) && isfinite(p.w);
}

void l_identity(bl_Mat4* m)
{
    memset(m, 0, sizeof(*m));
    m->values[0] = m->values[5] = m->values[10] = m->values[15] = 1;
}

void l_multiply(const bl_Mat4* a, const bl_Mat4* b, bl_Mat4* out, bool precise)
{
    bl_Mat4 temp;
    for (unsigned c = 0; c < 4; ++c)
    {
        for (unsigned row = 0; row < 4; ++row)
        {
            double value = a->values[row] * b->values[c * 4] +
                           a->values[4 + row] * b->values[c * 4 + 1] +
                           a->values[8 + row] * b->values[c * 4 + 2] +
                           a->values[12 + row] * b->values[c * 4 + 3];
            temp.values[c * 4 + row] = precise ? value : (double)(float)value;
        }
    }
    *out = temp;
}

void l_inverse(const bl_Mat4* m, bl_Mat4* out, bool precise)
{
    double a[4][8] = {};
    for (unsigned row = 0; row < 4; ++row)
    {
        for (unsigned c = 0; c < 4; ++c)
        {
            a[row][c] = m->values[c * 4 + row];
        }
        a[row][4 + row] = 1;
    }
    for (unsigned c = 0; c < 4; ++c)
    {
        unsigned pivot = c;
        for (unsigned row = c + 1; row < 4; ++row)
        {
            if (fabs(a[row][c]) > fabs(a[pivot][c]))
            {
                pivot = row;
            }
        }
        if (fabs(a[pivot][c]) < 1e-30)
        {
            l_identity(out);
            return;
        }
        for (unsigned j = 0; j < 8; ++j)
        {
            double v = a[pivot][j];
            a[pivot][j] = a[c][j];
            a[c][j] = v;
        }
        double inv = 1 / a[c][c];
        for (unsigned j = 0; j < 8; ++j)
        {
            a[c][j] *= inv;
        }
        for (unsigned row = 0; row < 4; ++row)
        {
            if (row != c)
            {
                double v = a[row][c];
                for (unsigned j = 0; j < 8; ++j)
                {
                    a[row][j] -= v * a[c][j];
                }
            }
        }
    }
    for (unsigned row = 0; row < 4; ++row)
    {
        for (unsigned c = 0; c < 4; ++c)
        {
            out->values[c * 4 + row] = precise ? a[row][c + 4] : (double)(float)a[row][c + 4];
        }
    }
}

bl_Status l_node(bl_SceneNode h, L_Node** out, bool disposedOK)
{
    L_Record* p;
    L_TRY(l_get(h._runtime, h._id, 0, &p, true));
    unsigned kind = h._runtime->slots[(uint32_t)h._id - 1].kind;
    if (kind != L_NODE && kind != L_CAMERA && kind != L_MESH)
    {
        return L_FAIL(h._runtime, BL_INVALID_HANDLE, "Not a scene node");
    }
    if ((!p || p->disposed) && !disposedOK)
    {
        return L_FAIL(h._runtime, BL_DISPOSED, "Disposed node identity");
    }
    *out = (L_Node*)p;
    return BL_OK;
}

void l_initNode(L_Node* n)
{
    n->scale = {1, 1, 1};
    n->quaternion = {0, 0, 0, 1};
    n->visible = true;
    n->dirty = true;
    n->camera = {.8, 1, 10000, 2, 2000, .9};
    l_identity(&n->world);
}

void l_parent(L_Node* n, L_Node* parent)
{
    if (n->parent != parent)
    {
        if (n->parent)
        {
            l_unpin(&n->parent->record);
        }
        n->parent = parent;
        if (parent)
        {
            l_pin(&parent->record);
        }
        n->dirty = true;
    }
}

void l_cleanNode(bl_Runtime* r, L_Node* n)
{
    l_parent(n, NULL);
    l_free(r, (void*)n->name.data);
    n->name = {};
    l_free(r, n->children);
    n->children = NULL;
    n->childCount = 0;
}

static void nodeCleanup(bl_Runtime* r, L_Record* record)
{
    l_cleanNode(r, (L_Node*)record);
}

bl_Status bl_createTransformNode(bl_Runtime* r, bl_String name, const bl_TransformNodeOptions* o,
                                 bl_TransformNode* out)
{
    L_TRY(l_check(r));
    if (!out || !l_string(name) ||
        (o && (!l_vec(o->position) || !l_vec(o->scaling) || !l_quat(o->rotationQuaternion))))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Invalid transform options");
    }
    bl_String copy = l_copyString(r, name);
    if (!copy.data)
    {
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Name allocation failed");
    }
    L_Record* record;
    bl_Status s = l_record(r, sizeof(L_Node), L_NODE, 30, nodeCleanup, &record);
    if (s != BL_OK)
    {
        l_free(r, (void*)copy.data);
        return s;
    }
    L_Node* n = (L_Node*)record;
    l_initNode(n);
    n->name = copy;
    if (o)
    {
        n->position = o->position;
        n->scale = o->scaling;
        n->quaternion = o->rotationQuaternion;
    }
    *out = {r, n->record.id};
    return BL_OK;
}

bl_Status bl_createFreeCamera(bl_Runtime* r, bl_Vec3 position, bl_Vec3 target, bl_FreeCamera* out)
{
    L_TRY(l_check(r));
    if (!out || !l_vec(position) || !l_vec(target))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Invalid camera");
    }
    L_NEW(r, L_CAMERA, 30, nodeCleanup, L_Node, n);
    l_initNode(n);
    n->position = position;
    n->target = target;
    *out = {r, n->record.id};
    return BL_OK;
}

bl_Status bl_meshNode(bl_Mesh h, bl_SceneNode* out)
{
    L_GET(h, L_MESH, L_Node, n);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, n->record.id};
    return BL_OK;
}

bl_Status bl_cameraNode(bl_FreeCamera h, bl_SceneNode* out)
{
    L_GET(h, L_CAMERA, L_Node, n);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {h._runtime, n->record.id};
    return BL_OK;
}

bl_Status bl_getNodeName(bl_SceneNode h, bl_String* out)
{
    L_NODE_GET(h, n);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = n->name;
    return BL_OK;
}

bl_Status bl_setNodeName(bl_SceneNode h, bl_String name)
{
    L_NODE_GET(h, n);
    if (!l_string(name))
    {
        return L_FAIL(h._runtime, BL_INVALID_ARGUMENT, "Invalid name");
    }
    if (l_equal(name, n->name))
    {
        return BL_OK;
    }
    bl_String copy = l_copyString(h._runtime, name);
    if (!copy.data)
    {
        return L_FAIL(h._runtime, BL_OUT_OF_MEMORY, "Name allocation failed");
    }
    l_free(h._runtime, (void*)n->name.data);
    n->name = copy;
    return BL_OK;
}

bl_Status bl_getNodePosition(bl_SceneNode h, bl_Vec3* out)
{
    L_NODE_GET(h, n);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = n->position;
    return BL_OK;
}

bl_Status bl_setNodePosition(bl_SceneNode h, bl_Vec3 value)
{
    L_NODE_GET(h, n);
    if (!l_vec(value))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (memcmp(&value, &n->position, sizeof(value)))
    {
        n->position = value;
        n->dirty = true;
    }
    return BL_OK;
}

bl_Status bl_getNodeScaling(bl_SceneNode h, bl_Vec3* out)
{
    L_NODE_GET(h, n);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = n->scale;
    return BL_OK;
}

bl_Status bl_setNodeScaling(bl_SceneNode h, bl_Vec3 value)
{
    L_NODE_GET(h, n);
    if (!l_vec(value))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (memcmp(&value, &n->scale, sizeof(value)))
    {
        n->scale = value;
        n->dirty = true;
    }
    return BL_OK;
}

bl_Status bl_getNodeRotation(bl_SceneNode h, bl_Vec3* out)
{
    L_NODE_GET(h, n);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (!n->eulerValid)
    {
        double x = n->quaternion.x;
        double y = n->quaternion.y;
        double z = n->quaternion.z;
        double w = n->quaternion.w;
        double v = 2 * (x * z + w * y);
        if (v < -1)
        {
            v = -1;
        }
        if (v > 1)
        {
            v = 1;
        }
        n->euler = {atan2(-2 * (y * z - w * x), 1 - 2 * (x * x + y * y)), asin(v),
                    atan2(-2 * (x * y - w * z), 1 - 2 * (y * y + z * z))};
        n->eulerValid = true;
    }
    *out = n->euler;
    return BL_OK;
}

bl_Status bl_setNodeRotation(bl_SceneNode h, bl_Vec3 value)
{
    L_NODE_GET(h, n);
    if (!l_vec(value))
    {
        return BL_INVALID_ARGUMENT;
    }
    double cx = cos(value.x * .5);
    double sx = sin(value.x * .5);
    double cy = cos(value.y * .5);
    double sy = sin(value.y * .5);
    double cz = cos(value.z * .5);
    double sz = sin(value.z * .5);
    bl_Quat q = {sx * cy * cz + cx * sy * sz, cx * sy * cz - sx * cy * sz,
                 cx * cy * sz + sx * sy * cz, cx * cy * cz - sx * sy * sz};
    n->dirty = true;
    n->quaternion = q;
    n->euler = value;
    n->eulerValid = true;
    return BL_OK;
}

bl_Status bl_getNodeRotationQuaternion(bl_SceneNode h, bl_Quat* out)
{
    L_NODE_GET(h, n);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = n->quaternion;
    return BL_OK;
}

bl_Status bl_setNodeRotationQuaternion(bl_SceneNode h, bl_Quat q)
{
    L_NODE_GET(h, n);
    if (!l_quat(q))
    {
        return BL_INVALID_ARGUMENT;
    }
    n->quaternion = q;
    n->dirty = true;
    n->eulerValid = false;
    return BL_OK;
}

static bool containsChild(bl_Runtime* r, L_Node* root, L_Node* target, size_t depth)
{
    if (root == target || depth > r->count)
    {
        return true;
    }
    for (size_t i = 0; i < root->childCount; ++i)
    {
        L_Node* c = (L_Node*)l_peek(r, root->children[i]._id);
        if (c && !c->record.disposed && containsChild(r, c, target, depth + 1))
        {
            return true;
        }
    }
    return false;
}

bl_Status bl_getNodeParent(bl_SceneNode h, bl_SceneNode* out)
{
    L_NODE_GET(h, n);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = n->parent ? bl_SceneNode{h._runtime, n->parent->record.id} : bl_SceneNode{};
    return BL_OK;
}

bl_Status bl_setNodeParent(bl_SceneNode h, bl_SceneNode p)
{
    L_NODE_GET(h, n);
    L_Node* parent = NULL;
    if (!L_NULL(p))
    {
        if (p._runtime != h._runtime)
        {
            return L_FAIL(h._runtime, BL_WRONG_RUNTIME, "Cross-runtime parent");
        }
        L_TRY(l_node(p, &parent));
        for (L_Node* ancestor = parent; ancestor; ancestor = ancestor->parent)
        {
            if (ancestor == n)
            {
                return L_FAIL(h._runtime, BL_INVALID_ARGUMENT, "Parent cycle");
            }
        }
    }
    l_parent(n, parent);
    return BL_OK;
}

bl_Status bl_getNodeChildren(bl_SceneNode h, bl_NodeChildren* out)
{
    L_NODE_GET(h, n);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {n->children, n->childCount};
    return BL_OK;
}

bl_Status bl_appendNodeChild(bl_SceneNode h, bl_SceneNode ch)
{
    L_NODE_GET(h, n);
    if (ch._runtime != h._runtime)
    {
        return BL_WRONG_RUNTIME;
    }
    L_NODE_GET(ch, child);
    if (containsChild(h._runtime, child, n, 0))
    {
        return L_FAIL(h._runtime, BL_INVALID_ARGUMENT, "Child cycle");
    }
    if (n->childCount == n->childCapacity)
    {
        size_t cap = n->childCapacity ? n->childCapacity * 2 : 4;
        size_t bytes;
        if (!l_size(cap, sizeof(bl_SceneNode), &bytes))
        {
            return BL_OUT_OF_MEMORY;
        }
        bl_SceneNode* p = (bl_SceneNode*)l_alloc(h._runtime, bytes);
        if (!p)
        {
            return BL_OUT_OF_MEMORY;
        }
        if (n->childCount)
        {
            memcpy(p, n->children, n->childCount * sizeof(*p));
        }
        l_free(h._runtime, n->children);
        n->children = p;
        n->childCapacity = cap;
    }
    n->children[n->childCount++] = ch;
    return BL_OK;
}

bl_Status bl_removeNodeChild(bl_SceneNode h, bl_SceneNode ch)
{
    L_NODE_GET(h, n);
    if (ch._runtime != h._runtime)
    {
        return BL_WRONG_RUNTIME;
    }
    L_Node* child;
    L_TRY(l_node(ch, &child, true));
    for (size_t i = 0; i < n->childCount; ++i)
    {
        if (n->children[i]._id == ch._id)
        {
            memmove(n->children + i, n->children + i + 1,
                    (--n->childCount - i) * sizeof(*n->children));
            break;
        }
    }
    return BL_OK;
}

bl_Status bl_getNodeVisible(bl_SceneNode h, bool* out)
{
    L_NODE_GET(h, n);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = n->visible;
    return BL_OK;
}

bl_Status bl_setNodeVisible(bl_SceneNode h, bool value)
{
    L_NODE_GET(h, n);
    n->visible = value;
    return BL_OK;
}

static void subtreeVisible(bl_Runtime* r, L_Node* n, bool v)
{
    n->visible = v;
    for (size_t i = 0; i < n->childCount; ++i)
    {
        L_Node* c = (L_Node*)l_peek(r, n->children[i]._id);
        if (c && !c->record.disposed)
        {
            subtreeVisible(r, c, v);
        }
    }
}

bl_Status bl_setSubtreeVisible(bl_SceneNode h, bool value)
{
    L_NODE_GET(h, n);
    subtreeVisible(h._runtime, n, value);
    return BL_OK;
}

bl_Status l_world(bl_Runtime* r, L_Node* n)
{
    if (n->parent)
    {
        if (n->highPrecision && !n->parent->highPrecision)
        {
            n->parent->highPrecision = true;
            n->parent->dirty = true;
        }
        L_TRY(l_world(r, n->parent));
    }
    uint64_t pv = n->parent ? n->parent->version : 0;
    if (!n->dirty && pv == n->parentVersion)
    {
        return BL_OK;
    }
    bl_Mat4 m;
    l_identity(&m);
    double* v = m.values;
    if (n->record.kind == L_CAMERA)
    {
        double zx = n->target.x - n->position.x;
        double zy = n->target.y - n->position.y;
        double zz = n->target.z - n->position.z;
        double len = sqrt(zx * zx + zy * zy + zz * zz);
        if (len >= 1e-10)
        {
            zx /= len;
            zy /= len;
            zz /= len;
            double xx = zz;
            double xz = -zx;
            double xl = sqrt(xx * xx + xz * xz);
            if (xl >= 1e-10)
            {
                xx /= xl;
                xz /= xl;
                v[0] = xx;
                v[1] = 0;
                v[2] = xz;
                v[4] = zy * xz;
                v[5] = zz * xx - zx * xz;
                v[6] = -zy * xx;
                v[8] = zx;
                v[9] = zy;
                v[10] = zz;
            }
        }
    }
    else
    {
        double x = n->quaternion.x;
        double y = n->quaternion.y;
        double z = n->quaternion.z;
        double w = n->quaternion.w;
        double xx = x * x;
        double yy = y * y;
        double zz = z * z;
        double xy = x * y;
        double xz = x * z;
        double yz = y * z;
        double wx = w * x;
        double wy = w * y;
        double wz = w * z;
        v[0] = (1 - 2 * (yy + zz)) * n->scale.x;
        v[1] = 2 * (xy + wz) * n->scale.x;
        v[2] = 2 * (xz - wy) * n->scale.x;
        v[4] = 2 * (xy - wz) * n->scale.y;
        v[5] = (1 - 2 * (xx + zz)) * n->scale.y;
        v[6] = 2 * (yz + wx) * n->scale.y;
        v[8] = 2 * (xz + wy) * n->scale.z;
        v[9] = 2 * (yz - wx) * n->scale.z;
        v[10] = (1 - 2 * (xx + yy)) * n->scale.z;
    }
    v[12] = n->position.x;
    v[13] = n->position.y;
    v[14] = n->position.z;
    if (!n->highPrecision)
    {
        for (unsigned i = 0; i < 16; ++i)
        {
            v[i] = (float)v[i];
        }
    }
    if (n->parent)
    {
        l_multiply(&n->parent->world, &m, &n->world, n->highPrecision);
    }
    else
    {
        n->world = m;
    }
    n->parentVersion = pv;
    n->dirty = false;
    n->version = ++r->matrixVersion;
    return BL_OK;
}

bl_Status bl_getNodeWorldMatrix(bl_SceneNode h, bl_Mat4* out, uint64_t* version)
{
    L_NODE_GET(h, n);
    if (!out || !version)
    {
        return BL_INVALID_ARGUMENT;
    }
    L_TRY(l_world(h._runtime, n));
    *out = n->world;
    *version = n->version;
    return BL_OK;
}

bl_Status bl_getCameraTarget(bl_FreeCamera h, bl_Vec3* out)
{
    L_GET(h, L_CAMERA, L_Node, n);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = n->target;
    return BL_OK;
}

bl_Status bl_setCameraTarget(bl_FreeCamera h, bl_Vec3 value)
{
    L_GET(h, L_CAMERA, L_Node, n);
    if (!l_vec(value))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (memcmp(&value, &n->target, sizeof(value)))
    {
        n->target = value;
        n->dirty = true;
    }
    return BL_OK;
}

bl_Status bl_getCameraProperties(bl_FreeCamera h, bl_CameraProperties* out)
{
    L_GET(h, L_CAMERA, L_Node, n);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = n->camera;
    return BL_OK;
}

bl_Status bl_setCameraProperties(bl_FreeCamera h, const bl_CameraProperties* p)
{
    L_GET(h, L_CAMERA, L_Node, n);
    if (!p || !isfinite(p->fov) || !isfinite(p->nearPlane) || !isfinite(p->farPlane) ||
        !isfinite(p->speed) || !isfinite(p->angularSensitivity) || !isfinite(p->inertia) ||
        p->fov <= 0 || p->fov >= 3.141592653589793 || p->nearPlane <= 0 ||
        p->farPlane <= p->nearPlane)
    {
        return BL_INVALID_ARGUMENT;
    }
    n->camera = *p;
    return BL_OK;
}

bl_Status bl_disposeNode(bl_SceneNode h)
{
    L_Node* n;
    L_TRY(l_node(h, &n, true));
    if (!n || n->record.disposed)
    {
        return BL_OK;
    }
    if (n->sceneCount || n->parent || n->childCount)
    {
        return L_FAIL(h._runtime, BL_BUSY, "Node is attached");
    }
    for (size_t i = 0; i < h._runtime->count; ++i)
    {
        L_Record* p = h._runtime->records[i];
        if (!p)
        {
            continue;
        }
        if (!p->disposed && p->kind == L_SCENE &&
            ((L_Scene*)p)->properties.camera._id == n->record.id)
        {
            return BL_BUSY;
        }
        if (!p->disposed && (p->kind == L_NODE || p->kind == L_CAMERA || p->kind == L_MESH))
        {
            L_Node* other = (L_Node*)p;
            if (other->parent == n)
            {
                return BL_BUSY;
            }
            for (size_t j = 0; j < other->childCount; ++j)
            {
                if (other->children[j]._id == h._id)
                {
                    return BL_BUSY;
                }
            }
        }
    }
    n->record.cleanup(h._runtime, &n->record);
    n->record.disposed = true;
    return BL_OK;
}
