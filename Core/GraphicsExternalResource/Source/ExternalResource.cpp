#include <ExternalResource.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <cassert>

namespace Babylon
{
    struct ExternalTexture::Impl
    {
        Impl(Graphics&)
            : m_textureHandle{bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT)}
        {
        }

        ~Impl()
        {
            bgfx::destroy(m_textureHandle);
        }

        void OverrideInternalTexture(uintptr_t texturePtr)
        {
            if (bgfx::overrideInternal(m_textureHandle, texturePtr) == 0)
            {
                assert(false);
            }
        }
    
    private:
        bgfx::TextureHandle m_textureHandle{};
    };

    ExternalTexture::ExternalTexture(std::unique_ptr<Impl> impl)
        : m_impl{std::move(impl)}
    {
    }

    ExternalTexture::ExternalTexture(ExternalTexture&& other)
        : m_impl{std::move(other.m_impl)}
    {
    }

    void ExternalTexture::OverrideInternalTexture(uintptr_t nativeTexturePtr)
    {
        m_impl->OverrideInternalTexture(nativeTexturePtr);
    }

    ExternalTexture::~ExternalTexture()
    {
    }

    ExternalTexture::ExternalTexture(Graphics& graphics, uintptr_t)
        : ExternalTexture{std::make_unique<ExternalTexture::Impl>(graphics)}
    {
    }

    Napi::Object ExternalTexture::ConvertToNapiObject(Napi::Env env, ExternalTexture)
    {
        return Napi::Object::New(env);
    }
}