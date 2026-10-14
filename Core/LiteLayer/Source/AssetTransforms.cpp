#include "AssetInternal.h"

bl_Status l_composeAssetMatrix(const bl_AssetNode* source, bl_TransformNodeOptions* out)
{
    bl_TransformNodeOptions options = {{}, {0, 0, 0, 1}, {1, 1, 1}};
    if (!source->hasMatrix)
    {
        if (source->hasTranslation)
        {
            options.position = source->translation;
        }
        if (source->hasRotation)
        {
            options.rotationQuaternion = source->rotation;
        }
        if (source->hasScale)
        {
            options.scaling = source->scale;
        }
        *out = options;
        return BL_OK;
    }
    const double* matrix = source->matrix.values;
    for (unsigned i = 0; i < 16; ++i)
    {
        if (!isfinite(matrix[i]))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    if (matrix[3] || matrix[7] || matrix[11] || matrix[15] != 1)
    {
        return BL_UNSUPPORTED;
    }
    // Original matrix nodes retain a locked local matrix and default TRS properties.
    *out = options;
    return BL_OK;
}
