#ifndef BL_ASSET_MOTION_INPUT_H
#define BL_ASSET_MOTION_INPUT_H

#include "babylon_lite.h"

// Preserved construction workspace for the paused motion implementation.
// This is not an advertised asset-service protocol or a public parser schema.
#define BL_ASSET_SERVICE_VERSION_6 UINT32_C(6)
#define BL_ASSET_DOCUMENT_VERSION_6 UINT32_C(6)

struct bl_AssetSkin
{
    bool hasName;
    bl_String name;
    const uint32_t* joints;
    size_t jointCount;
    bl_AssetOptionalIndex inverseBindMatrices;
    bl_AssetOptionalIndex skeleton;
    uint64_t unsupportedFeatures;
};

struct bl_AssetAnimationSampler
{
    uint32_t input;
    uint32_t output;
    bool hasInterpolation;
    bl_String interpolation;
    uint64_t unsupportedFeatures;
};

struct bl_AssetAnimationChannel
{
    uint32_t sampler;
    bl_AssetOptionalIndex node;
    bl_String path;
    uint64_t unsupportedFeatures;
};

struct bl_AssetAnimation
{
    bool hasName;
    bl_String name;
    const bl_AssetAnimationSampler* samplers;
    size_t samplerCount;
    const bl_AssetAnimationChannel* channels;
    size_t channelCount;
    uint64_t unsupportedFeatures;
};

struct bl_ParsedGltf6
{
    bl_ParsedGltf5 document5;
    const bl_AssetSkin* skins;
    const bl_AssetAnimation* animations;
};

#endif
