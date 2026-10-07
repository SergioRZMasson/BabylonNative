#include "UserRuntimeData.h"
#include <cstdint>
#include <cstdio>

namespace bblscene
{
    void generateChunk(double seed, double cx, double cz, bbl::js::U8Array& data);
}

int main()
{
    uint64_t digest = 1469598103934665603ull;
    uint64_t nonzero{};
    uint32_t chunks{};
    for (int z = -6; z <= 6; ++z)
    {
        for (int x = -6; x <= 6; ++x)
        {
            bbl::js::U8Array blocks(16 * 16 * 96);
            bblscene::generateChunk(1337, x, z, blocks);
            for (size_t index = 0; index < blocks.size(); ++index)
            {
                const uint8_t block = blocks[index];
                digest = (digest ^ block) * 1099511628211ull;
                nonzero += block != 0;
            }
            ++chunks;
        }
    }
    std::printf(
        "Original generated user worldgen: seed=1337 radius=6 chunks=%u nonzero=%llu hash=%llu\n",
        chunks, static_cast<unsigned long long>(nonzero), static_cast<unsigned long long>(digest));
    return chunks == 169 && nonzero == 1392067 && digest == 3315049788837698003ull ? 0 : 1;
}
