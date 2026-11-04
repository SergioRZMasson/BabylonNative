/*
 * Original Lite 1.32.0 math/compose-mat4-into-buffer.ts and
 * math/create-{identity,translation}-mat4.ts. Apache-2.0, source 2e064d88.
 */
#include "SceneInternal.h"

static double hypotValues(const double* values, size_t count)
{
    double maximum = 0;
    for (size_t i = 0; i < count; ++i)
    {
        double value = fabs(values[i]);
        if (value > maximum)
        {
            maximum = value;
        }
    }
    if (maximum == 0)
    {
        return 0;
    }
    // JS Math.hypot scales before its compensated sum; nested binary hypot
    // introduces an extra double rounding before the original F32 store.
    double sum = 0;
    double compensation = 0;
    for (size_t i = 0; i < count; ++i)
    {
        double value = fabs(values[i]) / maximum;
        double square = value * value - compensation;
        double next = sum + square;
        compensation = (next - sum) - square;
        sum = next;
    }
    return sqrt(sum) * maximum;
}

double l_hypot3(double x, double y, double z)
{
    const double values[3] = {x, y, z};
    return hypotValues(values, 3);
}

double l_hypot4(double x, double y, double z, double w)
{
    const double values[4] = {x, y, z, w};
    return hypotValues(values, 4);
}

bl_Status bl_createIdentityMat4(bl_Mat4* out)
{
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    l_identity(out);
    return BL_OK;
}

bl_Status bl_composeMat4(double tx, double ty, double tz, double x, double y, double z, double w,
                         double sx, double sy, double sz, bool precise, bl_Mat4* out)
{
    if (!out || !l_vec({tx, ty, tz}) || !l_quat({x, y, z, w}) || !l_vec({sx, sy, sz}))
    {
        return BL_INVALID_ARGUMENT;
    }
    double xx = x * x;
    double yy = y * y;
    double zz = z * z;
    double xy = x * y;
    double xz = x * z;
    double yz = y * z;
    double wx = w * x;
    double wy = w * y;
    double wz = w * z;
    bl_Mat4 matrix = {{(1 - 2 * (yy + zz)) * sx, 2 * (xy + wz) * sx, 2 * (xz - wy) * sx, 0,
                       2 * (xy - wz) * sy, (1 - 2 * (xx + zz)) * sy, 2 * (yz + wx) * sy, 0,
                       2 * (xz + wy) * sz, 2 * (yz - wx) * sz, (1 - 2 * (xx + yy)) * sz, 0, tx, ty,
                       tz, 1}};
    for (unsigned i = 0; i < 16; ++i)
    {
        if (!precise)
        {
            matrix.values[i] = (float)matrix.values[i];
        }
        if (!isfinite(matrix.values[i]))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    *out = matrix;
    return BL_OK;
}

bl_Status bl_createTranslationMat4(double x, double y, double z, bool precise, bl_Mat4* out)
{
    return bl_composeMat4(x, y, z, 0, 0, 0, 1, 1, 1, 1, precise, out);
}
