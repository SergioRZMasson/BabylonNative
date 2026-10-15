#include "AssetInternal.h"

int l_assetInfluence(bl_String semantic, bool* joints)
{
    if (!l_string(semantic) || semantic.length < 7)
    {
        return -1;
    }
    bool joint = !memcmp(semantic.data, "JOINTS_", 7);
    bool weight = semantic.length >= 8 && !memcmp(semantic.data, "WEIGHTS_", 8);
    if (!joint && !weight)
    {
        return -1;
    }
    *joints = joint;
    size_t first = joint ? 7 : 8;
    if (semantic.length == first || (semantic.length > first + 1 && semantic.data[first] == '0'))
    {
        return -2;
    }
    unsigned value = 0;
    for (size_t i = first; i < semantic.length; ++i)
    {
        unsigned digit = (unsigned)(semantic.data[i] - '0');
        if (digit > 9 || value > (INT32_MAX - digit) / 10)
        {
            return -2;
        }
        value = value * 10 + digit;
    }
    return (int)value;
}
