#pragma once

#include <Babylon/Polyfills/Canvas.h>

#include <Babylon/Graphics/DeviceContext.h>
#include <Babylon/Graphics/FrameBuffer.h>
#include <Babylon/Graphics/Texture.h>

namespace Babylon::Polyfills
{
    class Canvas::Impl final : public std::enable_shared_from_this<Canvas::Impl>
    {
    public:
        explicit Impl(Napi::Env);

        void FlushGraphicResources();

        static Canvas::Impl& GetFromJavaScript(Napi::Env env);

        struct MonitoredResource
        {
            MonitoredResource(Canvas::Impl& impl)
                : m_impl(impl)
            {
                m_impl.AddMonitoredResource(this);
            }
            virtual ~MonitoredResource()
            {
                m_impl.RemoveMonitoredResource(this);
            }

            virtual void FlushGraphicResources() = 0;

        private:
            Canvas::Impl& m_impl;
        };

    private:
        Napi::Env m_env;

        void AddToJavaScript(Napi::Env env);

        std::vector<MonitoredResource*> m_monitoredResources{};

        void AddMonitoredResource(MonitoredResource* monitoredResource);
        void RemoveMonitoredResource(MonitoredResource* monitoredResource);

        friend struct MonitoredResource;
    };
} // namespace

namespace Babylon::Polyfills::Internal
{
    class NativeCanvas final : public Napi::ObjectWrap<NativeCanvas>, Polyfills::Canvas::Impl::MonitoredResource
    {
    public:
        static void Initialize(Napi::Env env);

        explicit NativeCanvas(const Napi::CallbackInfo& info);
        virtual ~NativeCanvas();

        uint32_t GetWidth() const { return m_width; }
        uint32_t GetHeight() const { return m_height; }

        static inline std::map<std::string, std::vector<uint8_t>> fontsInfos;

        void UpdateRenderTarget();
        Babylon::Graphics::FrameBuffer& GetFrameBuffer() { return *m_frameBuffer; }

        Graphics::DeviceContext& GetGraphicsContext()
        {
            return m_graphicsContext;
        }

    private:
        Napi::Value GetContext(const Napi::CallbackInfo&);
        Napi::Value GetWidth(const Napi::CallbackInfo&);
        void SetWidth(const Napi::CallbackInfo&, const Napi::Value& value);
        Napi::Value GetHeight(const Napi::CallbackInfo&);
        void SetHeight(const Napi::CallbackInfo&, const Napi::Value& value);
        Napi::Value GetCanvasTexture(const Napi::CallbackInfo& info);
        static Napi::Value LoadTTFAsync(const Napi::CallbackInfo& info);
        static Napi::Value ParseColor(const Napi::CallbackInfo& info);
        void Remove(const Napi::CallbackInfo& info);
        void Dispose(const Napi::CallbackInfo& info);
        void Dispose();

        uint16_t m_width{1};
        uint16_t m_height{1};

        Graphics::DeviceContext& m_graphicsContext;

        std::unique_ptr<Graphics::FrameBuffer> m_frameBuffer;
        std::unique_ptr<Graphics::Texture> m_texture{};
        bool m_dirty{};

        void FlushGraphicResources() override;
    };
}
