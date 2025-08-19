namespace IntegrationTestApp
{
    const char* GetLogLevelString(Babylon::Polyfills::Console::LogLevel logLevel)
    {
        switch (logLevel)
        {
            case Babylon::Polyfills::Console::LogLevel::Log:
                return "Log";
            case Babylon::Polyfills::Console::LogLevel::Warn:
                return "Warn";
            case Babylon::Polyfills::Console::LogLevel::Error:
                return "Error";
            default:
                return "";
        }
    }

    void BabylonRendererImpl::BeginFrame()
    {
        BABYLON_GRAPHICS_DEBUG_BEGIN_FRAME_CAPTURE(m_pGraphicsDevice->GetPlatformInfo().Device);
        m_pGraphicsDevice->StartRenderingCurrentFrame();
        m_pGraphicsDeviceUpdate->Start();
    }

    void BabylonRendererImpl::EndFrame()
    {
        m_pGraphicsDeviceUpdate->Finish();
        m_pGraphicsDevice->FinishRenderingCurrentFrame();
        BABYLON_GRAPHICS_DEBUG_END_FRAME_CAPTURE(m_pGraphicsDevice->GetPlatformInfo().Device);
    }

    void BabylonRendererImpl::RenderFrame()
    {
        EndFrame();
        BeginFrame();
    }

    void BabylonRendererImpl::RenderJS(Napi::Env env)
    {
        auto jsRender = m_pContext->Get("render").As<Napi::Function>();
        jsRender.Call(m_pContext->Value(), {m_pRenderTargetTexture->Value()});
    }

    void BabylonRendererImpl::ApplyRootNodeTransform(Napi::Env env, const Matrix4& transform)
    {
        auto arrayBuffer = Napi::ArrayBuffer::New(env, const_cast<Matrix4*>(&transform), sizeof(transform));
        auto typedArray = Napi::Float32Array::New(env, 16, arrayBuffer, 0);
        auto applyRootNodeTransform = m_pContext->Get("applyRootNodeTransform").As<Napi::Function>();
        applyRootNodeTransform.Call(m_pContext->Value(), {typedArray});
    }

    void BabylonRendererImpl::ApplyCameraTransform(Napi::Env env, const ICameraTransform& transform,
        float left, float top, float right, float bottom, bool fClipped)
    {
        float viewportScaleForMargin = 1.0f;
        if (!fClipped)
        {
            const float maxMarginX = std::max(left, static_cast<float>(m_textureWidth) - right) / (right - left);
            const float maxMarginY = std::max(top, static_cast<float>(m_textureHeight) - bottom) / (bottom - top);
            viewportScaleForMargin = 1.0f + std::max(maxMarginX, maxMarginY) * 2;
        }

        const float centerX = (left + right) / 2.0f;
        const float centerY = (top + bottom) / 2.0f;
        const float viewportWidth = static_cast<float>(right - left) * viewportScaleForMargin;
        const float viewportHeight = static_cast<float>(bottom - top) * viewportScaleForMargin;

        const float vpMinX = centerX - (viewportWidth / 2.0f);
        const float vpMinY = centerY - (viewportHeight / 2.0f);
        const float vpMaxX = centerX + (viewportWidth / 2.0f);
        const float vpMaxY = centerY + (viewportHeight / 2.0f);

        const bool fOrthographic = false;
        float cameraFovOrOrthographicSize{};

        constexpr float pi = 3.14f;
        const float verticalFieldOfViewDegrees = transform.GetFovInDegree();
        const float verticalFieldOfViewDegreesWithPadding = 2.0f * atanf(viewportScaleForMargin * tanf(verticalFieldOfViewDegrees / 2.0f * pi / 180.0f)) * 180.0f / pi;
        cameraFovOrOrthographicSize = (verticalFieldOfViewDegreesWithPadding / 360.0f) * 2.0f * pi;

        if (!fClipped)
        {
            left = top = right = bottom = 0.0f;
        }

        auto applyCameraTransform = m_pContext->Get("applyCameraTransform").As<Napi::Function>();
        applyCameraTransform.Call(m_pContext->Value(),
            {Napi::Value::From(env, m_textureAspectRatio),
                Napi::Value::From(env, fOrthographic),
                Napi::Value::From(env, cameraFovOrOrthographicSize),
                Napi::Value::From(env, transform.GetNearClip()),
                Napi::Value::From(env, transform.GetFarClip()),
                Napi::Value::From(env, transform.GetPosition().x),
                Napi::Value::From(env, transform.GetPosition().y),
                Napi::Value::From(env, transform.GetPosition().z),
                Napi::Value::From(env, transform.GetTargetPoint().x),
                Napi::Value::From(env, transform.GetTargetPoint().y),
                Napi::Value::From(env, transform.GetTargetPoint().z),
                Napi::Value::From(env, transform.GetUpVector().x),
                Napi::Value::From(env, transform.GetUpVector().y),
                Napi::Value::From(env, transform.GetUpVector().z),
                Napi::Value::From(env, vpMinX / m_textureWidth),
                Napi::Value::From(env, vpMinY / m_textureHeight),
                Napi::Value::From(env, vpMaxX / m_textureWidth),
                Napi::Value::From(env, vpMaxY / m_textureHeight),
                Napi::Value::From(env, left),
                Napi::Value::From(env, top),
                Napi::Value::From(env, right),
                Napi::Value::From(env, bottom)});
    }

    void BabylonRendererImpl::DispatchToJsRuntime(std::function<void(Napi::Env, std::promise<void>&)>&& function) const
    {
        std::promise<void> done;

        m_pJsRuntime->Dispatch([&done, function = std::move(function)](Napi::Env env) {
            try
            {
                function(env, done);
            }
            catch (...)
            {
                done.set_exception(std::current_exception());
            }
        });

        done.get_future().get();
    }

    void BabylonRendererImpl::Release3DModel()
    {
        try
        {
            BeginFrame();

            DispatchToJsRuntime([this](Napi::Env env, std::promise<void>& done) {
                if (m_pContext)
                {
                    env.Global().Get("BI_destroyScene").As<Napi::Function>().Call({m_pContext->Value()});
                    m_pContext = nullptr;
                    done.set_value();
                }
            });

            EndFrame();
        }
        catch (...) // std::lock_guard might throw
        {
            assert(false);
        }
    }

    void BabylonRendererImpl::Render(const Rect& viewport, const Matrix4& sceneTransform, const ICameraTransform& cameraTransform, bool fClipped)
    {
        WaitForSceneReady();

        DispatchToJsRuntime([this, &sceneTransform, &viewport, &cameraTransform, fClipped](Napi::Env env, std::promise<void>& done) {
            ApplyRootNodeTransform(env, sceneTransform);
            ApplyCameraTransform(env, cameraTransform, viewport.Left(), viewport.Top(), viewport.Right(), viewport.Bottom(), fClipped);
            RenderJS(env);
            done.set_value();
        });

        RenderFrame();

        CopyRenderTextureToOutput();
        EndFrame();
    }

    void BabylonRendererImpl::LoadModel3D(std::vector<char> modelData, std::vector<char> environmentData)
    {
        DispatchToJsRuntime([this, &modelData, environmentData](Napi::Env env, std::promise<void>& done) {
            auto jsEnvironmentData = Napi::ArrayBuffer::New(env, environmentData.size());
            std::memcpy(jsEnvironmentData.Data(), environmentData.data(), jsEnvironmentData.ByteLength());

            auto jsModelData = Napi::ArrayBuffer::New(env, modelData.size());
            std::memcpy(jsModelData.Data(), modelData.data(), jsModelData.ByteLength());

            auto createSceneAsync = env.Global().Get("BI_createSceneAsync").As<Napi::Function>();

            auto onFulfilled = [this, &done](const Napi::CallbackInfo& info) {
                auto context = info[0].As<Napi::Object>();
                m_pContext = std::make_shared<Napi::ObjectReference>(Napi::Persistent(context));
                done.set_value();
            };

            auto onRejected = [&done](const Napi::CallbackInfo& info) {
                auto errorString = info[0].ToString().Utf8Value();
                assert(false);
                done.set_exception(std::make_exception_ptr(std::runtime_error{errorString}));
            };

            auto promise = createSceneAsync.Call({jsEnvironmentData, jsModelData}).As<Napi::Promise>();
            promise.Get("then").As<Napi::Function>().Call(promise, {Napi::Function::New(env, onFulfilled, "onFulfilled")});
            promise.Get("catch").As<Napi::Function>().Call(promise, {Napi::Function::New(env, onRejected, "onRejected")});
        });
    }

    void BabylonRendererImpl::WaitForSceneReady()
    {
        std::promise<void> done;
        std::promise<void> renderFrame;

        m_pJsRuntime->Dispatch([this, &done, &renderFrame](Napi::Env env) {
            auto jsRender = m_pContext->Get("waitForSceneReadyAsync").As<Napi::Function>();

            if (jsRender)
            {
                auto promise = jsRender.Call(m_pContext->Value(), {}).As<Napi::Promise>();

                auto onFulfilled = Napi::Function::New(env, [this, &done](const Napi::CallbackInfo&) { 
                   done.set_value(); 
                }, "onFulfilled");

                auto onRejected = Napi::Function::New(env, [&done](const Napi::CallbackInfo& info) {
                     auto errorString = info[0].ToString().Utf8Value();
                     assert(false);
                     done.set_exception(std::make_exception_ptr(std::runtime_error { errorString })); 
                }, "onRejected");

                promise.Get("then").As<Napi::Function>().Call(promise, {onFulfilled});
                promise.Get("catch").As<Napi::Function>().Call(promise, {onRejected});

                renderFrame.set_value();
            }
            else
            {
                renderFrame.set_value();
                done.set_value();
            }
        });

        renderFrame.get_future().get();

        std::future<void> doneFuture = done.get_future();

        while (std::future_status::timeout == doneFuture.wait_for(std::chrono::milliseconds(16)))
        {
            RenderFrame();
        }

        doneFuture.get();
    }

    void BabylonRendererImpl::Init()
    {
        BABYLON_GRAPHICS_DEBUG_INIT();

        if (m_pGraphicsDevice)
        {
            return;
        }

        ::Babylon::Graphics::Configuration config;

        m_pGraphicsDevice = std::make_unique<::Babylon::Graphics::Device>(config);
        m_pGraphicsDeviceUpdate = std::make_unique<::Babylon::Graphics::DeviceUpdate>(m_pGraphicsDevice->GetUpdate("update"));

        // Initialize with the frame started so that JS will not block when calling native engine APIs.
        BeginFrame();

        auto jsOptions = ::Babylon::AppRuntime::Options{};
        jsOptions.EnableDebugger = false;
        jsOptions.WaitForDebugger = false;

        m_pJsRuntime = std::make_unique<Babylon::AppRuntime>(jsOptions);

        ::Babylon::ScriptLoader scriptLoader(*m_pJsRuntime);
        std::promise<void> done;

        scriptLoader.Dispatch([this](Napi::Env env) {
            m_pGraphicsDevice->AddToJavaScript(env);

            ::Babylon::Polyfills::Console::Initialize(env,
                [](const char* message, auto logLevel) {
                    std::ostringstream ss{};
                    ss << "[" << GetLogLevelString(logLevel) << "] " << message << std::endl;
                    ConsolePrint(ss.str());
                    std::cout << ss.str();
                    std::cout.flush();
                });

            ::Babylon::Polyfills::Window::Initialize(env);
            ::Babylon::Polyfills::XMLHttpRequest::Initialize(env);
            ::Babylon::Plugins::NativeEngine::Initialize(env);
        });

        scriptLoader.LoadScript(m_context.BabylonFilePath);

        scriptLoader.Dispatch([this, &done](Napi::Env env) {
            auto getEngineInfo = env.Global().Get("BI_getEngineInfo").As<Napi::Function>();
            auto engineInfo = getEngineInfo.Call({}).As<Napi::Object>();
            std::string engineVersion = engineInfo.Get("version").As<Napi::String>().Utf8Value();
            std::string engineName = engineInfo.Get("name").As<Napi::String>().Utf8Value();

            done.set_value();
        });

        // Wait for script loader to complete before continuing.
        done.get_future().get();
    }
}