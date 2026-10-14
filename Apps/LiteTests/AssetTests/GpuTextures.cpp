#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "AssetInternal.h"
#include <babylon_lite_asset_decoder.h>
#include "../LiteSandbox/AssetIO.h"
#include <filesystem>
#include <fstream>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

using Microsoft::WRL::ComPtr;

static void require(bool value, const char* message)
{
    if (!value)
    {
        fprintf(stderr, "Native texture contract: %s\n", message);
        exit(2);
    }
}

struct Memory
{
    size_t calls = 0;
    size_t failAt = 0;
    size_t live = 0;
    size_t bytes = 0;
};

static void* allocate(void* user, size_t size, size_t alignment)
{
    auto* memory = static_cast<Memory*>(user);
    if (++memory->calls == memory->failAt)
    {
        return nullptr;
    }
    void* result = _aligned_malloc(size, alignment);
    if (result)
    {
        ++memory->live;
        memory->bytes += size;
    }
    return result;
}

static void release(void* user, void* pointer, size_t bytes, size_t)
{
    if (pointer)
    {
        auto* memory = static_cast<Memory*>(user);
        --memory->live;
        memory->bytes -= bytes;
        _aligned_free(pointer);
    }
}

static bool initialized(void*)
{
    return true;
}

static bl_Status wait(void*)
{
    uint8_t bytes[4] = {};
    const uint8_t marker[4] = {13, 31, 61, 255};
    auto source = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                       bgfx::copy(marker, 4));
    auto target = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                       BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
    require(bgfx::isValid(source) && bgfx::isValid(target), "actual retirement fence resources");
    bgfx::TextureRegion from;
    from.init(source);
    bgfx::TextureRegion to;
    to.init(target);
    bgfx::blit(126, to, from);
    uint32_t ready = bgfx::read(to, bytes);
    uint32_t frame = bgfx::frame();
    while (static_cast<int32_t>(frame - ready) < 0)
    {
        frame = bgfx::frame();
    }
    require(!memcmp(bytes, marker, 4), "actual submitted work completed before release");
    bgfx::destroy(source);
    bgfx::destroy(target);
    return BL_OK;
}

struct Context
{
    Memory memory;
    bl_IOService base = SandboxAssetIO();
    bl_AssetService provider = {};
    bl_AssetDecoder* decoder = nullptr;
    bl_Runtime* runtime = nullptr;
    bl_EngineContext engine = {};
    size_t imageCalls = 0;
    size_t imageReleases = 0;
    size_t parseCalls = 0;
    size_t parseReleases = 0;
    size_t failureAt = 0;
    bl_Status failure = BL_OK;
    bool empty = false;
    bool malformed = false;
    const bl_ParsedGltf* prefix = nullptr;
};

static bl_Status image(void* user, bl_Bytes bytes, bl_HostImage* out)
{
    auto* context = static_cast<Context*>(user);
    bool selected = ++context->imageCalls == context->failureAt;
    if (selected && context->empty)
    {
        return context->failure;
    }
    bl_Status status = context->base.decodeImage(context->base.userData, bytes, out);
    if (selected && context->malformed)
    {
        out->rowStrideBytes = SIZE_MAX;
    }
    return selected ? context->failure : status;
}

static void releaseImage(void* user, bl_HostImage* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->imageReleases;
    context->base.releaseImage(context->base.userData, out);
}

static bl_Status parse(void* user, bl_Bytes bytes, bl_String base, bl_ParsedGltf* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->parseCalls;
    context->prefix = out;
    return context->provider.parseGltf(context->provider.userData, bytes, base, out);
}

static void releaseParsed(void* user, bl_ParsedGltf* out)
{
    auto* context = static_cast<Context*>(user);
    require(context->prefix == out, "same original full-v3 prefix passed to release");
    ++context->parseReleases;
    context->provider.releaseGltf(context->provider.userData, out);
}

static bl_Status parseEnvironment(void* user, bl_Bytes bytes, bl_ParsedEnvironment* out)
{
    auto* context = static_cast<Context*>(user);
    return context->provider.parseEnvironment(context->provider.userData, bytes, out);
}

static void releaseEnvironment(void* user, bl_ParsedEnvironment* out)
{
    auto* context = static_cast<Context*>(user);
    context->provider.releaseEnvironment(context->provider.userData, out);
}

static void setup(Context& context)
{
    bl_Allocator allocator = {&context.memory, allocate, release};
    require(bl_createAssetDecoder(&allocator, &context.decoder, &context.provider) == BL_OK,
            "native decoder allocation");
    require(bl_assetDecoderService3(context.decoder, &context.provider) == BL_OK, "advertised v3");
    bl_AssetService provider = context.provider;
    provider.userData = &context;
    provider.parseGltf = parse;
    provider.releaseGltf = releaseParsed;
    provider.parseEnvironment = parseEnvironment;
    provider.releaseEnvironment = releaseEnvironment;
    bl_IOService io = {};
    io.userData = &context;
    io.decodeImage = image;
    io.releaseImage = releaseImage;
    bl_RuntimeOptions2 options = {sizeof(options), 2, {allocator, nullptr, nullptr, nullptr,
                                                      nullptr, &io}, &provider, nullptr};
    require(bl_createRuntime2(&options, &context.runtime) == BL_OK, "native texture runtime");
    bl_NativeEngineOptions native = {};
    native.ownership = BL_BGFX_BORROWED;
    native.backend = BL_RENDERER_D3D11;
    native.target = {BL_TARGET_SWAPCHAIN, BL_INVALID_BGFX_HANDLE, 1280, 720,
                     BL_COLOR_BGRA8, BL_DEPTH_D24S8, 4, 0, 116};
    native.isExternalBgfxInitialized = initialized;
    native.waitForSubmittedWork = wait;
    require(bl_createEngine(context.runtime, &native, nullptr, &context.engine) == BL_OK,
            "native texture engine");
}

static void finish(Context& context)
{
    context.memory.failAt = 0;
    require(context.imageCalls == context.imageReleases, "every image invocation released once");
    require(context.parseCalls == context.parseReleases, "every parse invocation released once");
    require(bl_disposeRuntime(context.runtime) == BL_OK, "native GPU teardown");
    bl_destroyAssetDecoder(context.decoder);
    require(!context.memory.live && !context.memory.bytes, "all native/provider allocator bytes released");
}

static bl_Status load(Context& context, const std::vector<uint8_t>& bytes, bl_AssetContainer* out)
{
    bl_AssetSource source = {BL_ASSET_SOURCE_BYTES, {}, {bytes.data(), bytes.size()}};
    return bl_loadGltf(context.engine, &source, out);
}

static std::vector<uint8_t> readGpu(L_Texture* texture, uint8_t mip)
{
    uint16_t width = static_cast<uint16_t>(texture->info.width >> mip);
    uint16_t height = static_cast<uint16_t>(texture->info.height >> mip);
    width = width ? width : 1;
    height = height ? height : 1;
    auto staging = bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA8,
                                         BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
    require(bgfx::isValid(staging), "actual GPU staging image");
    bgfx::TextureRegion from;
    from.init(bgfx::TextureHandle{texture->handleIndex});
    from.mip = mip;
    from.width = width;
    from.height = height;
    bgfx::TextureRegion to;
    to.init(staging);
    bgfx::blit(120, to, from);
    std::vector<uint8_t> bytes(size_t(width) * height * 4);
    uint32_t ready = bgfx::read(to, bytes.data());
    uint32_t frame = bgfx::frame();
    while (static_cast<int32_t>(frame - ready) < 0)
    {
        frame = bgfx::frame();
    }
    bgfx::destroy(staging);
    return bytes;
}

int main(int argc, char** argv)
{
    require(argc == 3, "real embedded JPEG+PNG GLB and new result file");
    std::ifstream file(std::filesystem::path(argv[1]), std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    require(!bytes.empty(), "unchanged real GLB input");
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "native image COM");
    HWND window = CreateWindowExW(0, L"STATIC", L"Native loader resource tests", WS_POPUP,
                                   0, 0, 1280, 720, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    init.swapChain.nwh = window;
    init.swapChain.width = 1280;
    init.swapChain.height = 720;
    init.swapChain.formatColor = bgfx::TextureFormat::BGRA8;
    init.swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8;
    init.swapChain.flags = BGFX_SWAP_CHAIN_MSAA_X4;
    require(bgfx::init(init), "real native GPU");
    unsigned resourceProofs = 0;
    {
        Context context;
        setup(context);
        bl_AssetContainer asset = {};
        require(load(context, bytes, &asset) == BL_OK, "real embedded image load");
        auto* owner = reinterpret_cast<L_Asset*>(l_peek(context.runtime, asset._id));
        require(owner->materialCount == 2, "real material/default slots");
        auto* material = reinterpret_cast<L_Material*>(l_peek(context.runtime, owner->materials[1]._id));
        require(material->pbrBaseImage && material->pbrOrmImage, "both authored inputs present");
        bl_ParsedGltf3 parsed = {};
        parsed.document.structSize = sizeof(parsed);
        parsed.document.version = BL_ASSET_DOCUMENT_VERSION_3;
        require(context.provider.parseGltf(context.provider.userData, {bytes.data(), bytes.size()},
                                           {}, &parsed.document) == BL_OK, "independent raw descriptors");
        const bl_AssetTextureInfo* inputs[2] = {&parsed.materialInputs[0].baseColor,
                                                &parsed.materialInputs[0].metallicRoughness};
        bl_Texture2D handles[2] = {material->pbrInfo.baseColorTexture, material->pbrInfo.ormTexture};
        for (unsigned i = 0; i < 2; ++i)
        {
            const auto& texture = parsed.textures[inputs[i]->index];
            const auto& image = parsed.images[texture.source.value];
            const auto& view = parsed.document.bufferViews[image.bufferView.value];
            bl_HostImage decoded = {};
            require(context.base.decodeImage(context.base.userData,
                {parsed.document.buffers[view.buffer].bytes.data + view.byteOffset, view.byteLength},
                &decoded) == BL_OK, "independent JPEG/PNG decode");
            auto* record = reinterpret_cast<L_Texture*>(l_peek(context.runtime, handles[i]._id));
            auto gpu = readGpu(record, 0);
            require(record->info.width == decoded.width && record->info.height == decoded.height &&
                        record->mipCount > 1 &&
                        record->info.srgb == (i == 0), "actual dimensions/mips/color-space metadata");
            for (uint32_t y = 0; y < decoded.height; ++y)
            {
                require(!memcmp(gpu.data() + size_t(y) * decoded.width * 4,
                                decoded.rgba8 + size_t(y) * decoded.rowStrideBytes,
                                size_t(decoded.width) * 4), "authored base-level GPU pixels byte exact");
            }
            size_t offset = 0;
            for (uint8_t mip = 0; mip < record->mipCount; ++mip)
            {
                auto physical = readGpu(record, mip);
                require(!memcmp(physical.data(), record->mipPixels + offset, physical.size()),
                        "every native physical mip matches supplied complete chain");
                offset += physical.size();
            }
            context.base.releaseImage(context.base.userData, &decoded);
            ++resourceProofs;
        }
        bl_Material shared = {};
        require(l_createPbrFactorMaterial(context.engine, parsed.document.materials, &shared) == BL_OK &&
                    l_applyAssetMaterialInputs(owner, &parsed, 0, shared) == BL_OK,
                "second material shares actual image cache");
        auto* second = reinterpret_cast<L_Material*>(l_peek(context.runtime, shared._id));
        require(second->pbrInfo.baseColorTexture._id == handles[0]._id &&
                    second->pbrInfo.ormTexture._id == handles[1]._id,
                "source image/material sharing preserves texture identities");
        require(bl_disposePbrMaterial({context.runtime, shared._id}) == BL_OK &&
                    !l_peek(context.runtime, handles[0]._id)->disposed,
                "another material release retains original image owner");
        const uint8_t changed[4] = {41, 53, 79, 255};
        require(bl_updateTexture2DFromPixels(context.engine, handles[0], {changed, 4}, 0, 0, 1, 1) ==
                    BL_OK, "existing pixel-update API updates authored mip texture");
        auto* updated = reinterpret_cast<L_Texture*>(l_peek(context.runtime, handles[0]._id));
        auto updatedBase = readGpu(updated, 0);
        require(!memcmp(updatedBase.data(), changed, 4), "actual base pixel update");
        size_t updatedOffset = 0;
        for (uint8_t mip = 0; mip < updated->mipCount; ++mip)
        {
            auto physical = readGpu(updated, mip);
            require(!memcmp(physical.data(), updated->mipPixels + updatedOffset, physical.size()),
                    "all physical mip levels refreshed by pixel update");
            updatedOffset += physical.size();
        }
        context.provider.releaseGltf(context.provider.userData, &parsed.document);
        require(bl_disposeAssetContainer(asset) == BL_OK, "last image/material owner retires safely");
        finish(context);
    }
    unsigned faultCases = 0;
    for (unsigned at = 1; at <= 2; ++at)
    {
        for (bool empty : {false, true})
        {
            for (auto failure : {BL_CANCELLED, BL_HOST_ERROR, BL_OUT_OF_MEMORY})
            {
                Context context;
                setup(context);
                context.failureAt = at;
                context.failure = failure;
                context.empty = empty;
                bl_AssetContainer out = {context.runtime, UINT64_MAX};
                require(load(context, bytes, &out) == failure && out._id == UINT64_MAX,
                        "partial/empty image failure keeps output unchanged");
                finish(context);
                ++faultCases;
            }
        }
    }
    {
        Context context;
        setup(context);
        context.failureAt = 1;
        context.malformed = true;
        bl_AssetContainer out = {context.runtime, UINT64_MAX};
        require(load(context, bytes, &out) == BL_INVALID_ARGUMENT && out._id == UINT64_MAX,
                "malformed actual image row span rejected");
        finish(context);
        ++faultCases;
    }
    unsigned allocationCases = 0;
    for (size_t failure = 1; failure < 256; ++failure)
    {
        Context context;
        setup(context);
        context.memory.failAt = context.memory.calls + failure;
        bl_AssetContainer out = {context.runtime, UINT64_MAX};
        auto status = load(context, bytes, &out);
        require(status == BL_OK || status == BL_OUT_OF_MEMORY, "all tracked setup OOM positions propagate");
        if (status != BL_OK)
        {
            require(out._id == UINT64_MAX, "OOM does not publish partial asset");
        }
        finish(context);
        if (status == BL_OK)
        {
            break;
        }
        ++allocationCases;
    }
    {
        Context context;
        setup(context);
        size_t warmBytes = 0;
        size_t warmSlots = 0;
        for (unsigned i = 0; i < 16; ++i)
        {
            bl_AssetContainer asset = {};
            require(load(context, bytes, &asset) == BL_OK &&
                        bl_disposeAssetContainer(asset) == BL_OK,
                    "repeated actual image/material owner load and retirement");
            if (i == 4)
            {
                warmBytes = context.memory.bytes;
                warmSlots = context.runtime->count;
            }
            if (i > 4)
            {
                require(context.memory.bytes == warmBytes && context.runtime->count == warmSlots,
                        "actual textured-owner allocator and identity slots plateau after warmup");
            }
        }
        finish(context);
    }
    std::ofstream result(argv[2]);
    result << "{\"realJpegPngGpuResourcesByteExact\":" << resourceProofs
           << ",\"sharedMaterialTextureIdentities\":true,\"partialEmptyImageFaults\":" << faultCases
           << ",\"trackedAllocationFailurePositions\":" << allocationCases
           << ",\"allPhysicalMipsUploadedAndPixelUpdateRefreshed\":true,"
              "\"texturedOwnerChurnIterations\":16,\"texturedOwnerPlateau\":true,"
              "\"exactOnceImageAndParseRelease\":true,\"allAllocatorTeardownZero\":true}\n";
    wait(nullptr);
    bgfx::shutdown();
    DestroyWindow(window);
    CoUninitialize();
    printf("Native GLB image resources%u faults%u OOM%u all releases/teardown passed\n",
            resourceProofs, faultCases, allocationCases);
}
