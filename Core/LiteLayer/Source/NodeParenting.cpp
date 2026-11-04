/*
 * Original Lite scene/set-parent.ts, math/invert-mat4.ts, decompose-mat4.ts
 * and create-quat-from-rotation-mat4.ts at 2e064d88 (Apache-2.0).
 */
#include "SceneInternal.h"

static bool inverse(const bl_Mat4* matrix, bool precise, bl_Mat4* out)
{
    const double* m = matrix->values;
    double b00 = m[0] * m[5] - m[1] * m[4];
    double b01 = m[0] * m[6] - m[2] * m[4];
    double b02 = m[0] * m[7] - m[3] * m[4];
    double b03 = m[1] * m[6] - m[2] * m[5];
    double b04 = m[1] * m[7] - m[3] * m[5];
    double b05 = m[2] * m[7] - m[3] * m[6];
    double b06 = m[8] * m[13] - m[9] * m[12];
    double b07 = m[8] * m[14] - m[10] * m[12];
    double b08 = m[8] * m[15] - m[11] * m[12];
    double b09 = m[9] * m[14] - m[10] * m[13];
    double b10 = m[9] * m[15] - m[11] * m[13];
    double b11 = m[10] * m[15] - m[11] * m[14];
    double det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (fabs(det) < 1e-10)
    {
        return false;
    }
    det = 1 / det;
    bl_Mat4 result = {
        {(m[5] * b11 - m[6] * b10 + m[7] * b09) * det, (m[2] * b10 - m[1] * b11 - m[3] * b09) * det,
         (m[13] * b05 - m[14] * b04 + m[15] * b03) * det,
         (m[10] * b04 - m[9] * b05 - m[11] * b03) * det,
         (m[6] * b08 - m[4] * b11 - m[7] * b07) * det, (m[0] * b11 - m[2] * b08 + m[3] * b07) * det,
         (m[14] * b02 - m[12] * b05 - m[15] * b01) * det,
         (m[8] * b05 - m[10] * b02 + m[11] * b01) * det,
         (m[4] * b10 - m[5] * b08 + m[7] * b06) * det, (m[1] * b08 - m[0] * b10 - m[3] * b06) * det,
         (m[12] * b04 - m[13] * b02 + m[15] * b00) * det,
         (m[9] * b02 - m[8] * b04 - m[11] * b00) * det,
         (m[5] * b07 - m[4] * b09 - m[6] * b06) * det, (m[0] * b09 - m[1] * b07 + m[2] * b06) * det,
         (m[13] * b01 - m[12] * b03 - m[14] * b00) * det,
         (m[8] * b03 - m[9] * b01 + m[10] * b00) * det}};
    if (!precise)
    {
        for (unsigned i = 0; i < 16; ++i)
        {
            result.values[i] = (float)result.values[i];
        }
    }
    *out = result;
    return true;
}

static bl_Quat rotation(double a, double b, double c, double d, double e, double f, double g,
                        double h, double i)
{
    double trace = a + e + i;
    bl_Quat q;
    if (trace > 0)
    {
        double s = .5 / sqrt(trace + 1);
        q = {(h - f) * s, (c - g) * s, (d - b) * s, .25 / s};
    }
    else if (a > e && a > i)
    {
        double s = 2 * sqrt(1 + a - e - i);
        q = {.25 * s, (b + d) / s, (c + g) / s, (h - f) / s};
    }
    else if (e > i)
    {
        double s = 2 * sqrt(1 + e - a - i);
        q = {(b + d) / s, .25 * s, (f + h) / s, (c - g) / s};
    }
    else
    {
        double s = 2 * sqrt(1 + i - a - e);
        q = {(c + g) / s, (f + h) / s, .25 * s, (d - b) / s};
    }
    double invLength = 1 / l_hypot4(q.x, q.y, q.z, q.w);
    return {q.x * invLength, q.y * invLength, q.z * invLength, q.w * invLength};
}

static bool decompose(const bl_Mat4* matrix, bl_TransformNodeOptions* out)
{
    const double* m = matrix->values;
    for (unsigned i = 0; i < 16; ++i)
    {
        if (!isfinite(m[i]))
        {
            return false;
        }
    }
    double sx = l_hypot3(m[0], m[1], m[2]);
    double sy = l_hypot3(m[4], m[5], m[6]);
    double sz = l_hypot3(m[8], m[9], m[10]);
    double det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[4] * (m[1] * m[10] - m[2] * m[9]) +
                 m[8] * (m[1] * m[6] - m[2] * m[5]);
    double iy = sy > 1e-8 ? 1 / sy : 0;
    if (det < 0)
    {
        sy = -sy;
        iy = -iy;
    }
    double ix = sx > 1e-8 ? 1 / sx : 0;
    double iz = sz > 1e-8 ? 1 / sz : 0;
    bl_Quat q = rotation(m[0] * ix, m[4] * iy, m[8] * iz, m[1] * ix, m[5] * iy, m[9] * iz,
                         m[2] * ix, m[6] * iy, m[10] * iz);
    if (!l_quat(q) || !l_vec({sx, sy, sz}))
    {
        return false;
    }
    *out = {{m[12], m[13], m[14]}, q, {sx, sy, sz}};
    return true;
}

bl_Status bl_setParent(bl_SceneNode h, bl_SceneNode parentHandle)
{
    L_NODE_GET(h, node);
    L_Node* parent = NULL;
    bl_Runtime* r = h._runtime;
    if (node->assetParentLocked || (node->parent && node->parent->assetChildrenLocked))
    {
        return BL_UNSUPPORTED;
    }
    if (!L_NULL(parentHandle))
    {
        if (parentHandle._runtime != r)
        {
            return BL_WRONG_RUNTIME;
        }
        L_TRY(l_node(parentHandle, &parent));
        if (parent->assetChildrenLocked)
        {
            return BL_UNSUPPORTED;
        }
        for (L_Node* ancestor = parent; ancestor; ancestor = ancestor->parent)
        {
            if (ancestor == node)
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    L_TRY(l_world(r, node));
    bl_Mat4 oldWorld = node->world;
    bl_Mat4 local = oldWorld;
    bool preserve = true;
    bl_TransformNodeOptions trs = {node->position, node->quaternion, node->scale};
    if (parent)
    {
        L_TRY(l_world(r, parent));
        bl_Mat4 inv;
        preserve = inverse(&parent->world, node->highPrecision, &inv);
        if (preserve)
        {
            l_multiply(&inv, &oldWorld, &local, node->highPrecision);
        }
        else
        {
            if ((node->localMatrixLocked || node->localMatrixOverride) &&
                !decompose(&node->lockedLocalMatrix, &trs))
            {
                return BL_INVALID_ARGUMENT;
            }
            trs.position = {oldWorld.values[12], oldWorld.values[13], oldWorld.values[14]};
        }
    }
    if (preserve && !decompose(&local, &trs))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (parent != node->parent)
    {
        bool found = false;
        if (parent)
        {
            for (size_t i = 0; i < parent->childCount; ++i)
            {
                found |= parent->children[i]._id == h._id;
            }
            if (!found)
            {
                L_TRY(bl_appendNodeChild(parentHandle, h));
            }
        }
        if (node->parent)
        {
            L_TRY(bl_removeNodeChild({r, node->parent->record.id}, h));
        }
    }
    l_parent(node, parent);
    node->position = trs.position;
    node->quaternion = trs.rotationQuaternion;
    node->scale = trs.scaling;
    node->localMatrixLocked = false;
    node->localMatrixOverride = preserve;
    node->lockedLocalMatrix = local;
    node->eulerValid = false;
    node->dirty = true;
    return BL_OK;
}
