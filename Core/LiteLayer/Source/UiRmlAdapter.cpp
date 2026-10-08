#include "UiInternal.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/StyleTypes.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Input.h>
#include <bx/os.h>
#include <bx/cpu.h>
#include <new>
#include <stdio.h>

struct U_File
{
    bl_Runtime* runtime;
    bl_HostBuffer buffer;
    size_t position;
};

struct U_Font
{
    U_Font* next;
    size_t bytes;
    uint8_t* data;
    char* family;
    uint32_t weight;
    bool italic;
    bool fallback;
};

struct U_Listener
{
    U_Listener* next;
    U_Context* context;
    uint64_t elementId;
    uint64_t token;
    bl_UiEventKind kind;
    bl_UiEventCallback callback;
    void* user;
    void* adapter;
    void* attachment;
};

static U_Context* activeContext;
static U_Context* contexts;
static U_Font* fonts;
static volatile uint32_t uiThread;
static bool rmlInitialized;
static bool rmlPoisoned;
static double globalTime;

static Rml::String rmlString(bl_String s)
{
    return Rml::String(s.data ? s.data : "", s.length);
}

static const char* eventNames[] = {"click", "change", "mousedown", "mouseup", "keydown", "keyup"};

static const Rml::Input::KeyIdentifier keys[] = {
    Rml::Input::KI_UNKNOWN, Rml::Input::KI_BACK,  Rml::Input::KI_TAB,  Rml::Input::KI_RETURN,
    Rml::Input::KI_ESCAPE,  Rml::Input::KI_SPACE, Rml::Input::KI_LEFT, Rml::Input::KI_RIGHT,
    Rml::Input::KI_UP,      Rml::Input::KI_DOWN,  Rml::Input::KI_HOME, Rml::Input::KI_END,
    Rml::Input::KI_DELETE,  Rml::Input::KI_A,     Rml::Input::KI_C,    Rml::Input::KI_V,
    Rml::Input::KI_X,       Rml::Input::KI_Y,     Rml::Input::KI_Z};

static int inputModifiers(uint32_t modifiers)
{
    int result = 0;
    if (modifiers & BL_UI_MOD_SHIFT)
    {
        result |= Rml::Input::KM_SHIFT;
    }
    if (modifiers & BL_UI_MOD_CONTROL)
    {
        result |= Rml::Input::KM_CTRL;
    }
    if (modifiers & BL_UI_MOD_ALT)
    {
        result |= Rml::Input::KM_ALT;
    }
    if (modifiers & BL_UI_MOD_META)
    {
        result |= Rml::Input::KM_META;
    }
    return result;
}

class U_SystemInterface final : public Rml::SystemInterface
{
public:
    double GetElapsedTime() override
    {
        return globalTime;
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
    {
        if (activeContext && type <= Rml::Log::LT_WARNING)
        {
            u_pending(activeContext, strstr(message.c_str(), "font") ? BL_NOT_READY : BL_HOST_ERROR,
                      message.c_str());
        }
        return true;
    }

    void SetClipboardText(const Rml::String&) override
    {
        if (activeContext)
        {
            u_pending(activeContext, BL_UNSUPPORTED, "No UI clipboard service");
        }
    }

    void GetClipboardText(Rml::String& text) override
    {
        text.clear();
        if (activeContext)
        {
            u_pending(activeContext, BL_UNSUPPORTED, "No UI clipboard service");
        }
    }
};

class U_FileInterface final : public Rml::FileInterface
{
public:
    Rml::FileHandle Open(const Rml::String& path) override
    {
        U_Context* c = activeContext;
        if (!c || !c->runtime->hasIO || !c->runtime->io.readFile || !c->runtime->io.releaseBuffer)
        {
            if (c)
            {
                u_pending(c, BL_UNSUPPORTED, "UI resource requires runtime IO read/release");
            }
            return 0;
        }
        bl_Runtime* r = c->runtime;
        bl_String source = {path.data(), path.size()};
        if (!l_string(source) || !source.length)
        {
            u_pending(c, BL_INVALID_ARGUMENT, "Invalid UI resource path");
            return 0;
        }
        U_File* f = (U_File*)l_alloc(r, sizeof(U_File));
        if (!f)
        {
            u_pending(c, BL_OUT_OF_MEMORY, "UI file record allocation failed");
            return 0;
        }
        f->runtime = r;
        bl_Status status;
        L_CALL(r, status, r->io.readFile(r->io.userData, source, &f->buffer));
        if (status != BL_OK || !l_span(f->buffer.data, f->buffer.byteCount) ||
            f->buffer.byteCount > INT_MAX)
        {
            l_enterHost(r);
            r->io.releaseBuffer(r->io.userData, &f->buffer);
            l_leaveHost(r);
            l_free(r, f);
            u_pending(c, status != BL_OK ? status : BL_HOST_ERROR, "UI file read failed");
            return 0;
        }
        return (Rml::FileHandle)(uintptr_t)f;
    }

    void Close(Rml::FileHandle file) override
    {
        U_File* f = (U_File*)(uintptr_t)file;
        if (!f)
        {
            return;
        }
        bl_Runtime* r = f->runtime;
        l_enterHost(r);
        r->io.releaseBuffer(r->io.userData, &f->buffer);
        l_leaveHost(r);
        l_free(r, f);
    }

    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override
    {
        U_File* f = (U_File*)(uintptr_t)file;
        size_t remaining = f->buffer.byteCount - f->position;
        size_t count = size < remaining ? size : remaining;
        if (count)
        {
            memcpy(buffer, f->buffer.data + f->position, count);
            f->position += count;
        }
        return count;
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override
    {
        U_File* f = (U_File*)(uintptr_t)file;
        size_t base = 0;
        if (origin == SEEK_CUR)
        {
            base = f->position;
        }
        else if (origin == SEEK_END)
        {
            base = f->buffer.byteCount;
        }
        else if (origin != SEEK_SET)
        {
            return false;
        }
        if (offset < 0)
        {
            uint64_t magnitude = (uint64_t)(-(int64_t)offset);
            if (magnitude > base)
            {
                return false;
            }
            f->position = base - (size_t)magnitude;
        }
        else
        {
            if ((size_t)offset > f->buffer.byteCount - base)
            {
                return false;
            }
            f->position = base + (size_t)offset;
        }
        return true;
    }

    size_t Tell(Rml::FileHandle file) override
    {
        return ((U_File*)(uintptr_t)file)->position;
    }

    size_t Length(Rml::FileHandle file) override
    {
        return ((U_File*)(uintptr_t)file)->buffer.byteCount;
    }

    bool LoadFile(const Rml::String& path, Rml::String& data) override
    {
        Rml::FileHandle file = Open(path);
        if (!file)
        {
            return false;
        }
        U_File* f = (U_File*)(uintptr_t)file;
        bool success = false;
        try
        {
            data.assign(f->buffer.data ? (const char*)f->buffer.data : "", f->buffer.byteCount);
            success = true;
        }
        catch (const std::bad_alloc&)
        {
            u_pending(activeContext, BL_OUT_OF_MEMORY, "RmlUI file-string allocation failed");
        }
        Close(file);
        return success;
    }
};

static U_SystemInterface* systemInterface;
static U_FileInterface* fileInterface;

static U_Image* imageFind(U_Context* c, bl_String source)
{
    for (U_Image* image = c->images; image; image = image->next)
    {
        if (l_equal(image->source, source))
        {
            return image;
        }
    }
    return NULL;
}

static uint64_t straightTexture(U_Context* c, const uint8_t* pixels, uint32_t width,
                                uint32_t height, size_t stride)
{
    size_t bytes;
    if (!width || !height || !l_size((size_t)width * height, 4, &bytes) || bytes > UINT32_MAX ||
        stride < (size_t)width * 4 ||
        (height > 1 && stride > (SIZE_MAX - (size_t)width * 4) / (height - 1)))
    {
        u_resourceFailure(c, BL_INVALID_ARGUMENT, "Invalid UI RGBA8 image span");
        return 0;
    }
    uint8_t* premultiplied = (uint8_t*)l_alloc(c->runtime, bytes);
    if (!premultiplied)
    {
        u_resourceFailure(c, BL_OUT_OF_MEMORY, "UI image upload staging allocation failed");
        return 0;
    }
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint8_t* row = pixels + y * stride;
        uint8_t* output = premultiplied + (size_t)y * width * 4;
        for (uint32_t x = 0; x < width; ++x)
        {
            unsigned alpha = row[x * 4 + 3];
            output[x * 4] = (uint8_t)((row[x * 4] * alpha + 127) / 255);
            output[x * 4 + 1] = (uint8_t)((row[x * 4 + 1] * alpha + 127) / 255);
            output[x * 4 + 2] = (uint8_t)((row[x * 4 + 2] * alpha + 127) / 255);
            output[x * 4 + 3] = (uint8_t)alpha;
        }
    }
    uint64_t handle = u_texture(c, premultiplied, width, height);
    l_free(c->runtime, premultiplied);
    return handle;
}

class U_RenderInterface final : public Rml::RenderInterface
{
public:
    U_Context* owner;

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override
    {
        U_Context* c = owner;
        size_t vertexBytes;
        size_t indexBytes;
        if (!l_size(vertices.size(), sizeof(float) * 8, &vertexBytes) ||
            !l_size(indices.size(), sizeof(uint32_t), &indexBytes))
        {
            u_pending(c, BL_OUT_OF_MEMORY, "UI geometry staging overflow");
            return 0;
        }
        float* v = (float*)l_alloc(c->runtime, vertexBytes);
        uint32_t* ix = (uint32_t*)l_alloc(c->runtime, indexBytes);
        if (!v || !ix)
        {
            l_free(c->runtime, v);
            l_free(c->runtime, ix);
            u_pending(c, BL_OUT_OF_MEMORY, "UI geometry staging allocation failed");
            return 0;
        }
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            const Rml::Vertex& input = vertices[i];
            v[i * 8] = input.position.x;
            v[i * 8 + 1] = input.position.y;
            v[i * 8 + 2] = input.colour.red / 255.0f;
            v[i * 8 + 3] = input.colour.green / 255.0f;
            v[i * 8 + 4] = input.colour.blue / 255.0f;
            v[i * 8 + 5] = input.colour.alpha / 255.0f;
            v[i * 8 + 6] = input.tex_coord.x;
            v[i * 8 + 7] = input.tex_coord.y;
        }
        for (size_t i = 0; i < indices.size(); ++i)
        {
            ix[i] = (uint32_t)indices[i];
        }
        uint64_t handle = u_geometry(c, v, vertices.size(), ix, indices.size());
        l_free(c->runtime, v);
        l_free(c->runtime, ix);
        return (Rml::CompiledGeometryHandle)handle;
    }

    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override
    {
        const float offset[] = {translation.x, translation.y};
        u_draw(owner, (uint64_t)geometry, offset, (uint64_t)texture, 0);
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override
    {
        u_release(owner, (uint64_t)geometry, U_GEOMETRY);
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override
    {
        U_Context* c = owner;
        bl_String path = {source.data(), source.size()};
        if (!l_string(path) || !path.length)
        {
            u_pending(c, BL_INVALID_ARGUMENT, "Invalid UI image source");
            return 0;
        }
        U_Image* image = imageFind(c, path);
        if (image)
        {
            uint64_t handle = straightTexture(c, image->pixels, image->width, image->height,
                                              (size_t)image->width * 4);
            if (handle)
            {
                ++image->references;
                U_Resource* resource = u_resource(c, handle, U_TEXTURE);
                resource->image = image;
                if (image->pixelated)
                {
                    resource->samplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                             BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
                }
                dimensions = {(int)image->width, (int)image->height};
            }
            return (Rml::TextureHandle)handle;
        }
        bl_Runtime* r = c->runtime;
        if (!r->hasIO || !r->io.readFile || !r->io.releaseBuffer || !r->io.decodeImage ||
            !r->io.releaseImage)
        {
            u_resourceFailure(c, BL_NOT_READY,
                              "UI image is unregistered and runtime image IO is missing");
            return 0;
        }
        bl_HostBuffer encoded = {};
        bl_HostImage decoded = {};
        bool decodeCalled = false;
        bl_Status status;
        L_CALL(r, status, r->io.readFile(r->io.userData, path, &encoded));
        if (status == BL_OK && !l_span(encoded.data, encoded.byteCount))
        {
            status = BL_HOST_ERROR;
        }
        if (status == BL_OK)
        {
            bl_Bytes bytes = {encoded.data, encoded.byteCount};
            decodeCalled = true;
            L_CALL(r, status, r->io.decodeImage(r->io.userData, bytes, &decoded));
        }
        uint64_t handle = 0;
        if (status == BL_OK && decoded.rgba8)
        {
            handle = straightTexture(c, decoded.rgba8, decoded.width, decoded.height,
                                     decoded.rowStrideBytes);
            if (handle)
            {
                dimensions = {(int)decoded.width, (int)decoded.height};
            }
        }
        else
        {
            u_resourceFailure(c, status == BL_OK ? BL_HOST_ERROR : status,
                              "UI image read/decode failed");
        }
        l_enterHost(r);
        if (decodeCalled)
        {
            r->io.releaseImage(r->io.userData, &decoded);
        }
        r->io.releaseBuffer(r->io.userData, &encoded);
        l_leaveHost(r);
        return (Rml::TextureHandle)handle;
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> bytes,
                                       Rml::Vector2i dimensions) override
    {
        if (dimensions.x <= 0 || dimensions.y <= 0 ||
            (size_t)dimensions.x * dimensions.y > bytes.size() / 4)
        {
            u_pending(owner, BL_INVALID_ARGUMENT, "Invalid RmlUI premultiplied image span");
            return 0;
        }
        return (Rml::TextureHandle)u_texture(owner, bytes.data(), (uint32_t)dimensions.x,
                                             (uint32_t)dimensions.y);
    }

    void ReleaseTexture(Rml::TextureHandle texture) override
    {
        U_Resource* resource = u_resource(owner, (uint64_t)texture, U_TEXTURE);
        if (resource && resource->image)
        {
            --resource->image->references;
            resource->image = NULL;
        }
        u_release(owner, (uint64_t)texture, U_TEXTURE);
    }

    void EnableScissorRegion(bool enable) override
    {
        owner->scissorEnabled = enable;
    }

    void SetScissorRegion(Rml::Rectanglei region) override
    {
        owner->scissor[0] = region.Left();
        owner->scissor[1] = region.Top();
        owner->scissor[2] = region.Right();
        owner->scissor[3] = region.Bottom();
    }

    void SetTransform(const Rml::Matrix4f* transform) override
    {
        if (transform)
        {
            memcpy(owner->transform, transform->data(), sizeof(owner->transform));
        }
        else
        {
            memset(owner->transform, 0, sizeof(owner->transform));
            owner->transform[0] = 1;
            owner->transform[5] = 1;
            owner->transform[10] = 1;
            owner->transform[15] = 1;
        }
    }

    void EnableClipMask(bool enable) override
    {
        owner->clipEnabled = enable;
    }

    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                          Rml::Vector2f translation) override
    {
        const float offset[] = {translation.x, translation.y};
        u_mask(owner, (unsigned)operation, (uint64_t)geometry, offset);
    }

    Rml::CompiledShaderHandle CompileShader(const Rml::String& name,
                                            const Rml::Dictionary& parameters) override
    {
        if (name != "linear-gradient" && name != "radial-gradient")
        {
            u_resourceFailure(owner, BL_UNSUPPORTED,
                              "UI backend supports linear/radial gradient shaders");
            return 0;
        }
        const Rml::ColorStopList stops =
            Rml::Get(parameters, "color_stop_list", Rml::ColorStopList());
        if (stops.size() < 2 || stops.size() > 8)
        {
            u_pending(owner, BL_UNSUPPORTED, "UI gradients require 2..8 resolved color stops");
            return 0;
        }
        float data[18][4] = {};
        if (name == "linear-gradient")
        {
            Rml::Vector2f p0 = Rml::Get(parameters, "p0", Rml::Vector2f());
            Rml::Vector2f p1 = Rml::Get(parameters, "p1", Rml::Vector2f());
            data[0][0] = p0.x;
            data[0][1] = p0.y;
            data[0][2] = p1.x;
            data[0][3] = p1.y;
            if (p0 == p1)
            {
                u_pending(owner, BL_INVALID_ARGUMENT, "Degenerate UI linear gradient");
                return 0;
            }
        }
        else
        {
            Rml::Vector2f center = Rml::Get(parameters, "center", Rml::Vector2f());
            Rml::Vector2f radius = Rml::Get(parameters, "radius", Rml::Vector2f());
            if (radius.x <= 0 || radius.y <= 0)
            {
                u_pending(owner, BL_INVALID_ARGUMENT, "Degenerate UI radial gradient");
                return 0;
            }
            data[0][0] = center.x;
            data[0][1] = center.y;
            data[0][2] = radius.x;
            data[0][3] = radius.y;
            data[1][2] = 1;
        }
        data[1][0] = (float)stops.size();
        data[1][1] = Rml::Get(parameters, "repeating", false) ? 1.0f : 0.0f;
        for (size_t i = 0; i < stops.size(); ++i)
        {
            data[2 + i][0] = stops[i].position.number;
            data[10 + i][0] = stops[i].color.red / 255.0f;
            data[10 + i][1] = stops[i].color.green / 255.0f;
            data[10 + i][2] = stops[i].color.blue / 255.0f;
            data[10 + i][3] = stops[i].color.alpha / 255.0f;
        }
        return (Rml::CompiledShaderHandle)u_gradient(owner, data);
    }

    void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry,
                      Rml::Vector2f translation, Rml::TextureHandle texture) override
    {
        if (texture)
        {
            u_pending(owner, BL_UNSUPPORTED, "UI gradient shader cannot also sample an image");
            return;
        }
        const float offset[] = {translation.x, translation.y};
        u_draw(owner, (uint64_t)geometry, offset, 0, (uint64_t)shader);
    }

    void ReleaseShader(Rml::CompiledShaderHandle shader) override
    {
        u_release(owner, (uint64_t)shader, U_GRADIENT);
    }

    Rml::CompiledFilterHandle CompileFilter(const Rml::String& name,
                                            const Rml::Dictionary& parameters) override
    {
        if (name != "blur")
        {
            u_resourceFailure(owner, BL_UNSUPPORTED, "UI backend supports Gaussian blur filters");
            return 0;
        }
        return (Rml::CompiledFilterHandle)u_compileBlur(owner,
                                                        Rml::Get(parameters, "sigma", -1.0f));
    }

    void ReleaseFilter(Rml::CompiledFilterHandle filter) override
    {
        u_release(owner, (uint64_t)filter, U_FILTER);
    }

    Rml::LayerHandle PushLayer() override
    {
        return (Rml::LayerHandle)u_pushLayer(owner);
    }

    void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination,
                         Rml::BlendMode blend,
                         Rml::Span<const Rml::CompiledFilterHandle> filters) override
    {
        if (filters.size() > 1)
        {
            u_resourceFailure(owner, BL_UNSUPPORTED, "UI composite filter chain exceeds capacity");
            return;
        }
        uint64_t handles[1] = {};
        if (!filters.empty())
        {
            handles[0] = (uint64_t)filters[0];
        }
        u_composite(owner, (uint64_t)source, (uint64_t)destination,
                    blend == Rml::BlendMode::Replace, handles, filters.size());
    }

    void PopLayer() override
    {
        u_popLayer(owner);
    }

    Rml::TextureHandle SaveLayerAsTexture() override
    {
        return (Rml::TextureHandle)u_saveLayer(owner);
    }

    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override
    {
        u_resourceFailure(owner, BL_UNSUPPORTED, "UI layer mask images are unsupported");
        return 0;
    }
};

static U_Element* elementFromPointer(U_Context* c, Rml::Element* pointer)
{
    bl_Runtime* r = c->runtime;
    for (size_t i = 0; i < r->count; ++i)
    {
        L_Record* p = r->records[i];
        if (p && p->kind == L_UI_ELEMENT && !p->disposed)
        {
            U_Element* element = (U_Element*)p;
            if (element->owner == c && element->element == pointer)
            {
                return element;
            }
        }
    }
    return NULL;
}

class U_EventListener final : public Rml::EventListener
{
public:
    U_Listener* owner;

    void OnDetach(Rml::Element*) override
    {
        owner->attachment = NULL;
    }

    void ProcessEvent(Rml::Event& event) override
    {
        U_Listener* listener = owner;
        U_Context* c = listener->context;
        bl_UiEvent output = {};
        output.kind = listener->kind;
        output.currentTarget = {c->runtime, listener->elementId};
        U_Element* target = elementFromPointer(c, event.GetTargetElement());
        if (target)
        {
            output.target = {c->runtime, target->record.id};
        }
        output.position = {event.GetParameter<float>("mouse_x", 0),
                           event.GetParameter<float>("mouse_y", 0)};
        output.button = (uint32_t)event.GetParameter<int>("button", 0);
        int key = event.GetParameter<int>("key_identifier", 0);
        for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        {
            if ((int)keys[i] == key)
            {
                output.key = (bl_UiKey)i;
                break;
            }
        }
        if (event.GetParameter<bool>("shift_key", false))
        {
            output.modifiers |= BL_UI_MOD_SHIFT;
        }
        if (event.GetParameter<bool>("ctrl_key", false))
        {
            output.modifiers |= BL_UI_MOD_CONTROL;
        }
        if (event.GetParameter<bool>("alt_key", false))
        {
            output.modifiers |= BL_UI_MOD_ALT;
        }
        if (event.GetParameter<bool>("meta_key", false))
        {
            output.modifiers |= BL_UI_MOD_META;
        }
        Rml::String value = event.GetParameter<Rml::String>("value", "");
        output.value = {value.data(), value.size()};
        l_enterDispatch(c->runtime);
        listener->callback(listener->user, &output);
        l_leaveDispatch(c->runtime);
    }
};

static void listenerRemove(U_Context* c, U_Listener* listener)
{
    if (listener->attachment)
    {
        ((Rml::Element*)listener->attachment)
            ->RemoveEventListener(eventNames[listener->kind], (U_EventListener*)listener->adapter);
    }
    U_EventListener* adapter = (U_EventListener*)listener->adapter;
    adapter->~U_EventListener();
    l_free(c->runtime, adapter);
    l_free(c->runtime, listener);
}

static void retireElement(U_Context* c, U_Element* element)
{
    U_Listener** link = &c->listeners;
    while (*link)
    {
        U_Listener* listener = *link;
        if (listener->elementId == element->record.id)
        {
            *link = listener->next;
            listenerRemove(c, listener);
        }
        else
        {
            link = &listener->next;
        }
    }
    element->record.disposed = true;
    element->element = NULL;
    l_free(c->runtime, (void*)element->content.data);
    element->content = {};
    --c->stats.liveElementCount;
    l_unpin(&c->record);
}

static bool descendantIdentity(U_Context* c, U_Element* element, uint64_t ancestor)
{
    while (element)
    {
        if (element->record.id == ancestor)
        {
            return true;
        }
        L_Record* parent = l_peek(c->runtime, element->parentId);
        element = parent && parent->kind == L_UI_ELEMENT ? (U_Element*)parent : NULL;
    }
    return false;
}

static void failedOwnership(U_Context* c, uint64_t subtree)
{
    for (size_t i = 0; i < c->runtime->count; ++i)
    {
        L_Record* record = c->runtime->records[i];
        if (record && record->kind == L_UI_ELEMENT && !record->disposed)
        {
            U_Element* element = (U_Element*)record;
            if (element->owner == c && descendantIdentity(c, element, subtree))
            {
                retireElement(c, element);
            }
        }
    }
    c->renderFailure = BL_OUT_OF_MEMORY;
    snprintf(c->renderMessage, sizeof(c->renderMessage),
             "RmlUI ownership transfer exhausted external memory; recreate context");
    u_pending(c, BL_OUT_OF_MEMORY, c->renderMessage);
}

static bool descendant(Rml::Element* candidate, Rml::Element* parent)
{
    for (Rml::Element* e = candidate; e; e = e->GetParentNode())
    {
        if (e == parent)
        {
            return true;
        }
    }
    return false;
}

static void invalidate(U_Context* c, Rml::Element* subtree, bool inclusive)
{
    bl_Runtime* r = c->runtime;
    for (size_t i = 0; i < r->count; ++i)
    {
        L_Record* p = r->records[i];
        if (!p || p->kind != L_UI_ELEMENT || p->disposed)
        {
            continue;
        }
        U_Element* element = (U_Element*)p;
        if (element->owner != c || !element->element)
        {
            continue;
        }
        Rml::Element* pointer = (Rml::Element*)element->element;
        if ((subtree && !descendant(pointer, subtree)) || (!inclusive && pointer == subtree))
        {
            continue;
        }
        retireElement(c, element);
    }
}

static void elementCleanup(bl_Runtime*, L_Record* record)
{
    U_Element* element = (U_Element*)record;
    if (!element->element || element->root)
    {
        return;
    }
    U_Context* c = element->owner;
    Rml::Element* pointer = (Rml::Element*)element->element;
    Rml::Element* parent = pointer->GetParentNode();
    try
    {
        if (parent)
        {
            parent->RemoveChild(pointer).release();
        }
        invalidate(c, pointer, true);
        Rml::ElementPtr owned(pointer);
        owned.reset();
    }
    catch (const std::bad_alloc&)
    {
        failedOwnership(c, record->id);
    }
}

static void globalShutdown()
{
    if (rmlInitialized)
    {
        Rml::Shutdown();
        rmlInitialized = false;
    }
    Rml::SetSystemInterface(NULL);
    Rml::SetFileInterface(NULL);
    while (fonts)
    {
        U_Font* font = fonts;
        fonts = font->next;
        free(font->data);
        free(font->family);
        free(font);
    }
    delete fileInterface;
    delete systemInterface;
    fileInterface = NULL;
    systemInterface = NULL;
    globalTime = 0;
    bx::atomicCompareAndSwap(&uiThread, bx::getTid(), UINT32_C(0));
}

static void contextCleanup(bl_Runtime* r, L_Record* record)
{
    U_Context* c = (U_Context*)record;
    record->cleanupStatus = c->submitted ? l_retire(c->engine) : BL_OK;
    if (record->cleanupStatus != BL_OK)
    {
        return;
    }
    activeContext = c;
    c->closing = true;
    for (size_t i = 0; i < r->count; ++i)
    {
        L_Record* p = r->records[i];
        if (p && p->kind == L_UI_ELEMENT && !p->disposed)
        {
            U_Element* element = (U_Element*)p;
            if (element->owner == c && element->detached && element->element)
            {
                elementCleanup(r, p);
            }
        }
    }
    invalidate(c, NULL, true);
    if (c->context)
    {
        Rml::Context* context = (Rml::Context*)c->context;
        Rml::RemoveContext(context->GetName());
        c->context = NULL;
    }
    try
    {
        if (rmlInitialized)
        {
            Rml::ReleaseRenderManagers();
        }
    }
    catch (const std::bad_alloc&)
    {
        record->cleanupStatus = BL_OUT_OF_MEMORY;
        activeContext = NULL;
        return;
    }
    u_gpuDestroy(c);
    U_RenderInterface* renderer = (U_RenderInterface*)c->renderer;
    if (renderer)
    {
        renderer->~U_RenderInterface();
        l_free(r, renderer);
        c->renderer = NULL;
    }
    while (c->images)
    {
        U_Image* image = c->images;
        c->images = image->next;
        l_free(r, (void*)image->source.data);
        l_free(r, image->pixels);
        l_free(r, image);
    }
    U_Context** link = &contexts;
    while (*link && *link != c)
    {
        link = &(*link)->globalNext;
    }
    if (*link)
    {
        *link = c->globalNext;
    }
    l_releaseViews(c->engine, &c->views);
    l_unpin(&c->engine->record);
    if (!contexts && systemInterface)
    {
        globalShutdown();
    }
    activeContext = NULL;
}

static bl_Status contextPreDispose(bl_Runtime*, L_Record* record)
{
    U_Context* c = (U_Context*)record;
    return c->busy ? BL_BUSY : c->submitted ? l_retire(c->engine) : BL_OK;
}

static bl_Status createIdentity(U_Context* c, Rml::Element* pointer, bool root, bool detached,
                                bl_UiElement* out)
{
    L_NEW(c->runtime, L_UI_ELEMENT, 10, elementCleanup, U_Element, element);
    element->owner = c;
    element->element = pointer;
    element->root = root;
    element->detached = detached;
    l_pin(&c->record);
    ++c->stats.liveElementCount;
    *out = {c->runtime, element->record.id};
    return BL_OK;
}

bl_Status bl_createUiContext(bl_EngineContext h, const bl_UiContextOptions* options,
                             bl_UiContext* out)
{
    L_GET(h, L_ENGINE, L_Engine, engine);
    bl_Runtime* r = h._runtime;
    if (!options || !out || !isfinite(options->densityRatio) || options->densityRatio <= 0 ||
        options->densityRatio > 16 || !options->target.width || !options->target.height ||
        options->target.width > UINT16_MAX || options->target.height > UINT16_MAX ||
        options->target.kind != engine->native.target.kind ||
        options->target.framebufferIndex != engine->native.target.framebufferIndex ||
        options->target.colorFormat != engine->native.target.colorFormat ||
        options->target.depthFormat != engine->native.target.depthFormat ||
        options->target.sampleCount != engine->native.target.sampleCount ||
        options->target.firstViewId <
            (uint32_t)engine->native.target.firstViewId + engine->native.target.viewCount)
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Invalid UI target, DPI or composition view range");
    }
    if (engine->native.ownership != BL_BGFX_BORROWED)
    {
        return L_FAIL(r, BL_UNSUPPORTED, "Initial UI contexts require a BORROWED engine");
    }
    if (!r->hasCompiler)
    {
        return L_FAIL(r, BL_SHADER_ERROR, "UI rendering requires the runtime shader compiler");
    }
    if (l_inFrame() || l_inDispatch())
    {
        return BL_BUSY;
    }
    uint32_t previous = bx::atomicCompareAndSwap(&uiThread, UINT32_C(0), r->thread);
    if (previous && previous != r->thread)
    {
        return BL_WRONG_THREAD;
    }
    if (activeContext)
    {
        return BL_BUSY;
    }
    if (rmlPoisoned)
    {
        return L_FAIL(r, BL_NOT_READY, "RmlUI initialization previously exhausted external memory");
    }
    if (!systemInterface && (Rml::GetSystemInterface() || Rml::GetFileInterface() ||
                             Rml::GetFontEngineInterface() || Rml::GetRenderInterface()))
    {
        bx::atomicCompareAndSwap(&uiThread, r->thread, UINT32_C(0));
        return L_FAIL(r, BL_BUSY, "Lite UI cannot coexist with external RmlUI interfaces");
    }
    L_Record* base = NULL;
    bl_Status status = l_record(r, sizeof(U_Context), L_UI_CONTEXT, 200, contextCleanup, &base);
    if (status != BL_OK)
    {
        if (!contexts)
        {
            bx::atomicCompareAndSwap(&uiThread, r->thread, UINT32_C(0));
        }
        return status;
    }
    U_Context* c = (U_Context*)base;
    c->runtime = r;
    c->engine = engine;
    u_layersInit(c);
    for (size_t i = 0; i < U_PROGRAM_COUNT; ++i)
    {
        c->programs[i].handle = BGFX_INVALID_HANDLE;
        c->programs[i].sampler = BGFX_INVALID_HANDLE;
    }
    c->options = *options;
    c->views.first = options->target.firstViewId;
    c->views.count = options->target.viewCount;
    status = l_reserveViews(engine, &c->views);
    if (status != BL_OK)
    {
        c->record.disposed = true;
        if (!contexts)
        {
            bx::atomicCompareAndSwap(&uiThread, r->thread, UINT32_C(0));
        }
        return status;
    }
    l_pin(&engine->record);
    c->record.preDispose = contextPreDispose;
    activeContext = c;
    try
    {
        if (!systemInterface)
        {
            systemInterface = new U_SystemInterface;
            fileInterface = new U_FileInterface;
            Rml::SetSystemInterface(systemInterface);
            Rml::SetFileInterface(fileInterface);
            rmlPoisoned = true;
            if (!Rml::Initialise())
            {
                status = BL_HOST_ERROR;
            }
            else
            {
                rmlInitialized = true;
                rmlPoisoned = false;
            }
        }
        c->globalNext = contexts;
        contexts = c;
        if (status == BL_OK)
        {
            void* memory = l_alloc(r, sizeof(U_RenderInterface));
            if (!memory)
            {
                status = BL_OUT_OF_MEMORY;
            }
            else
            {
                U_RenderInterface* renderer = new (memory) U_RenderInterface;
                renderer->owner = c;
                c->renderer = renderer;
                status = u_gpuCreate(c);
            }
        }
        if (status == BL_OK)
        {
            char name[64];
            snprintf(name, sizeof(name), "bl_ui_%llu", (unsigned long long)c->record.serial);
            Rml::Context* context =
                Rml::CreateContext(name, {(int)options->target.width, (int)options->target.height},
                                   (U_RenderInterface*)c->renderer);
            c->context = context;
            if (!context)
            {
                status = BL_HOST_ERROR;
            }
            else
            {
                context->SetDensityIndependentPixelRatio((float)options->densityRatio);
                Rml::ElementDocument* document = context->LoadDocumentFromMemory(
                    "<rml><head><style>body { width: 100%; height: 100%; "
                    "margin: 0; padding: 0; }</style></head><body></body></rml>");
                c->document = document;
                if (!document)
                {
                    status = BL_HOST_ERROR;
                }
                else
                {
                    document->SetProperty("width", "100%");
                    document->SetProperty("height", "100%");
                    document->Show();
                    bl_UiElement root = {};
                    status = createIdentity(c, document, true, false, &root);
                    c->rootId = root._id;
                }
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        status = BL_OUT_OF_MEMORY;
        u_pending(c, status, "RmlUI external allocation failed");
    }
    activeContext = NULL;
    if (status == BL_OK && c->pending != BL_OK)
    {
        status = c->pending;
    }
    if (status != BL_OK)
    {
        char message[512];
        snprintf(message, sizeof(message), "%s",
                 c->pendingMessage[0] ? c->pendingMessage : "UI context initialization failed");
        contextCleanup(r, &c->record);
        c->record.disposed = true;
        return l_error(r, status, __func__, message);
    }
    *out = {r, c->record.id};
    return BL_OK;
}

bl_Status bl_disposeUiContext(bl_UiContext h)
{
    L_RETIRED(h, L_UI_CONTEXT, U_Context, c);
    if (!c || c->record.disposed)
    {
        return BL_OK;
    }
    if (c->busy || l_inFrame() || l_inDispatch() || activeContext)
    {
        return BL_BUSY;
    }
    contextCleanup(h._runtime, &c->record);
    if (c->record.cleanupStatus != BL_OK)
    {
        return c->record.cleanupStatus;
    }
    c->record.disposed = true;
    return BL_OK;
}

bl_Status bl_setUiViewport(bl_UiContext h, uint32_t width, uint32_t height, double ratio)
{
    L_GET(h, L_UI_CONTEXT, U_Context, c);
    if (!width || !height || width > UINT16_MAX || height > UINT16_MAX || !isfinite(ratio) ||
        ratio <= 0 || ratio > 16)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (c->busy || c->closing || l_inDispatch())
    {
        return BL_BUSY;
    }
    if (c->clipQuad)
    {
        u_release(c, c->clipQuad, U_GEOMETRY);
        c->clipQuad = 0;
    }
    u_layersResize(c);
    c->options.target.width = width;
    c->options.target.height = height;
    c->options.densityRatio = ratio;
    Rml::Context* context = (Rml::Context*)c->context;
    try
    {
        context->SetDimensions({(int)width, (int)height});
        context->SetDensityIndependentPixelRatio((float)ratio);
    }
    catch (const std::bad_alloc&)
    {
        return BL_OUT_OF_MEMORY;
    }
    return BL_OK;
}

bl_Status bl_getUiRoot(bl_UiContext h, bl_UiElement* out)
{
    L_GET(h, L_UI_CONTEXT, U_Context, c);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (c->closing)
    {
        return BL_BUSY;
    }
    *out = {h._runtime, c->rootId};
    return BL_OK;
}

bl_Status bl_createUiElement(bl_UiContext h, bl_String tag, bl_UiElement* out)
{
    L_GET(h, L_UI_CONTEXT, U_Context, c);
    if (!out || !l_identifier(tag))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (l_inDispatch() || c->busy || c->closing)
    {
        return BL_BUSY;
    }
    bl_Status status = BL_OK;
    activeContext = c;
    try
    {
        Rml::ElementPtr element =
            ((Rml::ElementDocument*)c->document)->CreateElement(rmlString(tag));
        if (!element)
        {
            status = BL_UNSUPPORTED;
        }
        else
        {
            status = createIdentity(c, element.get(), false, true, out);
            if (status == BL_OK)
            {
                element.release();
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        status = BL_OUT_OF_MEMORY;
    }
    activeContext = NULL;
    if (status != BL_OK)
    {
        c->pending = BL_OK;
        return status;
    }
    return u_finish(c, __func__);
}

bl_Status bl_appendUiChild(bl_UiElement p, bl_UiElement ch)
{
    L_GET(p, L_UI_ELEMENT, U_Element, parent);
    if (ch._runtime != p._runtime)
    {
        return BL_WRONG_RUNTIME;
    }
    L_GET(ch, L_UI_ELEMENT, U_Element, child);
    if (parent->owner != child->owner)
    {
        return BL_WRONG_ENGINE;
    }
    if (l_inDispatch() || parent->owner->busy)
    {
        return BL_BUSY;
    }
    if (child->root || !child->detached || parent == child ||
        descendant((Rml::Element*)parent->element, (Rml::Element*)child->element))
    {
        return BL_INVALID_ARGUMENT;
    }
    try
    {
        ((Rml::Element*)parent->element)
            ->AppendChild(Rml::ElementPtr((Rml::Element*)child->element));
    }
    catch (const std::bad_alloc&)
    {
        failedOwnership(parent->owner, child->record.id);
        return u_finish(parent->owner, __func__);
    }
    child->detached = false;
    child->parentId = parent->record.id;
    parent->hasContent = false;
    return BL_OK;
}

bl_Status bl_removeUiChild(bl_UiElement p, bl_UiElement ch)
{
    L_GET(p, L_UI_ELEMENT, U_Element, parent);
    if (ch._runtime != p._runtime)
    {
        return BL_WRONG_RUNTIME;
    }
    L_GET(ch, L_UI_ELEMENT, U_Element, child);
    if (parent->owner != child->owner)
    {
        return BL_WRONG_ENGINE;
    }
    if (l_inDispatch() || parent->owner->busy)
    {
        return BL_BUSY;
    }
    Rml::Element* pointer = (Rml::Element*)child->element;
    if (pointer->GetParentNode() != parent->element)
    {
        return BL_INVALID_ARGUMENT;
    }
    try
    {
        ((Rml::Element*)parent->element)->RemoveChild(pointer).release();
    }
    catch (const std::bad_alloc&)
    {
        failedOwnership(parent->owner, child->record.id);
        return u_finish(parent->owner, __func__);
    }
    child->detached = true;
    child->parentId = 0;
    parent->hasContent = false;
    return BL_OK;
}

bl_Status bl_disposeUiElement(bl_UiElement h)
{
    L_RETIRED(h, L_UI_ELEMENT, U_Element, element);
    if (!element || element->record.disposed)
    {
        return BL_OK;
    }
    if (element->root || element->owner->busy || l_inDispatch())
    {
        return BL_BUSY;
    }
    activeContext = element->owner;
    elementCleanup(h._runtime, &element->record);
    activeContext = NULL;
    return u_finish(element->owner, __func__);
}

bl_Status bl_setUiProperty(bl_UiElement h, bl_String name, bl_String value)
{
    L_GET(h, L_UI_ELEMENT, U_Element, element);
    if (!l_string(name) || !name.length || !l_string(value))
    {
        return BL_INVALID_ARGUMENT;
    }
    U_Context* c = element->owner;
    activeContext = c;
    bl_Status status = BL_OK;
    try
    {
        if (!((Rml::Element*)element->element)->SetProperty(rmlString(name), rmlString(value)))
        {
            status = BL_UNSUPPORTED;
        }
    }
    catch (const std::bad_alloc&)
    {
        status = BL_OUT_OF_MEMORY;
    }
    activeContext = c->busy ? c : NULL;
    if (status != BL_OK)
    {
        c->pending = BL_OK;
        return L_FAIL(h._runtime, status, "UI property is unknown, invalid or unsupported");
    }
    return u_finish(c, __func__);
}

bl_Status bl_removeUiProperty(bl_UiElement h, bl_String name)
{
    L_GET(h, L_UI_ELEMENT, U_Element, element);
    if (!l_string(name) || !name.length)
    {
        return BL_INVALID_ARGUMENT;
    }
    try
    {
        if (!Rml::StyleSheetSpecification::GetProperty(rmlString(name)) &&
            !Rml::StyleSheetSpecification::GetShorthand(rmlString(name)))
        {
            return BL_UNSUPPORTED;
        }
        ((Rml::Element*)element->element)->RemoveProperty(rmlString(name));
    }
    catch (const std::bad_alloc&)
    {
        return BL_OUT_OF_MEMORY;
    }
    return BL_OK;
}

static bl_Status setContent(U_Element* element, bl_String content, bool markup)
{
    U_Context* c = element->owner;
    if (!l_string(content))
    {
        return BL_INVALID_ARGUMENT;
    }
    if ((l_inDispatch() || c->busy) && (markup || !l_inDispatch()))
    {
        return BL_BUSY;
    }
    if (element->hasContent && element->contentMarkup == markup &&
        l_equal(element->content, content))
    {
        return BL_OK;
    }
    bl_String copied = l_copyString(c->runtime, content);
    if (!copied.data)
    {
        return BL_OUT_OF_MEMORY;
    }
    activeContext = c;
    bl_Status status = BL_OK;
    try
    {
        Rml::String text = rmlString(content);
        if (!markup)
        {
            text = Rml::StringUtilities::EncodeRml(text);
        }
        invalidate(c, (Rml::Element*)element->element, false);
        ((Rml::Element*)element->element)->SetInnerRML(text);
    }
    catch (const std::bad_alloc&)
    {
        status = BL_OUT_OF_MEMORY;
    }
    activeContext = c->busy ? c : NULL;
    if (status == BL_OK && c->pending == BL_OK)
    {
        l_free(c->runtime, (void*)element->content.data);
        element->content = copied;
        element->contentMarkup = markup;
        element->hasContent = true;
    }
    else
    {
        l_free(c->runtime, (void*)copied.data);
    }
    return status == BL_OK ? u_finish(c, markup ? "bl_setUiMarkup" : "bl_setUiText") : status;
}

bl_Status bl_setUiText(bl_UiElement h, bl_String text)
{
    L_GET(h, L_UI_ELEMENT, U_Element, element);
    return setContent(element, text, false);
}

bl_Status bl_setUiMarkup(bl_UiElement h, bl_String markup)
{
    L_GET(h, L_UI_ELEMENT, U_Element, element);
    return setContent(element, markup, true);
}

bl_Status bl_setUiAttribute(bl_UiElement h, bl_String name, bl_String value)
{
    L_GET(h, L_UI_ELEMENT, U_Element, element);
    if (!l_string(name) || !name.length || !l_string(value))
    {
        return BL_INVALID_ARGUMENT;
    }
    U_Context* c = element->owner;
    activeContext = c;
    try
    {
        ((Rml::Element*)element->element)->SetAttribute(rmlString(name), rmlString(value));
    }
    catch (const std::bad_alloc&)
    {
        u_pending(c, BL_OUT_OF_MEMORY, "RmlUI attribute allocation failed");
    }
    activeContext = c->busy ? c : NULL;
    return u_finish(c, __func__);
}

bl_Status bl_loadUiFont(bl_UiContext h, bl_Bytes bytes, bl_String family, uint32_t weight,
                        bool italic, bool fallback)
{
    L_GET(h, L_UI_CONTEXT, U_Context, c);
    if (!bytes.data || !bytes.count || bytes.count > INT_MAX || !l_string(family) ||
        !family.length || family.length == SIZE_MAX || !weight || weight > 1000)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (c->busy || c->closing || l_inDispatch())
    {
        return BL_BUSY;
    }
    for (U_Font* font = fonts; font; font = font->next)
    {
        bl_String existing = {font->family, strlen(font->family)};
        if (l_equal(existing, family) && font->weight == weight && font->italic == italic)
        {
            return font->bytes == bytes.count && font->fallback == fallback &&
                           !memcmp(font->data, bytes.data, bytes.count)
                       ? BL_OK
                       : BL_BUSY;
        }
    }
    U_Font* font = (U_Font*)calloc(1, sizeof(U_Font));
    if (!font)
    {
        return BL_OUT_OF_MEMORY;
    }
    font->data = (uint8_t*)malloc(bytes.count);
    font->family = (char*)malloc(family.length + 1);
    if (!font->data || !font->family)
    {
        free(font->data);
        free(font->family);
        free(font);
        return BL_OUT_OF_MEMORY;
    }
    memcpy(font->data, bytes.data, bytes.count);
    memcpy(font->family, family.data, family.length);
    font->family[family.length] = 0;
    font->bytes = bytes.count;
    font->weight = weight;
    font->italic = italic;
    font->fallback = fallback;
    activeContext = c;
    bool loaded = false;
    try
    {
        loaded = Rml::LoadFontFace({font->data, font->bytes}, rmlString(family),
                                   italic ? Rml::Style::FontStyle::Italic
                                          : Rml::Style::FontStyle::Normal,
                                   (Rml::Style::FontWeight)weight, fallback);
    }
    catch (const std::bad_alloc&)
    {
        u_pending(c, BL_OUT_OF_MEMORY, "RmlUI font allocation failed");
    }
    activeContext = NULL;
    if (!loaded)
    {
        free(font->data);
        free(font->family);
        free(font);
        if (c->pending == BL_OK)
        {
            u_pending(c, BL_HOST_ERROR, "RmlUI rejected the supplied font face");
        }
        return u_finish(c, __func__);
    }
    font->next = fonts;
    fonts = font;
    return u_finish(c, __func__);
}

bl_Status bl_registerUiImage(bl_UiContext h, bl_String source, bl_Bytes bytes, uint32_t width,
                             uint32_t height)
{
    L_GET(h, L_UI_CONTEXT, U_Context, c);
    size_t count;
    if (!l_string(source) || !source.length || !width || !height ||
        !l_size((size_t)width * height, 4, &count) || bytes.count != count || !bytes.data ||
        width > bgfx::getCaps()->limits.maxTextureSize ||
        height > bgfx::getCaps()->limits.maxTextureSize)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (c->busy || c->closing || l_inDispatch() || imageFind(c, source))
    {
        return BL_BUSY;
    }
    U_Image* image = (U_Image*)l_alloc(h._runtime, sizeof(U_Image));
    if (!image)
    {
        return BL_OUT_OF_MEMORY;
    }
    image->source = l_copyString(h._runtime, source);
    image->pixels = (uint8_t*)l_alloc(h._runtime, count);
    if (!image->source.data || !image->pixels)
    {
        l_free(h._runtime, (void*)image->source.data);
        l_free(h._runtime, image->pixels);
        l_free(h._runtime, image);
        return BL_OUT_OF_MEMORY;
    }
    memcpy(image->pixels, bytes.data, count);
    image->width = width;
    image->height = height;
    image->next = c->images;
    c->images = image;
    Rml::ReleaseTexture(rmlString(source), (U_RenderInterface*)c->renderer);
    return BL_OK;
}

bl_Status bl_setUiImageSampling(bl_UiContext h, bl_String source, bool pixelated)
{
    L_GET(h, L_UI_CONTEXT, U_Context, c);
    if (!l_string(source) || !source.length)
    {
        return L_FAIL(h._runtime, BL_INVALID_ARGUMENT, "Invalid UI image source");
    }
    if (c->busy || c->closing || l_inDispatch())
    {
        return BL_BUSY;
    }
    U_Image* image = imageFind(c, source);
    if (!image)
    {
        return L_FAIL(h._runtime, BL_INVALID_ARGUMENT, "UI image source is unregistered");
    }
    if (image->references)
    {
        return BL_BUSY;
    }
    image->pixelated = pixelated;
    return BL_OK;
}

bl_Status bl_unregisterUiImage(bl_UiContext h, bl_String source)
{
    L_GET(h, L_UI_CONTEXT, U_Context, c);
    if (!l_string(source))
    {
        return BL_INVALID_ARGUMENT;
    }
    U_Image** link = &c->images;
    while (*link && !l_equal((*link)->source, source))
    {
        link = &(*link)->next;
    }
    if (!*link)
    {
        return BL_INVALID_ARGUMENT;
    }
    U_Image* image = *link;
    if (image->references || c->busy || c->closing || l_inDispatch())
    {
        return BL_BUSY;
    }
    *link = image->next;
    l_free(h._runtime, (void*)image->source.data);
    l_free(h._runtime, image->pixels);
    l_free(h._runtime, image);
    return BL_OK;
}

bl_Status bl_processUiInput(bl_UiContext h, const bl_UiInput* input, bool* out)
{
    L_GET(h, L_UI_CONTEXT, U_Context, c);
    if (!input || !out || (unsigned)input->kind > BL_UI_TEXT || !isfinite(input->x) ||
        !isfinite(input->y) || input->x < INT_MIN || input->x > INT_MAX || input->y < INT_MIN ||
        input->y > INT_MAX || input->button > 2 ||
        (unsigned)input->key >= sizeof(keys) / sizeof(keys[0]) ||
        (input->modifiers & ~UINT32_C(15)) || !l_string(input->text))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (c->busy || c->closing || activeContext || l_inDispatch() || l_inFrame())
    {
        return BL_BUSY;
    }
    if (c->renderFailure != BL_OK)
    {
        return l_error(h._runtime, c->renderFailure, __func__, c->renderMessage);
    }
    Rml::Context* context = (Rml::Context*)c->context;
    activeContext = c;
    c->busy = true;
    bool propagates = true;
    int modifiers = inputModifiers(input->modifiers);
    try
    {
        switch (input->kind)
        {
            case BL_UI_POINTER_MOVE:
                propagates = context->ProcessMouseMove((int)input->x, (int)input->y, modifiers);
                break;
            case BL_UI_POINTER_DOWN:
                propagates = context->ProcessMouseButtonDown((int)input->button, modifiers);
                break;
            case BL_UI_POINTER_UP:
                propagates = context->ProcessMouseButtonUp((int)input->button, modifiers);
                break;
            case BL_UI_POINTER_LEAVE:
                propagates = context->ProcessMouseLeave();
                break;
            case BL_UI_WHEEL:
                propagates =
                    context->ProcessMouseWheel({(float)input->x, (float)input->y}, modifiers);
                break;
            case BL_UI_KEY_DOWN:
                propagates = context->ProcessKeyDown(keys[input->key], modifiers);
                break;
            case BL_UI_KEY_UP:
                propagates = context->ProcessKeyUp(keys[input->key], modifiers);
                break;
            case BL_UI_TEXT:
                propagates = context->ProcessTextInput(rmlString(input->text));
                break;
        }
    }
    catch (const std::bad_alloc&)
    {
        u_pending(c, BL_OUT_OF_MEMORY, "RmlUI input allocation failed");
    }
    c->busy = false;
    activeContext = NULL;
    L_TRY(u_finish(c, __func__));
    *out = !propagates;
    return BL_OK;
}

bl_Status bl_updateUi(bl_UiContext h, double time)
{
    L_GET(h, L_UI_CONTEXT, U_Context, c);
    if (!isfinite(time) || time < globalTime || (c->hasTime && time < c->time))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (c->busy || c->closing || activeContext || l_inDispatch() || l_inFrame())
    {
        return BL_BUSY;
    }
    if (c->renderFailure != BL_OK)
    {
        return l_error(h._runtime, c->renderFailure, __func__, c->renderMessage);
    }
    // bgfx owns copied upload data and orders destruction after queued draws.
    // A real host synchronization boundary is required for context teardown,
    // not for every invalidated text/geometry resource.
    u_collect(c);
    c->time = time;
    globalTime = time;
    c->hasTime = true;
    activeContext = c;
    c->busy = true;
    c->recording = true;
    try
    {
        ((Rml::Context*)c->context)->Update();
    }
    catch (const std::bad_alloc&)
    {
        u_pending(c, BL_OUT_OF_MEMORY, "RmlUI layout allocation failed");
    }
    c->busy = false;
    c->recording = false;
    activeContext = NULL;
    return u_finish(c, __func__);
}

bl_Status bl_setUiWhiteDifference(bl_UiContext h, bool enabled)
{
    L_GET(h, L_UI_CONTEXT, U_Context, c);
    if (c->busy || c->closing || l_inDispatch())
    {
        return BL_BUSY;
    }
    c->whiteDifference = enabled;
    return BL_OK;
}

bl_Status bl_renderUi(bl_UiContext h)
{
    L_GET(h, L_UI_CONTEXT, U_Context, c);
    if (c->busy || c->closing || activeContext || l_inDispatch() || l_inFrame())
    {
        return BL_BUSY;
    }
    if (!c->hasTime)
    {
        return L_FAIL(h._runtime, BL_NOT_READY, "Update UI before rendering");
    }
    if (c->renderFailure != BL_OK)
    {
        return l_error(h._runtime, c->renderFailure, __func__, c->renderMessage);
    }
    activeContext = c;
    c->busy = true;
    c->recording = true;
    u_begin(c);
    try
    {
        ((Rml::Context*)c->context)->Render();
    }
    catch (const std::bad_alloc&)
    {
        u_pending(c, BL_OUT_OF_MEMORY, "RmlUI render allocation failed");
    }
    c->busy = false;
    c->recording = false;
    activeContext = NULL;
    return u_finish(c, __func__);
}

bl_Status bl_getUiStats(bl_UiContext h, bl_UiStats* out)
{
    L_GET(h, L_UI_CONTEXT, U_Context, c);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = c->stats;
    return BL_OK;
}

bl_Status bl_addUiEventListener(bl_UiElement h, bl_UiEventKind kind, bl_UiEventCallback callback,
                                void* user, bl_UiListenerToken* out)
{
    L_GET(h, L_UI_ELEMENT, U_Element, element);
    U_Context* c = element->owner;
    if (!callback || !out || (unsigned)kind > BL_UI_EVENT_KEY_UP)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (c->busy || l_inDispatch())
    {
        return BL_BUSY;
    }
    if (h._runtime->callbackId == UINT64_MAX)
    {
        return BL_OUT_OF_MEMORY;
    }
    U_Listener* listener = (U_Listener*)l_alloc(h._runtime, sizeof(U_Listener));
    void* memory = l_alloc(h._runtime, sizeof(U_EventListener));
    if (!listener || !memory)
    {
        l_free(h._runtime, listener);
        l_free(h._runtime, memory);
        return BL_OUT_OF_MEMORY;
    }
    U_EventListener* adapter = new (memory) U_EventListener;
    listener->adapter = adapter;
    listener->attachment = element->element;
    listener->context = c;
    listener->elementId = h._id;
    listener->token = ++h._runtime->callbackId;
    listener->kind = kind;
    listener->callback = callback;
    listener->user = user;
    adapter->owner = listener;
    try
    {
        ((Rml::Element*)element->element)->AddEventListener(eventNames[kind], adapter);
    }
    catch (const std::bad_alloc&)
    {
        adapter->~U_EventListener();
        l_free(h._runtime, adapter);
        l_free(h._runtime, listener);
        return BL_OUT_OF_MEMORY;
    }
    listener->next = c->listeners;
    c->listeners = listener;
    *out = {listener->token};
    return BL_OK;
}

bl_Status bl_removeUiEventListener(bl_UiElement h, bl_UiListenerToken token)
{
    L_GET(h, L_UI_ELEMENT, U_Element, element);
    U_Context* c = element->owner;
    if (c->busy || l_inDispatch())
    {
        return BL_BUSY;
    }
    U_Listener** link = &c->listeners;
    while (*link && ((*link)->token != token.value || (*link)->elementId != h._id))
    {
        link = &(*link)->next;
    }
    if (!*link)
    {
        return BL_INVALID_ARGUMENT;
    }
    U_Listener* listener = *link;
    *link = listener->next;
    listenerRemove(c, listener);
    return BL_OK;
}
