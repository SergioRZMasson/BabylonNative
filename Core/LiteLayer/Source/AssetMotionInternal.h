#ifndef BL_ASSET_MOTION_INTERNAL_H
#define BL_ASSET_MOTION_INTERNAL_H

#include "AssetInternal.h"

struct L_AssetSampler
{
    float* times;
    float* values;
    size_t count;
    size_t components;
    unsigned interpolation;
};

struct L_AssetChannel
{
    uint32_t sampler;
    uint32_t node;
    unsigned path;
};

struct L_AssetAnimation
{
    L_Record record;
    L_Asset* asset;
    bl_AnimationGroupInfo info;
    L_AssetSampler* samplers;
    size_t samplerCount;
    L_AssetChannel* channels;
    size_t channelCount;
};

struct L_AssetSkinData
{
    uint32_t* joints;
    size_t count;
    float* inverseBind;
    float* palette;
};

struct L_AssetDeformation
{
    L_AssetDeformation* next;
    L_Asset* asset;
    L_Mesh* mesh;
    L_AssetSkinData* skin;
    uint32_t* joints;
    float* weights;
    size_t influences;
    float* bind[3];
    float* output[3];
    float* bitangents;
    float* gpuBitangents;
    uint8_t* packed;
    bgfx::TextureHandle bitangentTexture;
    uint16_t textureWidth;
    uint16_t textureHeight;
    bool gpuDirty;
};

struct L_AssetMotion
{
    size_t nodeCount;
    L_Node** nodes;
    int32_t* parents;
    uint32_t* order;
    bl_TransformNodeOptions* rest;
    bl_TransformNodeOptions* nextPose;
    bl_Mat4* locals;
    bl_Mat4* worlds;
    uint8_t* targets;
    uint8_t* matrixLocked;
    L_AssetSkinData* skins;
    size_t skinCount;
    bl_AnimationGroup* groups;
    size_t groupCount;
    L_AssetDeformation* deformations;
    uint64_t lastFrame;
};

bl_Status l_sampleAssetAnimation(const L_AssetSampler* sampler, double time, bool quaternion,
                                 float* result);
bl_Status l_evaluateAssetMotion(L_Asset* asset, L_AssetAnimation* group, double time, bool reset);
bl_Status l_prepareAssetDeformation(L_Asset* asset);
void l_commitAssetDeformation(L_Asset* asset);
void l_freeAssetDeformation(bl_Runtime* runtime, L_AssetDeformation* deformation);
void l_composeAssetPose(const bl_TransformNodeOptions* pose, bl_Mat4* result);
bl_Status l_normalizeAssetQuaternion(bl_Quat* quaternion);
unsigned l_assetAnimationPath(bl_String path);
unsigned l_assetInterpolation(const bl_AssetAnimationSampler* sampler);

#endif
