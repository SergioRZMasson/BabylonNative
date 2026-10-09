#include "LiteInternal.h"

static bl_Status reserveDraws(bl_Runtime* r, L_Scene* s)
{
    if (s->memberCount <= s->drawCapacity)
    {
        return BL_OK;
    }
    size_t capacity = s->drawCapacity ? s->drawCapacity : 16;
    while (capacity < s->memberCount)
    {
        if (capacity > SIZE_MAX / 2)
        {
            return L_FAIL(r, BL_OUT_OF_MEMORY, "Draw capacity overflow");
        }
        capacity *= 2;
    }
    size_t drawBytes;
    size_t groupBytes;
    if (capacity > SIZE_MAX / 2 || !l_size(capacity, sizeof(L_Draw), &drawBytes) ||
        !l_size(capacity * 2, sizeof(L_DrawGroup), &groupBytes))
    {
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Draw scratch size overflow");
    }
    L_Draw* draws = (L_Draw*)l_alloc(r, drawBytes);
    if (!draws)
    {
        return BL_OUT_OF_MEMORY;
    }
    L_DrawGroup* groups = (L_DrawGroup*)l_alloc(r, groupBytes);
    if (!groups)
    {
        l_free(r, draws);
        return BL_OUT_OF_MEMORY;
    }
    l_free(r, s->drawScratch);
    l_free(r, s->groupScratch);
    s->drawScratch = draws;
    s->groupScratch = groups;
    s->drawCapacity = capacity;
    s->groupCapacity = capacity * 2;
    return BL_OK;
}

static L_DrawGroup* drawGroup(L_Scene* s, uint64_t materialId)
{
    // Include the generation; recycled registry slots are different material identities.
    uint64_t hash = materialId ^ (materialId >> 32);
    hash *= UINT64_C(0x9e3779b97f4a7c15);
    size_t index = (size_t)(hash ^ (hash >> 32)) & (s->groupCapacity - 1);
    while (s->groupScratch[index].materialId && s->groupScratch[index].materialId != materialId)
    {
        index = (index + 1) & (s->groupCapacity - 1);
    }
    return s->groupScratch + index;
}

bl_Status l_collectDraws(bl_Runtime* r, L_Scene* s, const bl_Mat4* view, size_t* out)
{
    L_TRY(l_prepareStandardPackets(r, s, false, 0));
    L_TRY(reserveDraws(r, s));
    if (s->groupCapacity)
    {
        memset(s->groupScratch, 0, s->groupCapacity * sizeof(*s->groupScratch));
    }
    for (size_t i = 0; i < s->memberCount; ++i)
    {
        L_Node* n = s->members[i];
        if (n->record.disposed || n->record.kind != L_MESH)
        {
            continue;
        }
        L_Mesh* mesh = (L_Mesh*)n;
        bl_Material identity = l_meshMaterial(mesh);
        if (L_NULL(identity))
        {
            continue;
        }
        L_Record* record = l_peek(r, identity._id);
        if (!record || record->disposed ||
            (record->kind != L_MATERIAL && record->kind != L_STANDARD))
        {
            return L_FAIL(r, BL_INVALID_HANDLE, "Scene material identity is not live");
        }
        L_Material* material = (L_Material*)record;
        double order = mesh->properties.renderOrder.present ? mesh->properties.renderOrder.value
                       : material->blending                 ? 200.0
                                                            : 100.0;
        if (!material->blending && record->kind == L_MATERIAL)
        {
            // All members participate, even hidden or empty meshes. Each duplicate still draws.
            L_DrawGroup* group = drawGroup(s, record->id);
            if (!group->materialId)
            {
                *group = {record->id, order, i};
            }
            else if (order < group->order)
            {
                group->order = order;
            }
        }
    }
    L_Draw* draws = (L_Draw*)s->drawScratch;
    size_t count = 0;
    for (size_t i = 0; i < s->memberCount; ++i)
    {
        L_Node* n = s->members[i];
        if (n->record.disposed || n->record.kind != L_MESH || !n->visible)
        {
            continue;
        }
        L_Mesh* mesh = (L_Mesh*)n;
        bl_Material identity = l_meshMaterial(mesh);
        if (L_NULL(identity) || !mesh->geometry.vertices || !mesh->geometry.indexCount)
        {
            continue;
        }
        L_Material* material = (L_Material*)l_peek(r, identity._id);
        L_StandardPacket* standard =
            material->record.kind == L_STANDARD ? s->standardPackets + i : NULL;
        bool transparent = standard ? standard->transparent : material->blending;
        double order = mesh->properties.renderOrder.present ? mesh->properties.renderOrder.value
                       : transparent                        ? 200.0
                                                            : 100.0;
        L_TRY(l_world(r, n));
        double depth = view->values[2] * n->world.values[12] +
                       view->values[6] * n->world.values[13] +
                       view->values[10] * n->world.values[14] + view->values[14];
        draws[count++] = {mesh, material, order, depth, i, i, transparent, standard};
    }
    for (size_t i = 0; i < count; ++i)
    {
        if (!draws[i].transparent && draws[i].material->record.kind == L_MATERIAL)
        {
            L_DrawGroup* group = drawGroup(s, draws[i].material->record.id);
            draws[i].order = group->order;
            draws[i].group = group->sequence;
        }
    }
    *out = count;
    return BL_OK;
}

static int drawCompare(const void* ap, const void* bp)
{
    const L_Draw* a = (const L_Draw*)ap;
    const L_Draw* b = (const L_Draw*)bp;
    if (a->transparent != b->transparent)
    {
        return a->transparent ? 1 : -1;
    }
    if (a->transparent && a->depth != b->depth)
    {
        return a->depth > b->depth ? -1 : 1;
    }
    if (a->order != b->order)
    {
        return a->order < b->order ? -1 : 1;
    }
    if (!a->transparent && a->group != b->group)
    {
        return a->group < b->group ? -1 : 1;
    }
    return a->sequence < b->sequence ? -1 : a->sequence != b->sequence ? 1 : 0;
}

void l_sortDraws(L_Scene* s, size_t count)
{
    if (count)
    {
        qsort(s->drawScratch, count, sizeof(L_Draw), drawCompare);
    }
}
