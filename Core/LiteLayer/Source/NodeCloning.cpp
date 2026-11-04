/* Original scene/transform-node.ts, Lite 1.32.0, 2e064d88 (Apache-2.0). */
#include "SceneInternal.h"

struct L_CloneFrame
{
    L_Node* source;
    L_Node* clone;
    size_t nextChild;
};

static bl_Status shallowClone(bl_Runtime* r, L_Node* source, L_Node** out)
{
    if (source->name.length > SIZE_MAX - 7)
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t bytes = source->name.length + 7;
    char* name = (char*)l_alloc(r, bytes);
    if (!name)
    {
        return BL_OUT_OF_MEMORY;
    }
    if (source->name.length)
    {
        memcpy(name, source->name.data, source->name.length);
    }
    memcpy(name + source->name.length, "_clone", 7);
    L_Record* cloned = NULL;
    bl_Status status;
    if (source->record.clone)
    {
        status = source->record.clone(r, &source->record, {name, bytes - 1}, &cloned);
    }
    else if (source->record.kind == L_NODE)
    {
        const bl_TransformNodeOptions trs = {source->position, source->quaternion, source->scale};
        bl_TransformNode node;
        status = bl_createTransformNode(r, {name, bytes - 1}, &trs, &node);
        if (status == BL_OK)
        {
            cloned = l_peek(r, node._id);
        }
    }
    else
    {
        status = L_FAIL(r, BL_UNSUPPORTED, "This node family has no original clone constructor");
    }
    l_free(r, name);
    if (status != BL_OK)
    {
        return status;
    }
    L_Node* node = (L_Node*)cloned;
    node->position = source->position;
    node->quaternion = source->quaternion;
    node->scale = source->scale;
    node->highPrecision = r->matrixPrecision;
    node->eulerValid = false;
    node->localMatrixLocked = source->localMatrixLocked;
    node->localMatrixOverride = source->localMatrixOverride;
    node->lockedLocalMatrix = source->lockedLocalMatrix;
    if (source->record.clone)
    {
        node->visible = source->visible;
    }
    node->dirty = true;
    *out = node;
    return BL_OK;
}

static void discard(bl_Runtime* r, L_Node* root, L_CloneFrame* frames)
{
    size_t depth = 1;
    frames[0] = {NULL, root, 0};
    while (depth)
    {
        L_CloneFrame* f = frames + depth - 1;
        if (f->nextChild < f->clone->childCount)
        {
            L_Node* child = (L_Node*)l_peek(r, f->clone->children[f->nextChild++]._id);
            frames[depth++] = {NULL, child, 0};
        }
        else
        {
            l_parent(f->clone, NULL);
            f->clone->record.cleanup(r, &f->clone->record);
            f->clone->record.disposed = true;
            --depth;
        }
    }
}

bl_Status bl_cloneTransformNode(bl_SceneNode h, bl_SceneNode* out)
{
    L_NODE_GET(h, source);
    bl_Runtime* r = h._runtime;
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t capacity = r->count + 1;
    size_t bytes;
    if (!l_size(capacity, sizeof(L_CloneFrame), &bytes) || !l_allocationFits(bytes))
    {
        return BL_INVALID_ARGUMENT;
    }
    L_CloneFrame* frames = (L_CloneFrame*)l_alloc(r, bytes);
    if (!frames)
    {
        return BL_OUT_OF_MEMORY;
    }
    L_Node* root;
    bl_Status status = shallowClone(r, source, &root);
    if (status != BL_OK)
    {
        l_free(r, frames);
        return status;
    }
    size_t depth = 1;
    frames[0] = {source, root, 0};
    while (depth && status == BL_OK)
    {
        L_CloneFrame* frame = frames + depth - 1;
        if (frame->nextChild == frame->source->childCount)
        {
            --depth;
            continue;
        }
        if (depth == capacity)
        {
            status = BL_INVALID_ARGUMENT;
            break;
        }
        L_Node* child;
        status = l_node(frame->source->children[frame->nextChild++], &child);
        if (status != BL_OK)
        {
            break;
        }
        L_Node* copy;
        status = shallowClone(r, child, &copy);
        if (status == BL_OK)
        {
            status = bl_appendNodeChild({r, frame->clone->record.id}, {r, copy->record.id});
            if (status == BL_OK)
            {
                l_parent(copy, frame->clone);
                frames[depth++] = {child, copy, 0};
            }
            else
            {
                copy->record.cleanup(r, &copy->record);
                copy->record.disposed = true;
            }
        }
    }
    if (status == BL_OK)
    {
        *out = {r, root->record.id};
    }
    else
    {
        discard(r, root, frames);
    }
    l_free(r, frames);
    return status;
}
