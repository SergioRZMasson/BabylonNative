#ifndef BABYLON_LITE_H
#define BABYLON_LITE_H

/*
 * Babylon Lite native contract, revision 1.
 *
 * Authority: original @babylonjs/lite 1.32.0 TypeScript declarations AND bodies,
 * source pin 2e064d88ec7422af946f8ec7f089ac6519f99295. Scope is the complete
 * original Minecraft demo's engine reach, not the package's entire export list.
 * This file declares a contract; it does not imply that every declaration has
 * already been implemented. No BabylonNative object model is part of this ABI.
 *
 * Naming map: original exported foo -> bl_foo. Property assignments and JS
 * collection operations use the explicit functions documented below. Runtime,
 * allocation, native target, service, status and handle helpers are adaptations,
 * NOT additional claims about the original browser API.
 *
 * All operations are synchronous on the runtime's creating thread. The Async
 * suffixes preserve source names: a JS binding wraps their returned result in a
 * Promise; native callers receive a completed status. Hosts own event loops,
 * input, browser-to-RML translation, PNG composition, file dialogs and demo/user-code algorithms.
 * Lite owns transforms, scene membership, draw ordering, resource retirement,
 * shader-interface generation, uniform values and audio master/source routing.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BL_CONTRACT_VERSION UINT32_C(1)
#define BL_INVALID_BGFX_HANDLE UINT16_C(65535)

typedef enum bl_Status
{
    BL_OK = 0,
    BL_INVALID_ARGUMENT,
    BL_INVALID_HANDLE,
    BL_DISPOSED,
    BL_WRONG_RUNTIME,
    BL_WRONG_ENGINE,
    BL_WRONG_THREAD,
    BL_OUT_OF_MEMORY,
    BL_UNSUPPORTED,
    BL_NOT_READY,
    BL_BUSY,
    BL_CANCELLED,
    BL_STOPPED,
    BL_DEVICE_LOST,
    BL_SHADER_ERROR,
    BL_HOST_ERROR,
    BL_AUDIO_UNAVAILABLE,
    BL_AUDIO_SUSPENDED
} bl_Status;

typedef struct bl_Runtime bl_Runtime;

/*
 * Typed, opaque identities, not pointers to public objects or method tables.
 * Copying a handle preserves identity. All-zero is the null handle. Never
 * inspect/write _runtime/_id. An ID includes a generation: disposing/reusing
 * storage must not make a stale identity refer to a new object.
 * Queries/mutations of previously disposed identities return DISPOSED; repeated
 * explicit disposal and scene removal are OK. Forged/unissued/wrong-type
 * identities return INVALID_HANDLE. Nullable parent/camera/material/texture
 * arguments accept an all-zero handle only where explicitly documented.
 * The runtime must outlive every handle and borrowed view. After runtime
 * destruction, even querying an old handle is invalid C usage.
 */
typedef struct bl_EngineContext
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_EngineContext;

typedef struct bl_SceneContext
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_SceneContext;

typedef struct bl_SceneNode
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_SceneNode;

typedef bl_SceneNode bl_TransformNode;

typedef struct bl_Mesh
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_Mesh;

typedef struct bl_FreeCamera
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_FreeCamera;

typedef struct bl_ShaderMaterial
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_ShaderMaterial;

typedef struct bl_Texture2D
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_Texture2D;

typedef struct bl_AudioEngine
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_AudioEngine;

typedef struct bl_AudioInputSource
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_AudioInputSource;

typedef struct bl_Vec2
{
    double x;
    double y;
} bl_Vec2;

typedef struct bl_Vec3
{
    double x;
    double y;
    double z;
} bl_Vec3;

typedef struct bl_Vec4
{
    double x;
    double y;
    double z;
    double w;
} bl_Vec4;

typedef bl_Vec4 bl_Quat;

typedef struct bl_Color4
{
    double r;
    double g;
    double b;
    double a;
} bl_Color4;

typedef struct bl_Mat4
{
    double values[16];
} bl_Mat4;

typedef struct bl_OptionalNumber
{
    bool present;
    double value;
} bl_OptionalNumber;

typedef enum bl_OptionalBool
{
    BL_BOOL_DEFAULT = 0,
    BL_BOOL_FALSE,
    BL_BOOL_TRUE
} bl_OptionalBool;

typedef struct bl_String
{
    const char* data;
    size_t length;
} bl_String;

typedef struct bl_Bytes
{
    const uint8_t* data;
    size_t count;
} bl_Bytes;

typedef struct bl_F32Span
{
    const float* data;
    size_t count;
} bl_F32Span;

typedef struct bl_U32Span
{
    const uint32_t* data;
    size_t count;
} bl_U32Span;

typedef struct bl_NumberSpan
{
    const double* data;
    size_t count;
} bl_NumberSpan;

/*
 * Strings are UTF-8 byte spans, need no trailing NUL, and cannot contain NUL
 * internally. Counts are elements, never bytes unless the field says Bytes.
 * NULL+zero is an empty span; NULL+nonzero is invalid. Calls copy input strings,
 * arrays and option tables before returning, unless expressly marked borrowed.
 * JS number values remain double; vertices, uniform backing arrays, pixels and
 * indices retain their original Float32/Uint8/Uint32 representations.
 */
typedef struct bl_Allocator
{
    void* userData;
    void* (*allocate)(void* userData, size_t bytes, size_t alignment);
    void (*deallocate)(void* userData, void* memory, size_t bytes, size_t alignment);
} bl_Allocator;

typedef struct bl_Error
{
    bl_Status status;
    uint32_t sourceErrorCode; /* Original Lite error number if applicable; else 0. */
    bl_String operation;
    bl_String message;
} bl_Error;

typedef void (*bl_ErrorCallback)(void* userData, const bl_Error* error);

/*
 * Every fallible function returns status. Output parameters are written only
 * on success, except shader compile diagnostics and bl_getLastError.
 * Failed calls leave observable state unchanged, except completed frame/audio
 * work cannot be rolled back. No missing service or failed shader may produce
 * fake success, a substituted shader, or a silently "audible" audio engine.
 * Last error is runtime-owned until the next error/runtime destruction; error
 * callback spans are valid only during the callback. No callback may throw
 * across C or re-enter Lite while it is a host/service/error callback.
 */

/* ---------------- Narrow injected runtime WGSL compiler service ----------- */

typedef enum bl_RendererBackend
{
    BL_RENDERER_AUTO = 0,
    BL_RENDERER_D3D11,
    BL_RENDERER_D3D12,
    BL_RENDERER_VULKAN,
    BL_RENDERER_METAL,
    BL_RENDERER_OPENGL,
    BL_RENDERER_OPENGLES
} bl_RendererBackend;

typedef enum bl_ShaderStage
{
    BL_STAGE_VERTEX = 1,
    BL_STAGE_FRAGMENT = 2
} bl_ShaderStage;

typedef enum bl_ShaderUniformType
{
    BL_UNIFORM_F32 = 0,
    BL_UNIFORM_U32,
    BL_UNIFORM_I32,
    BL_UNIFORM_VEC2,
    BL_UNIFORM_VEC3,
    BL_UNIFORM_VEC4,
    BL_UNIFORM_MAT4
} bl_ShaderUniformType;

typedef enum bl_VertexSemantic
{
    BL_ATTRIBUTE_POSITION = 0,
    BL_ATTRIBUTE_NORMAL,
    BL_ATTRIBUTE_UV,
    BL_ATTRIBUTE_UV2,
    BL_ATTRIBUTE_TANGENT,
    BL_ATTRIBUTE_COLOR
} bl_VertexSemantic;

typedef enum bl_NativeUniformType
{
    BL_NATIVE_UNIFORM_VEC4 = 0,
    BL_NATIVE_UNIFORM_MAT3,
    BL_NATIVE_UNIFORM_MAT4
} bl_NativeUniformType;

typedef struct bl_ShaderStageInput
{
    bl_ShaderStage stage;
    bl_String wgsl;
    bl_String entryPoint;
} bl_ShaderStageInput;

typedef struct bl_ShaderCompileRequest
{
    uint32_t contractVersion;
    bl_RendererBackend backend;       /* Concrete initialized backend, never AUTO. */
    uint32_t bgfxShaderBinaryVersion; /* Container version, not a native blob. */
    bl_ShaderStageInput vertex;
    bl_ShaderStageInput fragment;
    bool homogeneousDepth; /* Native NDC is [-1,1] rather than [0,1]. */
    bool originBottomLeft;
    bl_String label;
} bl_ShaderCompileRequest;

typedef struct bl_ReflectedAttribute
{
    bl_String name;
    uint32_t location; /* WGSL location, declaration-order for VertexInput. */
    bl_VertexSemantic semantic;
    uint8_t components; /* Canonical float32 stream: 2, 3 or 4. */
} bl_ReflectedAttribute;

typedef struct bl_ReflectedUniformBlock
{
    bl_String name; /* WGSL variable, e.g. shaderSystem or shaderUniforms. */
    uint32_t group;
    uint32_t binding;
    uint32_t byteSize; /* Original WGSL uniform layout, rounded to 16 bytes. */
    uint32_t stages;   /* Bitwise OR of BL_STAGE_* values. */
} bl_ReflectedUniformBlock;

typedef struct bl_ReflectedUniform
{
    /*
     * Semantic leaf path relative to its WGSL block, not a generated register.
     * Grammar: identifier ( "." identifier | "[" decimal-index "]" )*.
     * Identifiers are WGSL identifiers; indices are nonnegative decimal with
     * no leading zero except "0". Existing flat names are unchanged.
     * Fixed arrays/structs recurse at their semantic byte strides/offsets.
     * Float vectors remain vector leaves; integer vectors may be scalar leaves
     * indexed by component. Runtime arrays and ambiguous/overlapping paths fail.
     */
    bl_String name;
    uint32_t blockIndex; /* Index into result.uniformBlocks. */
    uint32_t byteOffset;
    uint32_t byteSize;
    bl_ShaderUniformType type;
    uint32_t stages;
    bl_String nativeName; /* Exact createUniform name in the bgfx containers. */
    bl_NativeUniformType nativeType;
    uint16_t nativeCount;
    uint32_t nativeByteOffset; /* Destination within nativeCount packed values. */
} bl_ReflectedUniform;

typedef struct bl_ReflectedTexture
{
    bl_String name;        /* Original texture variable, e.g. atlasTex. */
    bl_String samplerName; /* Original associated WGSL sampler variable. */
    uint32_t textureGroup;
    uint32_t textureBinding;
    uint32_t samplerGroup;
    uint32_t samplerBinding;
    uint32_t stages;
    bl_String nativeName;       /* bgfx sampler uniform name. */
    uint8_t nativeTextureStage; /* bgfx setTexture stage, not WGSL binding. */
} bl_ReflectedTexture;

typedef struct bl_ShaderCompileResult
{
    bl_Bytes vertexContainer;
    bl_Bytes fragmentContainer;
    const bl_ReflectedAttribute* attributes;
    size_t attributeCount;
    const bl_ReflectedUniformBlock* uniformBlocks;
    size_t uniformBlockCount;
    const bl_ReflectedUniform* uniforms;
    size_t uniformCount;
    const bl_ReflectedTexture* textures;
    size_t textureCount;
    bl_String diagnostics; /* Warnings on success, explanation on failure. */
    void* allocation;      /* Opaque compiler-owned result/release token. */
} bl_ShaderCompileResult;

typedef struct bl_ShaderCompilerService
{
    void* userData;
    bl_Status (*compile)(void* userData, const bl_ShaderCompileRequest* request,
                         bl_ShaderCompileResult* result);
    void (*release)(void* userData, bl_ShaderCompileResult* result);
} bl_ShaderCompilerService;

/*
 * compile receives arbitrary complete WGSL modules and arbitrary entry names.
 * Lite, not a JS binding, builds its original material prelude (VertexInput,
 * shaderSystem, shaderUniforms, texture/sampler declarations and defines).
 * The compiler must emit real bgfx shader containers for the requested binary
 * version/backend, with linked varying signatures, AND complete active binding
 * reflection. Raw DXBC/DXIL/SPIR-V/MSL or text renamed to a container is invalid.
 * Dead declarations may be absent; unsupported constructs return SHADER_ERROR
 * or UNSUPPORTED with diagnostics, never a demo-specific canned shader.
 *
 * Original WGSL uniform blocks may differ from native register storage. The
 * compiler performs IR-level lowering/consolidation to the bgfx-supported
 * backend uniform buffer (D3D11 uses its b0 scratch buffer). Core must never
 * rewrite user WGSL to guess b-registers. nativeByteOffset/nativeCount describe
 * packing into Vec4/Mat3/Mat4 uniforms. Entries sharing nativeName must agree on
 * nativeType/count and have nonoverlapping destinations; the core zero-fills
 * padding, packs members, and submits that native uniform once per draw.
 * Scalars/vectors preserve f32/i32/u32 bit representation; matrices use
 * column-major storage. Source WGSL offsets remain reflected independently.
 *
 * All result memory, including strings, belongs to the compiler. Lite copies
 * needed bytes/reflection and calls release EXACTLY ONCE after EVERY compile
 * invocation, even one returning failure, on the same thread. result starts
 * zeroed; release must accept an empty/partial result. request memory is
 * borrowed for that call only. Service lifetime extends to runtime destruction.
 */

/* ---------------- Host image/file and actual audio primitives ------------ */

typedef struct bl_HostBuffer
{
    const uint8_t* data;
    size_t byteCount;
    void* allocation;
} bl_HostBuffer;

typedef struct bl_HostImage
{
    const uint8_t* rgba8;
    uint32_t width;
    uint32_t height;
    size_t rowStrideBytes;
    void* allocation;
} bl_HostImage;

typedef struct bl_IOService
{
    void* userData;
    bl_Status (*readFile)(void* userData, bl_String pathOrUrl, bl_HostBuffer* result);
    bl_Status (*writeFile)(void* userData, bl_String path, bl_Bytes bytes);
    bl_Status (*decodeImage)(void* userData, bl_Bytes encoded, bl_HostImage* result);
    void (*releaseBuffer)(void* userData, bl_HostBuffer* result);
    void (*releaseImage)(void* userData, bl_HostImage* result);
} bl_IOService;
/*
 * IO result bytes belong to the host until the paired release callback.
 * Images are top-to-bottom straight-alpha RGBA8, rowStrideBytes >= width*4;
 * caller handles resampling/atlas composition, never Lite renderer internals.
 * Failed reads/decodes may return a partial zero-initialized result: caller
 * releases it once. No host IO result or channel pointer may escape its stated
 * lifetime. Null service getters return UNSUPPORTED, not a successful null.
 */

typedef struct bl_HostAudioContextTag* bl_HostAudioContext;
typedef struct bl_HostAudioNodeTag* bl_HostAudioNode;
typedef struct bl_HostAudioBufferTag* bl_HostAudioBuffer;

typedef enum bl_AudioState
{
    BL_AUDIO_RUNNING = 0,
    BL_AUDIO_STATE_SUSPENDED,
    BL_AUDIO_CLOSED,
    BL_AUDIO_INTERRUPTED
} bl_AudioState;

typedef enum bl_AudioParameter
{
    BL_AUDIO_GAIN = 0,
    BL_AUDIO_FREQUENCY,
    BL_AUDIO_PLAYBACK_RATE
} bl_AudioParameter;

typedef enum bl_OscillatorType
{
    BL_OSCILLATOR_SINE = 0,
    BL_OSCILLATOR_TRIANGLE
} bl_OscillatorType;

typedef struct bl_HostAudioInfo
{
    bl_AudioState state;
    double currentTime; /* Seconds, driven by audio clock, not frame delta. */
    double sampleRate;
    bool offline;                /* Offline rendering is running, but is NOT audible. */
    bool audibleOutputAvailable; /* Real output device/path, not just unlock. */
} bl_HostAudioInfo;

/*
 * Host implements native audio/OfflineAudioContext equivalents, not Minecraft
 * playBreak/playStep algorithms. Demo synthesis stays user code. All node
 * tokens are context-affine. Successful create callbacks return non-NULL.
 * Buffers/nodes retain connected/referenced inputs until disconnected/released;
 * release removes caller ownership, not a graph's references. Channel data is
 * writable Float32 storage stable until buffer release. Hosts may not read it
 * concurrently with user writes. start/stop use absolute audio-clock seconds.
 */
typedef struct bl_AudioService
{
    void* userData;
    bl_Status (*createContext)(void* userData, bl_HostAudioContext* context);
    bl_Status (*closeContext)(void* userData, bl_HostAudioContext context);
    bl_Status (*resumeContext)(void* userData, bl_HostAudioContext context);
    bl_Status (*getContextInfo)(void* userData, bl_HostAudioContext context,
                                bl_HostAudioInfo* info);
    bl_Status (*getDestination)(void* userData, bl_HostAudioContext context,
                                bl_HostAudioNode* node); /* Borrowed. */
    bl_Status (*createGain)(void* userData, bl_HostAudioContext context, bl_HostAudioNode* node);
    bl_Status (*createOscillator)(void* userData, bl_HostAudioContext context,
                                  bl_HostAudioNode* node);
    bl_Status (*createBiquadLowpass)(void* userData, bl_HostAudioContext context,
                                     bl_HostAudioNode* node);
    bl_Status (*createBufferSource)(void* userData, bl_HostAudioContext context,
                                    bl_HostAudioNode* node);
    bl_Status (*createBuffer)(void* userData, bl_HostAudioContext context, uint32_t channels,
                              size_t frames, double sampleRate, bl_HostAudioBuffer* buffer);
    bl_Status (*getChannelData)(void* userData, bl_HostAudioBuffer buffer, uint32_t channel,
                                float** samples, size_t* frames);
    bl_Status (*setBuffer)(void* userData, bl_HostAudioNode source, bl_HostAudioBuffer buffer);
    bl_Status (*setLoop)(void* userData, bl_HostAudioNode source, bool loop);
    bl_Status (*setOscillatorType)(void* userData, bl_HostAudioNode oscillator,
                                   bl_OscillatorType type);
    bl_Status (*connect)(void* userData, bl_HostAudioNode source, bl_HostAudioNode target);
    bl_Status (*connectParameter)(void* userData, bl_HostAudioNode source, bl_HostAudioNode target,
                                  bl_AudioParameter parameter);
    bl_Status (*disconnect)(void* userData, bl_HostAudioNode source);
    bl_Status (*setParameter)(void* userData, bl_HostAudioNode node, bl_AudioParameter parameter,
                              double value, double atTime);
    bl_Status (*getParameter)(void* userData, bl_HostAudioNode node, bl_AudioParameter parameter,
                              double* value);
    bl_Status (*cancelScheduledParameter)(void* userData, bl_HostAudioNode node,
                                          bl_AudioParameter parameter, double fromTime);
    bl_Status (*setParameterCurve)(void* userData, bl_HostAudioNode node,
                                   bl_AudioParameter parameter, bl_F32Span values, double startTime,
                                   double duration);
    bl_Status (*linearRampParameter)(void* userData, bl_HostAudioNode node,
                                     bl_AudioParameter parameter, double value, double endTime);
    bl_Status (*exponentialRampParameter)(void* userData, bl_HostAudioNode node,
                                          bl_AudioParameter parameter, double value,
                                          double endTime);
    bl_Status (*start)(void* userData, bl_HostAudioNode source, double atTime);
    bl_Status (*stop)(void* userData, bl_HostAudioNode source, double atTime);
    void (*releaseNode)(void* userData, bl_HostAudioNode node);
    void (*releaseBuffer)(void* userData, bl_HostAudioBuffer buffer);
} bl_AudioService;

typedef struct bl_RuntimeOptions
{
    bl_Allocator allocator;   /* Both callbacks required; alignment is a power of 2. */
    bl_ErrorCallback onError; /* Optional. */
    void* errorUserData;
    const bl_ShaderCompilerService* shaderCompiler; /* Optional until rendering. */
    const bl_AudioService* audio; /* Optional; audio creation then fails explicitly. */
    const bl_IOService* io;       /* Optional user-code image/file conveniences. */
} bl_RuntimeOptions;

bl_Status bl_createRuntime(const bl_RuntimeOptions* options, bl_Runtime** runtime);
bl_Status bl_disposeRuntime(bl_Runtime* runtime);
bl_Status bl_getLastError(bl_Runtime* runtime, bl_Error* error);
bl_Status bl_getIOService(bl_Runtime* runtime, const bl_IOService** service);

/*
 * Runtime copies service tables, borrows userdata. Allocator must remain valid
 * until disposal. Runtime creation requires no renderer/window. disposeRuntime
 * disposes live resources, waits for Lite-owned GPU work to retire, then frees
 * the runtime; illegal from callbacks/in-flight render (BUSY).
 */

/* ---------------- Engine native embedding, not browser API ---------------- */

typedef enum bl_BgfxOwnership
{
    BL_BGFX_OWNED = 0, /* Lite performs init/reset/frame/shutdown. */
    BL_BGFX_BORROWED   /* Host initialized bgfx; Lite NEVER does these operations. */
} bl_BgfxOwnership;

typedef enum bl_NativeTargetKind
{
    BL_TARGET_SWAPCHAIN = 0,
    BL_TARGET_BGFX_FRAMEBUFFER
} bl_NativeTargetKind;

typedef enum bl_ColorFormat
{
    BL_COLOR_RGBA8 = 0,
    BL_COLOR_BGRA8,
    BL_COLOR_RGBA16F
} bl_ColorFormat;

typedef enum bl_DepthFormat
{
    BL_DEPTH_NONE = 0,
    BL_DEPTH_D24S8,
    BL_DEPTH_D32F
} bl_DepthFormat;

typedef struct bl_NativeTarget
{
    bl_NativeTargetKind kind;
    uint16_t framebufferIndex; /* bgfx FrameBufferHandle.idx, host-owned. */
    uint32_t width;            /* Physical pixels, >=1. */
    uint32_t height;           /* Physical pixels, >=1. */
    bl_ColorFormat colorFormat;
    bl_DepthFormat depthFormat;
    uint8_t sampleCount;  /* 1, 2, 4, 8 or 16; source default is 4. */
    uint16_t firstViewId; /* Host grants exclusive contiguous range. */
    uint16_t viewCount;
} bl_NativeTarget;

typedef struct bl_NativePlatformData
{
    void* nativeDisplayType;
    void* nativeWindowHandle;
    void* context;
    void* backBuffer;
    void* backBufferDepthStencil;
} bl_NativePlatformData;

typedef struct bl_NativeEngineOptions
{
    bl_BgfxOwnership ownership;
    bl_RendererBackend backend;
    bl_NativePlatformData platform;
    bl_NativeTarget target;
    bool vsync;
    bool singleThreadedRenderer;
    void* hostUserData;
    /*
     * Required for both ownership modes. Reports initialization by the HOST,
     * never initialization performed by Lite. Do not infer this from getCaps:
     * bgfx retains capability data after shutdown. Must not re-enter Lite.
     */
    bool (*isExternalBgfxInitialized)(void* hostUserData);
    /*
     * BORROWED only: synchronize already submitted work for Lite resource
     * retirement/teardown. Host must progress its renderer and block until all
     * preceding submits are safe to destroy; must not call back into Lite.
     */
    bl_Status (*waitForSubmittedWork)(void* hostUserData);
} bl_NativeEngineOptions;

typedef struct bl_EngineOptions
{
    bl_OptionalBool useHighPrecisionMatrix; /* Source default false. */
} bl_EngineOptions;

typedef struct bl_EngineStats
{
    uint64_t drawCallCount;
    double gpuFrameTimeMs; /* 0 unless a real supported timer is enabled. */
} bl_EngineStats;

typedef void (*bl_CompletionCallback)(void* userData, bl_Status status);

bl_Status bl_createEngine(bl_Runtime* runtime, const bl_NativeEngineOptions* nativeOptions,
                          const bl_EngineOptions* options, bl_EngineContext* engine);
bl_Status bl_disposeEngine(bl_EngineContext engine);
bl_Status bl_getEngineStats(bl_EngineContext engine, bl_EngineStats* stats);
bl_Status bl_setNativeTarget(bl_EngineContext engine, const bl_NativeTarget* target);
bl_Status bl_waitForGpuIdle(bl_EngineContext engine);
bl_Status bl_waitForGpuResourceRetirements(bl_EngineContext engine);
bl_Status bl_startEngine(bl_EngineContext engine, bl_CompletionCallback firstFrame, void* userData);
bl_Status bl_stopEngine(bl_EngineContext engine);
bl_Status bl_frame(bl_EngineContext engine, double deltaMs);
bl_Status bl_renderFrame(bl_EngineContext engine, double deltaMs);

/*
 * bgfx is process-global. At most one OWNED engine may exist, and it cannot
 * coexist with independently initialized host bgfx or live BORROWED engines.
 * Lite tracks its own successful init/shutdown transitions separately.
 * OWNED creation returns BUSY if an owner exists or the host reports external
 * initialization. BORROWED creation requires external initialization (else
 * NOT_READY) and no Lite owner (else BUSY). Both require the state callback;
 * omitting it is INVALID_ARGUMENT, not permission to probe an uninitialized
 * renderer. Hosts serialize external init/shutdown with all Lite engine
 * creation/disposal and keep external bgfx initialized while borrowers live.
 * BORROWED engines share the
 * host device/backend but have nonoverlapping view ranges. Host owns borrowed
 * native target/framebuffer lifetimes and cannot destroy/reset them in a frame.
 * BORROWED requires waitForSubmittedWork; shutdown is host-owned after all Lite
 * engines are disposed. Native framebuffer numeric metadata is an adaptation,
 * never a C++ bgfx type. Frame submission must keep requested draw order (bgfx
 * sequential view mode); opaque/translucent order cannot be backend state sort.
 *
 * startEngine arms host-driven bl_frame, never starts a window loop. The first
 * bl_frame after start passes 0ms (original first RAF), then supplied deltaMs.
 * firstFrame callback runs once after that frame's successful submission; stop
 * before then completes it with CANCELLED. bl_frame on a stopped engine returns
 * STOPPED. bl_renderFrame always uses the supplied finite nonnegative deltaMs
 * regardless of running state, as original renderFrame does. No active scenes
 * means zero draws. BORROWED records/submits into host bgfx; host calls frame.
 * OWNED advances bgfx once per render call. Target resize and first-frame
 * completion occur before/after work, respectively, not inside user callbacks.
 */

/* ---------------- Optional retained RmlUI/bgfx native UI ------------------ */

typedef struct bl_UiContext
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_UiContext;

typedef struct bl_UiElement
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_UiElement;

typedef struct bl_UiContextOptions
{
    bl_NativeTarget target; /* Exclusive later view range, outside engine ranges. */
    double densityRatio;    /* RmlUI dp-to-physical-pixel ratio, finite (0,16]. */
} bl_UiContextOptions;

typedef struct bl_UiStats
{
    uint64_t geometryCompileCount;
    uint64_t geometryReleaseCount;
    uint64_t textureCreateCount;
    uint64_t textureReleaseCount;
    uint64_t drawCount; /* Most recent render. */
    uint64_t uploadedBytes;
    size_t liveGeometryCount;
    size_t liveTextureCount;
    size_t liveElementCount;
} bl_UiStats;

typedef enum bl_UiInputKind
{
    BL_UI_POINTER_MOVE = 0,
    BL_UI_POINTER_DOWN,
    BL_UI_POINTER_UP,
    BL_UI_POINTER_LEAVE,
    BL_UI_WHEEL,
    BL_UI_KEY_DOWN,
    BL_UI_KEY_UP,
    BL_UI_TEXT
} bl_UiInputKind;

typedef enum bl_UiKey
{
    BL_UI_KEY_UNKNOWN = 0,
    BL_UI_KEY_BACKSPACE,
    BL_UI_KEY_TAB,
    BL_UI_KEY_ENTER,
    BL_UI_KEY_ESCAPE,
    BL_UI_KEY_SPACE,
    BL_UI_KEY_LEFT,
    BL_UI_KEY_RIGHT,
    BL_UI_KEY_UP_ARROW,
    BL_UI_KEY_DOWN_ARROW,
    BL_UI_KEY_HOME,
    BL_UI_KEY_END,
    BL_UI_KEY_DELETE,
    BL_UI_KEY_A,
    BL_UI_KEY_C,
    BL_UI_KEY_V,
    BL_UI_KEY_X,
    BL_UI_KEY_Y,
    BL_UI_KEY_Z
} bl_UiKey;

#define BL_UI_MOD_SHIFT UINT32_C(1)
#define BL_UI_MOD_CONTROL UINT32_C(2)
#define BL_UI_MOD_ALT UINT32_C(4)
#define BL_UI_MOD_META UINT32_C(8)

typedef struct bl_UiInput
{
    bl_UiInputKind kind;
    double x;        /* Physical pointer coordinate, or horizontal wheel delta. */
    double y;        /* Physical pointer coordinate, or vertical wheel delta. */
    uint32_t button; /* 0 left, 1 right, 2 middle. */
    bl_UiKey key;
    uint32_t modifiers;
    bl_String text; /* UTF-8 for BL_UI_TEXT only. */
} bl_UiInput;

typedef enum bl_UiEventKind
{
    BL_UI_EVENT_CLICK = 0,
    BL_UI_EVENT_CHANGE,
    BL_UI_EVENT_POINTER_DOWN,
    BL_UI_EVENT_POINTER_UP,
    BL_UI_EVENT_KEY_DOWN,
    BL_UI_EVENT_KEY_UP
} bl_UiEventKind;

typedef struct bl_UiEvent
{
    bl_UiEventKind kind;
    bl_UiElement currentTarget;
    bl_UiElement target; /* Null for markup-created targets without a C99 identity. */
    bl_Vec2 position;
    uint32_t button;
    bl_UiKey key;
    uint32_t modifiers;
    bl_String value; /* Borrowed through callback only. */
} bl_UiEvent;

typedef void (*bl_UiEventCallback)(void* userData, const bl_UiEvent* event);

typedef struct bl_UiListenerToken
{
    uint64_t value;
} bl_UiListenerToken;

bl_Status bl_createUiContext(bl_EngineContext engine, const bl_UiContextOptions* options,
                             bl_UiContext* context);
bl_Status bl_disposeUiContext(bl_UiContext context);
bl_Status bl_setUiViewport(bl_UiContext context, uint32_t width, uint32_t height,
                           double densityRatio);
bl_Status bl_getUiRoot(bl_UiContext context, bl_UiElement* root);
bl_Status bl_createUiElement(bl_UiContext context, bl_String tag, bl_UiElement* element);
bl_Status bl_appendUiChild(bl_UiElement parent, bl_UiElement child);
bl_Status bl_removeUiChild(bl_UiElement parent, bl_UiElement child);
bl_Status bl_disposeUiElement(bl_UiElement element);
bl_Status bl_setUiProperty(bl_UiElement element, bl_String name, bl_String value);
bl_Status bl_removeUiProperty(bl_UiElement element, bl_String name);
bl_Status bl_setUiText(bl_UiElement element, bl_String text);
bl_Status bl_setUiAttribute(bl_UiElement element, bl_String name, bl_String value);
bl_Status bl_setUiMarkup(bl_UiElement element, bl_String markup);
bl_Status bl_loadUiFont(bl_UiContext context, bl_Bytes bytes, bl_String family, uint32_t weight,
                        bool italic, bool fallback);
bl_Status bl_registerUiImage(bl_UiContext context, bl_String source, bl_Bytes straightRgba8,
                             uint32_t width, uint32_t height);
bl_Status bl_unregisterUiImage(bl_UiContext context, bl_String source);
bl_Status bl_setUiImageSampling(bl_UiContext context, bl_String source, bool pixelated);
bl_Status bl_processUiInput(bl_UiContext context, const bl_UiInput* input, bool* consumed);
bl_Status bl_updateUi(bl_UiContext context, double monotonicTimeSeconds);
bl_Status bl_renderUi(bl_UiContext context);
bl_Status bl_setUiWhiteDifference(bl_UiContext context, bool enabled);
bl_Status bl_getUiStats(bl_UiContext context, bl_UiStats* stats);
bl_Status bl_addUiEventListener(bl_UiElement element, bl_UiEventKind kind,
                                bl_UiEventCallback callback, void* userData,
                                bl_UiListenerToken* token);
bl_Status bl_removeUiEventListener(bl_UiElement element, bl_UiListenerToken token);

/*
 * UI is optional; disabled builds return UNSUPPORTED without initialization.
 * Initial UI contexts require a BORROWED engine. Host renders 3D, renders UI,
 * then calls bgfx::frame ONCE. UI never presents/resets/advances bgfx. Target
 * framebuffer remains host-owned; UI render preserves its color/depth. D24S8
 * is required if transformed clipping uses a stencil mask. Views must be later
 * than the attached engine and nonoverlapping with every engine/UI reservation.
 * All UI contexts in the process share one creating thread; another thread is
 * rejected. Multiple runtimes on that thread may each own independent contexts.
 *
 * Context owns document/detached elements/image registrations. Its destruction
 * invalidates all its element handles and waits for actual submitted work.
 * Engine disposal is BUSY while contexts remain attached. Runtime cascading
 * disposal destroys UI before engines. Root element disposal is BUSY; dispose
 * context instead. Element disposal destroys/invalidate its subtree. Append
 * requires a detached child, rejects cycles/duplicates/cross-context handles.
 * Remove detaches and preserves identity. Markup/text replacement invalidates
 * replaced descendant handles. All strings/images/font bytes are copied.
 *
 * Properties are RmlUI names/values, not browser cssText or browser shorthand
 * emulation. Text is plain escaped UTF-8; markup is an explicit RML boundary.
 * Unknown properties/unsupported effects fail explicitly, never fake success.
 * Registered images are tight top-to-bottom straight-alpha RGBA8; backend
 * converts them to RmlUI premultiplied alpha once. Unregister is BUSY while
 * retained RmlUI textures reference the source. Other sources use runtime IO.
 * Registered sources default to linear sampling. setUiImageSampling selects
 * point minification/magnification when pixelated=true, before texture retention;
 * retained sources return BUSY, unknown sources INVALID_ARGUMENT. This setting
 * is source-level, not per-element browser CSS or a font-atlas sampler override.
 * WhiteDifference defaults false. Enabled contexts use exact white-source
 * difference composition with source-over alpha; colored geometry, textures,
 * gradient stops and offscreen layers are UNSUPPORTED, not approximated.
 * The setting is BUSY during dispatch/update/render/teardown. Hosts give this
 * subtree its own context/view range and order it relative to normal HUD and
 * underwater contexts. It does not implement arbitrary colored-source difference.
 * Missing IO/images/fonts are explicit update/render errors. No white fallback.
 * Font family/weight (1..1000)/italic/fallback describe the RmlUI registration.
 * Its global font engine retains copied faces until LAST UI context shutdown.
 * RmlUI/FreeType allocations and font-byte retention are external to runtime
 * allocator counters; Core identities/upload staging/resources use allocator.
 * Update consumes finite process-wide nondecreasing monotonic seconds; all
 * contexts share that clock, and Core owns no clock/event loop.
 * Update after mutations before rendering; render requires an initial update.
 * densityRatio maps RmlUI dp units to physical pixels; RmlUI px stay physical.
 * Input pointer coordinates are physical. consumed reports RmlUI propagation.
 *
 * Event callbacks may mutate style/text/attributes. Topology/listener mutation,
 * nested update/render/input and disposal during dispatch return BUSY. Callback
 * spans/userdata are borrowed until removal/context disposal and MUST NOT throw.
 * This narrow event transport does not implement the complete browser DOM.
 * Retained-resource/effect/layout failures are latched by the context: later
 * update/render calls continue reporting the failure rather than silently
 * omitting failed cached content. Recreate the context after such a failure.
 * External RmlUI initialization/interfaces cannot coexist with Lite UI.
 * External allocator failure during RmlUI's non-transactional global startup
 * leaves UI unavailable in that process, rather than attempting unsafe reuse.
 * An external allocation failure during element ownership transfer can retire
 * the affected subtree's identities; context rendering then fails explicitly.
 */

/* ---------------- Data-only camera/transform nodes and properties --------- */

typedef struct bl_TransformNodeOptions
{
    bl_Vec3 position;
    bl_Quat rotationQuaternion;
    bl_Vec3 scaling;
} bl_TransformNodeOptions;

typedef struct bl_NodeChildren
{
    const bl_SceneNode* data; /* Borrowed until this hierarchy's next mutation. */
    size_t count;
} bl_NodeChildren;

typedef struct bl_CameraProperties
{
    double fov;
    double nearPlane;
    double farPlane;
    double speed;
    double angularSensitivity;
    double inertia;
} bl_CameraProperties;

bl_Status bl_createFreeCamera(bl_Runtime* runtime, bl_Vec3 position, bl_Vec3 target,
                              bl_FreeCamera* camera);
bl_Status bl_createTransformNode(bl_Runtime* runtime, bl_String name,
                                 const bl_TransformNodeOptions* options, bl_TransformNode* node);
bl_Status bl_meshNode(bl_Mesh mesh, bl_SceneNode* node);
bl_Status bl_cameraNode(bl_FreeCamera camera, bl_SceneNode* node);
bl_Status bl_getNodeName(bl_SceneNode node, bl_String* name);
bl_Status bl_setNodeName(bl_SceneNode node, bl_String name);
bl_Status bl_getNodePosition(bl_SceneNode node, bl_Vec3* position);
bl_Status bl_setNodePosition(bl_SceneNode node, bl_Vec3 position);
bl_Status bl_getNodeScaling(bl_SceneNode node, bl_Vec3* scaling);
bl_Status bl_setNodeScaling(bl_SceneNode node, bl_Vec3 scaling);
bl_Status bl_getNodeRotation(bl_SceneNode node, bl_Vec3* eulerXYZ);
bl_Status bl_setNodeRotation(bl_SceneNode node, bl_Vec3 eulerXYZ);
bl_Status bl_getNodeRotationQuaternion(bl_SceneNode node, bl_Quat* quaternion);
bl_Status bl_setNodeRotationQuaternion(bl_SceneNode node, bl_Quat quaternion);
bl_Status bl_getNodeParent(bl_SceneNode node, bl_SceneNode* parent);
bl_Status bl_setNodeParent(bl_SceneNode node, bl_SceneNode parent);
bl_Status bl_getNodeChildren(bl_SceneNode node, bl_NodeChildren* children);
bl_Status bl_appendNodeChild(bl_SceneNode node, bl_SceneNode child);
bl_Status bl_removeNodeChild(bl_SceneNode node, bl_SceneNode child);
bl_Status bl_getNodeVisible(bl_SceneNode node, bool* visible);
bl_Status bl_setNodeVisible(bl_SceneNode node, bool visible);
bl_Status bl_setSubtreeVisible(bl_SceneNode node, bool visible);
bl_Status bl_getNodeWorldMatrix(bl_SceneNode node, bl_Mat4* matrix, uint64_t* version);
bl_Status bl_getCameraTarget(bl_FreeCamera camera, bl_Vec3* target);
bl_Status bl_setCameraTarget(bl_FreeCamera camera, bl_Vec3 target);
bl_Status bl_getCameraProperties(bl_FreeCamera camera, bl_CameraProperties* properties);
bl_Status bl_setCameraProperties(bl_FreeCamera camera, const bl_CameraProperties* properties);
bl_Status bl_disposeNode(bl_SceneNode node); /* Native helper, not removeFromScene. */

/*
 * Transform defaults with NULL options: position/rotation=(0,0,0), quaternion
 * (0,0,0,1), scaling=(1,1,1), visible=true. A non-NULL transform option is a
 * complete TRS value, so zero scaling remains representable. FreeCamera defaults:
 * fov=.8 radians, near=1, far=10000, speed=2, angularSensitivity=2000, inertia=.9.
 * FreeCamera is left-handed, +Y up, position+target; no automatic controls.
 *
 * Original node.position/scaling/rotation.{x,y,z} writes -> get/modify/set the
 * corresponding Vec3; rotation.set(x,y,z) -> setNodeRotation. Setters dirty
 * derived matrices immediately. Euler XYZ uses the original cached proxy rule:
 * consecutive Euler writes retain the chosen Euler triple even near gimbal
 * lock; quaternion writes invalidate that cache. World matrix is column-major,
 * parentWorld * localTRS, and is refreshed lazily with a changed version.
 * Public scalars/transforms use double. useHighPrecisionMatrix=false matches
 * original F32 matrix storage; true uses F64 intermediates, both GPU-upload F32.
 *
 * node.parent=other -> setNodeParent; node.children.push(child) -> appendNodeChild.
 * These are deliberately SEPARATE operations, matching original field+array
 * semantics, not world-preserving source setParent(). Local TRS is unchanged.
 * append does not set parent; parent assignment does not append. A JS binding
 * must preserve both operations. Cycles/cross-runtime parenting are rejected.
 * addToScene recursively fixes children's parent links as original does.
 * Bare visible assignment maps setNodeVisible; setSubtreeVisible materializes
 * each descendant flag and invalidates rendering only if some flag changes.
 * Rendering shall observe visibility before its next draw; temporary hiding
 * does not dispose geometry. disposeNode rejects nodes still in any scene or
 * attached hierarchy (BUSY); detach/remove first. Last-scene mesh retirement
 * already disposes the mesh, so subsequent disposal is idempotent.
 * getNodeName borrows storage until name replacement/node disposal. Child-array
 * edits preserve order and reject cycles; removeNodeChild removes the first
 * matching occurrence, without implicitly changing its parent. Like original,
 * recursive scene removal clears parent links but keeps the node.children array.
 */

/* ---------------- Scenes, exact membership and callback ordering ---------- */

typedef struct bl_SceneProperties
{
    bl_Color4 clearColor;
    bl_FreeCamera camera; /* Null is no active camera. Assignment preserves identity. */
    double fixedDeltaMs;
} bl_SceneProperties;

typedef void (*bl_BeforeRenderCallback)(void* userData, double deltaMs);
typedef void (*bl_SceneDisposeCallback)(void* userData);

typedef struct bl_CallbackToken
{
    uint64_t value;
} bl_CallbackToken;

bl_Status bl_createSceneContext(bl_EngineContext engine, bl_SceneContext* scene);
bl_Status bl_getSceneProperties(bl_SceneContext scene, bl_SceneProperties* properties);
bl_Status bl_setSceneProperties(bl_SceneContext scene, const bl_SceneProperties* properties);
bl_Status bl_addToScene(bl_SceneContext scene, bl_SceneNode entity);
bl_Status bl_removeFromScene(bl_SceneContext scene, bl_SceneNode entity);
bl_Status bl_registerScene(bl_SceneContext scene);
bl_Status bl_unregisterScene(bl_SceneContext scene);
bl_Status bl_disposeScene(bl_SceneContext scene);
bl_Status bl_onBeforeRender(bl_SceneContext scene, bl_BeforeRenderCallback callback, void* userData,
                            bl_CallbackToken* token);
bl_Status bl_onSceneDispose(bl_SceneContext scene, bl_SceneDisposeCallback callback, void* userData,
                            bl_CallbackToken* token);
bl_Status bl_removeSceneCallback(bl_SceneContext scene, bl_CallbackToken token);

/*
 * scene.clearColor/camera/fixedDeltaMs -> get/modify/setSceneProperties. Defaults:
 * clearColor=(.2,.2,.3,1), camera=null, fixedDeltaMs=0. A positive fixedDeltaMs
 * overrides frame delta only for that scene. Callback delta units are MS.
 * onBeforeRender PREPENDS: callbacks execute newest-first (TS unshift), before
 * transforms, uniforms and draws. Scene-dispose callbacks execute registration
 * order once (TS push), during disposeScene; repeated dispose is a no-op.
 * Callback tokens/removal are native additions. Callback/userdata is borrowed
 * until removal/disposal. Before-render callbacks may mutate scene content,
 * remove meshes, change transforms/materials/visibility, or stop the engine.
 * Mutations affect the SAME rendered frame. Nested frames, runtime/engine
 * disposal and callback-list mutation during callback dispatch return BUSY.
 *
 * registerScene synchronously completes shader/pipeline preparation and
 * registers in engine scene order; duplicate registration is a no-op.
 * unregisterScene does not dispose; re-register appends at the end. Every
 * registered scene updates then draws in registration order. First scene
 * clears color+reversed depth (0); overlays preserve color and clear depth.
 * Within scene, opaque phase precedes transparent phase, regardless of
 * renderOrder. Opaque material-group renderables sort by ascending order;
 * a merged group's order is the minimum of its meshes' effective orders and
 * packets retain insertion order. An absent opaque ShaderMaterial order is
 * 100, an absent transparent order is 200. Transparent bindings sort FIRST
 * by descending view-space Z of their world translation (not bounds center
 * or radial distance), THEN ascending renderOrder. Stable insertion order
 * breaks ties. This reflects source bodies, not misleading demo comments
 * claiming that transparent renderOrder always takes precedence over depth.
 *
 * addToScene accepts mesh/transform/camera nodes and traverses children. Mesh
 * same-runtime/same-engine ownership is enforced. One identity can belong to
 * several scenes on the same engine. Original meshes array push is NOT a set:
 * duplicate addition appends another occurrence; implementations must not
 * silently reinterpret it as an idempotent set operation.
 * removeFromScene is idempotent, recursively removes children and detaches
 * parent; an active camera removal clears scene.camera. Removing a mesh from
 * its LAST owning scene permanently retires it; re-add then returns DISPOSED,
 * even when GPU destruction has not completed. Removal from one of several
 * scenes permits re-add. Visibility removal is immediate; GPU frees wait for
 * submitted work, including removal inside a before-render callback.
 * Scene disposal removes its ownership without invalidating another owning
 * scene. Materials and texture identities can be reused by newly created
 * meshes; removal must not destroy resources still shared by live owners.
 */

/* ---------------- Float32 geometry and dynamic mesh variants ------------- */

typedef struct bl_MeshGeometry
{
    bl_F32Span positions; /* Required XYZ, length=3*vertexCount. */
    bl_F32Span normals;   /* Required XYZ, same length as positions (zero is valid). */
    bl_U32Span indices;   /* Required triangle-list, 3*triangleCount. */
    bl_F32Span uvs;       /* Optional XY, length=2*vertexCount. */
    bl_F32Span uvs2;      /* Optional XY. */
    bl_F32Span tangents;  /* Optional XYZW. */
    bl_F32Span colors;    /* Optional RGBA, length=4*vertexCount; alpha preserved. */
} bl_MeshGeometry;

typedef struct bl_GeometryData
{
    float* positions;
    float* normals;
    float* uvs;
    uint32_t* indices;
    size_t vertexCount;
    size_t indexCount;
    void* allocation; /* Runtime-owned allocation token for freeGeometryData. */
} bl_GeometryData;

typedef struct bl_BoxOptions
{
    bl_OptionalNumber size;
    bl_OptionalNumber width;
    bl_OptionalNumber height;
    bl_OptionalNumber depth;
} bl_BoxOptions;

typedef struct bl_SphereOptions
{
    bl_OptionalNumber segments;
    bl_OptionalNumber diameter;
    bl_OptionalNumber diameterX;
    bl_OptionalNumber diameterY;
    bl_OptionalNumber diameterZ;
} bl_SphereOptions;

typedef struct bl_GroundOptions
{
    bl_OptionalNumber width;
    bl_OptionalNumber height;
    bl_OptionalNumber subdivisions;
    bool hasUvScale;
    bl_Vec2 uvScale;
} bl_GroundOptions;

typedef struct bl_MeshProperties
{
    bl_ShaderMaterial material;    /* Original default null, assigned before rendering. */
    bl_OptionalNumber renderOrder; /* Original undefined, not zero; any finite double. */
    bool receiveShadows;           /* Original default false; shadow subsystem not in this scope. */
} bl_MeshProperties;

typedef struct bl_GeometryRange
{
    size_t offset;
    size_t count;
} bl_GeometryRange;

typedef struct bl_GeometryUpdateRanges
{
    const bl_GeometryRange* vertices;
    size_t vertexRangeCount;
    const bl_GeometryRange* indices;
    size_t indexRangeCount;
} bl_GeometryUpdateRanges;

typedef struct bl_GeometryCapacityResult
{
    bool stable;
    size_t vertexCapacity;
    size_t indexCapacity;
} bl_GeometryCapacityResult;

bl_Status bl_createBoxData(bl_Runtime* runtime, const bl_BoxOptions* options,
                           bl_GeometryData* data);
bl_Status bl_createSphereData(bl_Runtime* runtime, const bl_SphereOptions* options,
                              bl_GeometryData* data);
bl_Status bl_createFlatGroundData(bl_Runtime* runtime, const bl_GroundOptions* options,
                                  bl_GeometryData* data);
bl_Status bl_freeGeometryData(bl_Runtime* runtime, bl_GeometryData* data);
bl_Status bl_createMeshFromData(bl_EngineContext engine, bl_String name,
                                const bl_MeshGeometry* geometry, bl_Mesh* mesh);
bl_Status bl_createBox(bl_EngineContext engine, const bl_BoxOptions* options, bl_Mesh* mesh);
bl_Status bl_createSphere(bl_EngineContext engine, const bl_SphereOptions* options, bl_Mesh* mesh);
bl_Status bl_createGround(bl_EngineContext engine, const bl_GroundOptions* options, bl_Mesh* mesh);
bl_Status bl_getMeshProperties(bl_Mesh mesh, bl_MeshProperties* properties);
bl_Status bl_setMeshProperties(bl_Mesh mesh, const bl_MeshProperties* properties);
bl_Status bl_updateMeshGeometry(bl_EngineContext engine, bl_Mesh mesh,
                                const bl_MeshGeometry* geometry);
bl_Status bl_resizeMeshGeometry(bl_EngineContext engine, bl_Mesh mesh,
                                const bl_MeshGeometry* geometry);
bl_Status bl_updateMeshGeometryCapacity(bl_EngineContext engine, bl_Mesh mesh,
                                        const bl_MeshGeometry* geometry,
                                        const bl_OptionalNumber* reserveFactor,
                                        const bl_GeometryUpdateRanges* ranges,
                                        bl_GeometryCapacityResult* result);
bl_Status bl_updateMeshPositions(bl_EngineContext engine, bl_Mesh mesh, bl_F32Span values,
                                 size_t vertexOffset, const size_t* vertexCount,
                                 size_t sourceVertexOffset);
bl_Status bl_updateMeshNormals(bl_EngineContext engine, bl_Mesh mesh, bl_F32Span values,
                               size_t vertexOffset, const size_t* vertexCount,
                               size_t sourceVertexOffset);
bl_Status bl_updateMeshColors(bl_EngineContext engine, bl_Mesh mesh, bl_F32Span values,
                              size_t vertexOffset, const size_t* vertexCount,
                              size_t sourceVertexOffset);
bl_Status bl_updateMeshUvs(bl_EngineContext engine, bl_Mesh mesh, bl_F32Span values,
                           size_t vertexOffset, const size_t* vertexCount,
                           size_t sourceVertexOffset);
bl_Status bl_updateMeshUv2(bl_EngineContext engine, bl_Mesh mesh, bl_F32Span values,
                           size_t vertexOffset, const size_t* vertexCount,
                           size_t sourceVertexOffset);
bl_Status bl_updateMeshTangents(bl_EngineContext engine, bl_Mesh mesh, bl_F32Span values,
                                size_t vertexOffset, const size_t* vertexCount,
                                size_t sourceVertexOffset);
bl_Status bl_invalidateRenderBundles(bl_EngineContext engine);

/*
 * Original positional createMeshFromData(engine,name,positions,normals,indices,
 * uvs?,uvs2?,tangents?,colors?) maps exactly to bl_MeshGeometry's named spans.
 * Input arrays are COPIED for safe C lifetimes, unlike JS's GC-retained CPU
 * arrays. Buffer mutation alone never uploads; explicit update calls do so.
 * Bounds use original min/max of active positions; every index must be in
 * bounds, counts/layout must be consistent, overflow is rejected. Empty valid
 * geometry has no bounds/draws. JS callers must not rely on native C copying
 * to introduce extra automatic updates absent from the original API.
 *
 * mesh.material/renderOrder -> get/modify/setMeshProperties (renderOrder is an
 * optional double, preserving source undefined vs explicitly zero). Material
 * swap changes every scene containing the identity before its next draw. Zero scale
 * is supported (highlight hides this way), not an invalid transform.
 * receiveShadows=true returns UNSUPPORTED in this no-shadow scope. A source
 * renderOrder write after preparation requires explicit source invalidation;
 * native setMeshProperties also invalidates the affected ordering as an
 * embedding convenience, rather than pretending source had an observable field.
 * createBox(engine,number) -> options.size={true,number}; NULL options is size=1.
 * Explicit width/height/depth override size. Data uses original 24 face vertices,
 * 36 indices, +Z/-Z/+X/-X/+Y/-Y face order and original UVs/winding.
 * Sphere defaults: segments=32, diameter=1, axis diameters override base;
 * segments=max(3,segments), totalZ=2+segments, totalY=2*totalZ. Segments must be
 * finite integral input in this native contract. 16 segments/diameter=4000
 * produces the demo sky, not a simplified proxy. Fresh mutable CPU arrays are
 * never shared across primitive factory calls. freeGeometryData zeroes data.
 * Ground maps the original module-exported createFlatGroundData helper and root
 * createGround factory, not a fictitious root createGroundData. NULL/zero options
 * default width=height=subdivisions=1 and uvScale=(1,1). Selected dimensions/scales
 * must be finite with finite generated F32 output; zero/negative values remain
 * valid. subdivisions must be a finite integral >=1 (explicit native safety
 * restriction, not a source clamp). Heightmap-only minHeight/maxHeight are omitted
 * because flat creation ignores them. Rows run +Z to -Z, columns -X to +X;
 * (s+1)^2 vertices, 6*s*s indices, Y=0, normals=(0,1,0). Quad indices preserve
 * (bottomRight,topRight,topLeft),(bottomLeft,bottomRight,topLeft). UV fractions
 * first round to F32, then multiply double scales and round again, including
 * signed zero. Data creation is CPU-only; mesh creation copies/uploads data,
 * sets name "ground", releases staging, and inserts no scene/material. Checked
 * count/byte/allocator/index-address and mesh GPU budgets precede allocation;
 * overflow is INVALID_ARGUMENT, allocation failure OUT_OF_MEMORY, outputs unchanged.
 *
 * updateMeshGeometry requires same counts/optional layout, refreshes retained
 * CPU data and bounds, and preserves GPU buffer identity. resizeMeshGeometry
 * preserves MESH identity but replaces buffers/layout and retires old buffers.
 * Capacity update defaults reserveFactor=1.25 (finite >=1), preserves buffers
 * within capacity, otherwise grows vertex capacity ceil(n*factor) and index
 * capacity ceil(ceil(n*factor)/3)*3. Unused indices become zeroed degenerate
 * triangles. ranges=NULL uploads all; non-NULL requires BOTH range lists
 * (empty means no upload), offsets/counts in vertices/indices, not floats.
 * Complete arrays still define active CPU geometry even with partial uploads.
 * Attribute-only updates are GPU-only (do NOT refresh CPU picking/bounds).
 * vertexCount=NULL defaults remaining source vertices; offsets default to 0
 * at a JS binding. Optional attribute updates require the attribute to exist.
 */

/* ---------------- Original ShaderMaterial declarations and value state ---- */

typedef enum bl_BlendMode
{
    BL_BLEND_ALPHA = 0,
    BL_BLEND_ADDITIVE
} bl_BlendMode;

typedef enum bl_DepthCompare
{
    BL_COMPARE_DEFAULT = 0,
    BL_COMPARE_NEVER,
    BL_COMPARE_LESS,
    BL_COMPARE_EQUAL,
    BL_COMPARE_LESS_EQUAL,
    BL_COMPARE_GREATER,
    BL_COMPARE_NOT_EQUAL,
    BL_COMPARE_GREATER_EQUAL,
    BL_COMPARE_ALWAYS
} bl_DepthCompare;

typedef enum bl_Topology
{
    BL_TOPOLOGY_TRIANGLE_LIST = 0,
    BL_TOPOLOGY_LINE_LIST,
    BL_TOPOLOGY_POINT_LIST
} bl_Topology;

typedef struct bl_ShaderUniformDecl
{
    bl_String name;
    bl_ShaderUniformType type;
    bl_NumberSpan defaultValue; /* Empty means source zero/default. */
    bool system;                /* Original bare system-name string; type inferred from name. */
} bl_ShaderUniformDecl;

typedef struct bl_ShaderSamplerDecl
{
    bl_String name; /* This scope supports ordinary filterable 2D color textures. */
} bl_ShaderSamplerDecl;

typedef struct bl_ShaderDefine
{
    bl_String name;
    bool booleanValue;
    bool isBoolean;
    double numberValue;
} bl_ShaderDefine;

typedef struct bl_ShaderMaterialOptions
{
    bl_String name;
    bl_String vertexSource;   /* Original wgsl tag is string identity. */
    bl_String fragmentSource; /* Original wgsl tag is string identity. */
    const bl_VertexSemantic* attributes;
    size_t attributeCount;
    const bl_ShaderUniformDecl* uniforms;
    size_t uniformCount;
    const bl_ShaderSamplerDecl* samplers;
    size_t samplerCount;
    const bl_ShaderDefine* defines;
    size_t defineCount;
    bl_OptionalBool needAlphaBlending;
    bl_OptionalBool needAlphaTesting;
    bl_OptionalBool backFaceCulling;
    bl_OptionalBool depthWrite;
    bl_BlendMode blendMode;
    bl_DepthCompare depthCompare;
    bl_Topology topology;
    bl_OptionalNumber depthBias;
    bl_OptionalNumber depthBiasSlopeScale;
} bl_ShaderMaterialOptions;

typedef struct bl_ShaderUniformView
{
    bl_ShaderUniformType type;
    bl_F32Span values; /* Stable live read-only backing, until material disposal. */
} bl_ShaderUniformView;

bl_Status bl_createShaderMaterial(bl_Runtime* runtime, const bl_ShaderMaterialOptions* options,
                                  bl_ShaderMaterial* material);
bl_Status bl_setShaderUniform(bl_ShaderMaterial material, bl_String name, bl_NumberSpan value);
bl_Status bl_setShaderUniformF32(bl_ShaderMaterial material, bl_String name, bl_F32Span value);
bl_Status bl_getShaderUniform(bl_ShaderMaterial material, bl_String name,
                              bl_ShaderUniformView* value);
bl_Status bl_setShaderFloat(bl_ShaderMaterial material, bl_String name, double value);
bl_Status bl_setShaderVector3(bl_ShaderMaterial material, bl_String name, bl_Vec3 value);
bl_Status bl_setShaderMatrix(bl_ShaderMaterial material, bl_String name, const bl_Mat4* value);
bl_Status bl_setShaderMatrixF32(bl_ShaderMaterial material, bl_String name, bl_F32Span value);
bl_Status bl_setShaderTexture(bl_ShaderMaterial material, bl_String name, bl_Texture2D texture);
bl_Status bl_getShaderTexture(bl_ShaderMaterial material, bl_String name, bl_Texture2D* texture);
bl_Status bl_disposeShaderMaterial(bl_ShaderMaterial material); /* Native GC equivalent. */

/*
 * Material creation is DATA-ONLY: no engine, GPU or compiler required. Compile
 * when registerScene (or runtime material addition/swap) needs a pipeline.
 * Original fixed entry names mainVertex/mainFragment apply to materials; the
 * injected compiler ABI itself accepts arbitrary entrypoints for other clients.
 * Duplicate attributes/names, missing position, invalid WGSL identifiers,
 * missing sources or unknown uniforms fail; no setter creates a hidden slot.
 * Attribute declaration order determines @location, canonical types are
 * position/normal=vec3, uv/uv2=vec2, tangent/color=vec4<f32>.
 *
 * Bare system names map system=true: world/view/projection/viewProjection/
 * worldView/worldViewProjection (mat4), cameraPosition(vec3), screenSize(vec2),
 * alphaCutoff(f32). Renderer fills these for each draw; alphaCutoff defaults .4.
 * Custom uniform types are exactly source f32/u32/i32/vec2/vec3/vec4/mat4.
 * Default/set values are rounded into stable Float32 backing EVEN for u32/i32,
 * matching original normalization; final UBO serialization converts integer
 * values as source JS Uint32/Int32 views do. Required element counts are
 * 1/2/3/4/16; default zero except alphaCutoff. setShaderFloat/Vector3 validate
 * element count like original wrappers, not a stricter new declaration type.
 * getShaderUniform represents a source scalar as values.count=1, otherwise
 * exposes the original stable live backing view. NEVER mutate returned storage.
 *
 * Core generates shaderSystem group1/binding0, shaderUniforms binding1 when
 * custom fields exist, then texture/sampler pairs in declaration order at
 * binding2 (or binding1 without custom fields). samplers.foo generates variables
 * foo and fooSampler. It computes original WGSL alignment (4/8/16; vec3 size12,
 * mat4 size64; blocks rounded to16), not a host JS algorithm.
 * Defines are unique identifiers, sorted by name, injected bool/f32 constants.
 *
 * Defaults: needAlphaBlending=false, needAlphaTesting=false, backFaceCulling=true,
 * depthWrite=!needAlphaBlending unless explicitly specified, blendMode=alpha,
 * depthCompare=greater-equal (REVERSED Z), depthBias/slope=0, triangle-list.
 * Src-over color factors are srcAlpha/oneMinusSrcAlpha, alpha factors
 * one/oneMinusSrcAlpha; additive uses srcAlpha/one for color, one/one for alpha.
 * WGSL fragment discard is preserved for cutout/highlight/cloud shaders.
 * Matrix upload is column-major F32; projection preserves the original
 * left-handed/reverse-Z clip convention. Backend clip conversion belongs to
 * compiler lowering, not replacement user shaders.
 *
 * Texture getters preserve handle identity; null clears a slot. Setting the
 * same value/texture is a no-op. A missing texture on an actively sampled slot
 * returns NOT_READY at preparation/draw, never silently substitutes white.
 * Source stencil/custom blend/storage/instancing/array/depth samplers and
 * transmission are outside this scoped contract; do not pretend these exist.
 * disposeShaderMaterial rejects a material referenced by live meshes (BUSY).
 */

/* ---------------- Original tightly packed RGBA8 pixel atlas --------------- */

typedef enum bl_AddressMode
{
    BL_ADDRESS_CLAMP_TO_EDGE = 0,
    BL_ADDRESS_REPEAT,
    BL_ADDRESS_MIRROR_REPEAT
} bl_AddressMode;

typedef enum bl_FilterMode
{
    BL_FILTER_NEAREST = 0,
    BL_FILTER_LINEAR
} bl_FilterMode;

typedef struct bl_PixelsTexture2DOptions
{
    bl_AddressMode addressModeU;
    bl_AddressMode addressModeV;
    bl_FilterMode minFilter;
    bl_FilterMode magFilter;
    bool srgb;
} bl_PixelsTexture2DOptions;

typedef struct bl_Texture2DInfo
{
    uint32_t width;
    uint32_t height;
    bool srgb;
} bl_Texture2DInfo;

bl_Status bl_createTexture2DFromPixels(bl_EngineContext engine, bl_Bytes pixels, uint32_t width,
                                       uint32_t height, const bl_PixelsTexture2DOptions* options,
                                       bl_Texture2D* texture);
bl_Status bl_updateTexture2DFromPixels(bl_EngineContext engine, bl_Texture2D texture,
                                       bl_Bytes pixels, uint32_t x, uint32_t y, uint32_t width,
                                       uint32_t height);
bl_Status bl_getTexture2DInfo(bl_Texture2D texture, bl_Texture2DInfo* info);
bl_Status bl_disposeTexture2D(bl_Texture2D texture); /* Native GC equivalent. */

/*
 * Pixels are width*height*4 RGBA8 bytes, row-major top-to-bottom, straight alpha,
 * no flip/premultiply. Extra trailing source bytes are allowed as original does.
 * Dimensions >=1; overflow/short buffer/out-of-range update is invalid.
 * Inputs copied before return. Defaults nearest/nearest, clamp/clamp, srgb=false,
 * no mipmaps. srgb=true performs real hardware sRGB-to-linear sampling. The
 * atlas uses srgb=true; its geometry UVs/half-texel inset are demo user code.
 * Texture update defaults at JS layer x=y=0,width=texture.width,height=texture.height;
 * native arguments are explicit. Do not report an unsupported sampler/format
 * successful. disposeTexture2D is BUSY while any live material slot refers to
 * it; clear/dispose materials first. GPU retirement still waits for submits.
 */

/* ---------------- Original audio engine and external input source --------- */

typedef struct bl_AudioEngineOptions
{
    bl_HostAudioContext audioContext; /* Null => create host context. */
    bl_OptionalNumber volume;
    bl_OptionalNumber parameterRampDuration;
    bl_OptionalNumber resumeOnPauseRetryInterval;
    bl_OptionalBool resumeOnInteraction;
    bl_OptionalBool resumeOnPause;
} bl_AudioEngineOptions;

typedef struct bl_SoundSourceOptions
{
    bl_String name;
    bl_OptionalNumber volume;
    bl_OptionalBool outBusAutoDefault; /* Default true; false leaves disconnected. */
} bl_SoundSourceOptions;

typedef enum bl_AudioRampShape
{
    BL_AUDIO_RAMP_LINEAR = 0,
    BL_AUDIO_RAMP_NONE,
    BL_AUDIO_RAMP_EXPONENTIAL,
    BL_AUDIO_RAMP_LOGARITHMIC
} bl_AudioRampShape;

typedef struct bl_RampOptions
{
    bl_OptionalNumber duration; /* Seconds, clamped to engine ramp duration. */
    bl_AudioRampShape shape;    /* Source default "linear". */
} bl_RampOptions;

typedef struct bl_AudioEngineInfo
{
    bl_HostAudioInfo context;
    double volume;
} bl_AudioEngineInfo;

bl_Status bl_createAudioEngineAsync(bl_Runtime* runtime, const bl_AudioEngineOptions* options,
                                    bl_AudioEngine* engine);
bl_Status bl_unlockAudioEngineAsync(bl_AudioEngine engine);
bl_Status bl_getAudioEngineInfo(bl_AudioEngine engine, bl_AudioEngineInfo* info);
bl_Status bl_getAudioContext(bl_AudioEngine engine, bl_HostAudioContext* context,
                             const bl_AudioService** service);
bl_Status bl_setMasterVolume(bl_AudioEngine engine, double value, const bl_RampOptions* options);
bl_Status bl_getMasterVolume(bl_AudioEngine engine, double* value);
bl_Status bl_createSoundSourceAsync(bl_AudioEngine engine, bl_HostAudioNode node,
                                    const bl_SoundSourceOptions* options,
                                    bl_AudioInputSource* source);
bl_Status bl_setSoundSourceVolume(bl_AudioInputSource source, double value,
                                  const bl_RampOptions* options);
bl_Status bl_disposeSoundSource(bl_AudioInputSource source);
bl_Status bl_disposeAudioEngine(bl_AudioEngine engine);
bl_Status bl_audioUserGesture(bl_AudioEngine engine);
bl_Status bl_pollAudioEngine(bl_AudioEngine engine, double monotonicTimeMs);

/*
 * Original createAudioEngineAsync()/unlockAudioEngineAsync()/createSoundSourceAsync
 * retain exact suffixes and their completed semantics. Context graph routing is
 * sourceNode -> source gain -> default main-bus gain -> master-out gain ->
 * host destination. Default gains=1; master volume=1; default ramp=.01 seconds
 * (negative parameterRampDuration clamps to0). Source name default empty,
 * volume=1. This scope exposes the default main bus, not arbitrary audio buses.
 * createSoundSource does NOT synthesize noise or force a context audible.
 * Master/source volume setters cancel scheduled parameter values from time0.
 * Shape none applies immediately; other durations clamp to the engine's ramp
 * duration and use immediate set below 1e-6 seconds. Lite computes the original
 * Float32 value curves: linear has 2 samples, exponential/logarithmic have 100
 * samples from audio-param.ts, and sends setParameterCurve to the host. The
 * exponential ramp shape is NOT WebAudio exponentialRampToValueAtTime.
 *
 * engine.audioContext/currentTime/state -> getAudioContext/getAudioEngineInfo.
 * The returned service/context is borrowed; native user code invokes actual
 * host createGain/createBuffer/createOscillator/createBiquadLowpass/connect/
 * connectParameter/setParameter/ramp/start/stop callbacks for the original
 * ambience/LFO/footstep/break/place algorithms. JSBinding exposes the equivalent
 * audioContext objects; graph tokens never masquerade as browser AudioNodes.
 *
 * resumeOnInteraction=true and resumeOnPause=true by default, retry=1000ms.
 * Hosts forward gestures through audioUserGesture and retries through
 * pollAudioEngine using a finite, nondecreasing monotonic wall-clock time in MS.
 * Retry timing MUST NOT use currentTime, which stops while audio is suspended.
 * Retry after a once-running context pauses, matching source resumeOnPause;
 * offline contexts need neither gesture nor retry. No DOM listeners in core.
 * unlock returns OK only for genuinely RUNNING/offline contexts, not merely
 * when resume callback returned success; suspended returns AUDIO_SUSPENDED.
 * No audio host returns AUDIO_UNAVAILABLE. Offline success is not audible:
 * clients must inspect offline/audibleOutputAvailable before claiming sound.
 * Real-time close follows original disposeAudioEngine even for supplied
 * context; an offline context is not closed. Source disposal disconnects its
 * input and releases its graph; it does not stop an unrelated external node.
 * Engine disposal disposes sources/buses once. User-code node/buffer ownership
 * remains with the host client; core retains only graph connections it creates.
 */

/* ---------------- Additional reviewed camera/light/material families ------- */

typedef struct bl_Camera
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_Camera;

typedef struct bl_ArcRotateCamera
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_ArcRotateCamera;

typedef struct bl_Material
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_Material;

typedef struct bl_StandardMaterial
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_StandardMaterial;

typedef struct bl_Light
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_Light;

typedef struct bl_HemisphericLight
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_HemisphericLight;

typedef struct bl_ArcRotateControl
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_ArcRotateControl;

typedef struct bl_CameraLimitToken
{
    bl_Runtime* _runtime;
    uint64_t _id;
} bl_CameraLimitToken;

bl_Status bl_freeCameraAsCamera(bl_FreeCamera source, bl_Camera* camera);
bl_Status bl_arcRotateCameraAsCamera(bl_ArcRotateCamera source, bl_Camera* camera);
bl_Status bl_cameraAsFreeCamera(bl_Camera source, bl_FreeCamera* camera);
bl_Status bl_cameraAsArcRotateCamera(bl_Camera source, bl_ArcRotateCamera* camera);
bl_Status bl_shaderMaterialAsMaterial(bl_ShaderMaterial source, bl_Material* material);
bl_Status bl_standardMaterialAsMaterial(bl_StandardMaterial source, bl_Material* material);
bl_Status bl_materialAsShaderMaterial(bl_Material source, bl_ShaderMaterial* material);
bl_Status bl_materialAsStandardMaterial(bl_Material source, bl_StandardMaterial* material);
bl_Status bl_hemisphericLightAsLight(bl_HemisphericLight source, bl_Light* light);
bl_Status bl_arcRotateCameraNode(bl_ArcRotateCamera camera, bl_SceneNode* node);
bl_Status bl_lightNode(bl_Light light, bl_SceneNode* node);

typedef struct bl_SceneProperties2
{
    bl_Color4 clearColor;
    bl_Camera camera;
    double fixedDeltaMs;
} bl_SceneProperties2;

typedef struct bl_MeshProperties2
{
    bl_Material material;
    bl_OptionalNumber renderOrder;
    bool receiveShadows;
} bl_MeshProperties2;

bl_Status bl_getSceneProperties2(bl_SceneContext scene, bl_SceneProperties2* properties);
bl_Status bl_setSceneProperties2(bl_SceneContext scene, const bl_SceneProperties2* properties);
bl_Status bl_getMeshProperties2(bl_Mesh mesh, bl_MeshProperties2* properties);
bl_Status bl_setMeshProperties2(bl_Mesh mesh, const bl_MeshProperties2* properties);

/*
 * Family conversions check concrete kind, preserve identity, and never cast a
 * new family through a legacy typed handle. Legacy getters return UNSUPPORTED
 * without writing outputs for unrepresentable new-family selections; legacy
 * setters still select FreeCamera/ShaderMaterial. Null is accepted only in
 * scene.camera/mesh.material; all other identity/error/ownership rules above apply.
 */
typedef struct bl_ArcRotateCameraProperties
{
    double alpha;
    double beta;
    double radius;
    bl_Vec3 target;
    double fov;
    double nearPlane;
    double farPlane;
    double inertia;
    double panningInertia;
    double angularSensibility;
    double panningSensibility;
    double wheelPrecision;
    double inertialAlphaOffset;
    double inertialBetaOffset;
    double inertialRadiusOffset;
    double inertialPanningX;
    double inertialPanningY;
} bl_ArcRotateCameraProperties;

bl_Status bl_createArcRotateCamera(bl_Runtime* runtime, double alpha, double beta, double radius,
                                   bl_Vec3 target, bl_ArcRotateCamera* camera);
bl_Status bl_getArcRotateCameraProperties(bl_ArcRotateCamera camera,
                                          bl_ArcRotateCameraProperties* properties);
bl_Status bl_setArcRotateCameraProperties(bl_ArcRotateCamera camera,
                                          const bl_ArcRotateCameraProperties* properties);

typedef struct bl_ArcRotateCameraLimits
{
    bl_OptionalNumber lowerAlphaLimit;
    bl_OptionalNumber upperAlphaLimit;
    bl_OptionalNumber lowerBetaLimit;
    bl_OptionalNumber upperBetaLimit;
    bl_OptionalNumber lowerRadiusLimit;
    bl_OptionalNumber upperRadiusLimit;
} bl_ArcRotateCameraLimits;

#define BL_LIMIT_LOWER_ALPHA UINT32_C(1)
#define BL_LIMIT_UPPER_ALPHA UINT32_C(2)
#define BL_LIMIT_LOWER_BETA UINT32_C(4)
#define BL_LIMIT_UPPER_BETA UINT32_C(8)
#define BL_LIMIT_LOWER_RADIUS UINT32_C(16)
#define BL_LIMIT_UPPER_RADIUS UINT32_C(32)

typedef struct bl_ArcRotateCameraLimitPatch
{
    uint32_t fields;
    bl_ArcRotateCameraLimits values;
} bl_ArcRotateCameraLimitPatch;

bl_Status bl_getArcRotateCameraLimits(bl_ArcRotateCamera camera, bl_ArcRotateCameraLimits* limits,
                                      bool* enforced);
bl_Status bl_setCameraLimits(bl_ArcRotateCamera camera, const bl_ArcRotateCameraLimitPatch* patch,
                             bl_CameraLimitToken* disposer);
bl_Status bl_removeCameraLimits(bl_CameraLimitToken disposer);
bl_Status bl_setArcRotateCameraLimitFields(bl_ArcRotateCamera camera,
                                           const bl_ArcRotateCameraLimitPatch* patch);

typedef enum bl_ArcRotatePointerAction
{
    BL_ARC_ACTION_DEFAULT = 0,
    BL_ARC_ACTION_ROTATE,
    BL_ARC_ACTION_PAN
} bl_ArcRotatePointerAction;

typedef enum bl_ArcRotateInputKind
{
    BL_ARC_POINTER_DOWN = 0,
    BL_ARC_POINTER_MOVE,
    BL_ARC_POINTER_UP,
    BL_ARC_WHEEL,
    BL_ARC_TOUCH_START,
    BL_ARC_TOUCH_MOVE,
    BL_ARC_TOUCH_END,
    BL_ARC_CONTEXT_MENU,
    BL_ARC_GESTURE
} bl_ArcRotateInputKind;

typedef enum bl_ArcRotatePointerType
{
    BL_ARC_POINTER_MOUSE = 0,
    BL_ARC_POINTER_PEN,
    BL_ARC_POINTER_TOUCH
} bl_ArcRotatePointerType;

typedef struct bl_ArcRotateTouch
{
    int64_t identifier;
    double clientX;
    double clientY;
} bl_ArcRotateTouch;

typedef struct bl_ArcRotateInput
{
    bl_ArcRotateInputKind kind;
    bl_ArcRotatePointerType pointerType;
    int64_t pointerId;
    uint32_t button;
    double clientX;
    double clientY;
    double deltaY;
    const bl_ArcRotateTouch* changedTouches;
    size_t changedTouchCount;
} bl_ArcRotateInput;

typedef bool (*bl_ArcRotatePointerPredicate)(void* userData, const bl_ArcRotateInput* input);
typedef bool (*bl_ArcRotateStatePredicate)(void* userData);

typedef struct bl_ArcRotateControlOptions
{
    bl_ArcRotatePointerAction primaryButton;
    bl_ArcRotatePointerAction secondaryButton;
    bool keyboard;
    bl_ArcRotatePointerPredicate shouldHandlePointerDown;
    bl_ArcRotateStatePredicate isExternalDragActive;
    bl_ArcRotateStatePredicate isExternalPickPending;
    void* userData;
} bl_ArcRotateControlOptions;

typedef struct bl_ArcRotateInputEffects
{
    bool preventDefault;
    bool capturePointer;
    bool releasePointer;
    int64_t pointerId;
} bl_ArcRotateInputEffects;

bl_Status bl_attachControl(bl_ArcRotateCamera camera, bl_SceneContext scene,
                           const bl_ArcRotateControlOptions* options, bl_ArcRotateControl* control);
bl_Status bl_processArcRotateInput(bl_ArcRotateControl control, const bl_ArcRotateInput* input,
                                   bl_ArcRotateInputEffects* effects);
bl_Status bl_detachControl(bl_ArcRotateControl control);
bl_Status bl_setArcRotateControlOptions(bl_ArcRotateControl control,
                                        const bl_ArcRotateControlOptions* options);

/*
 * setArcRotateControlOptions requires a non-NULL table (zero table means defaults),
 * copies it atomically, and preserves identity, touch/drag/coordinate/inertia state
 * and frame-hook order. Input/predicate dispatch is BUSY; ordinary before-render
 * callbacks may replace options safely. Old borrowed callbacks/userdata remain
 * valid until successful replacement; failure keeps the old table.
 * setArcRotateCameraLimitFields changes masked DATA only: no hook installation,
 * clamp, dirty matrix, inertia reset or disposer replacement. Changed scalar orbit
 * writes invoke an installed hook; equal, target, projection and offset-only writes
 * do not. setCameraLimits remains the explicit install-and-enforce operation.
 */

/*
 * All factories are data-only. Arc defaults: fov=.8, near=.1, far=1000,
 * inertia/panningInertia=.9, angularSensibility=1000, panningSensibility=50,
 * wheelPrecision=3, offsets=0. Bare finite beta/radius have no implicit clamps.
 * Orbit/target writes dirty world lazily; projection writes do not dirty children.
 * Converted Arc node position/rotation/scaling mutation is unsupported.
 * Limits: mask absent=keep, selected present=false=clear. Install self-clamping
 * immediately (radius/beta/alpha), zeroing matching inertia only outside a bound.
 * Disposer removes only its current hook, leaves bound fields, and is idempotent.
 * Contradictory bounds/nonfinite values fail atomically as a native safety rule.
 * Projection requires fov(0,pi), near>0, far>near; sensitivities must be positive.
 *
 * Host owns listeners/canvas/capture/preventDefault; Core owns gestures/inertia.
 * Client coordinates are source CSS pixels (host converts DPI), wheel deltaY
 * is raw, not a notch. Send pointer AND changed-touch streams in source order.
 * Default primary rotate/secondary pan fall back independently; touch fixed.
 * Control APPENDS its frame hook (onBeforeRender PREPENDS). Integration is once
 * per frame, not delta-scaled. Null scene means no inertia fallback, but pinch
 * still writes radius. Control pins camera; scene disposal removes frame hook,
 * not control/listeners. Caller explicitly detaches. Predicates borrow userdata
 * until detach, must not throw/re-enter. Nested input/detach during dispatch BUSY.
 * Keyboard=true, viewport/orthographic/LWR are unsupported in this slice.
 */

typedef struct bl_HemisphericLightOptions
{
    bool hasDirection;
    bl_Vec3 direction;
    bl_OptionalNumber intensity;
} bl_HemisphericLightOptions;

typedef struct bl_HemisphericLightProperties
{
    bl_Vec3 direction;
    double intensity;
    bl_Vec3 diffuseColor;
    bl_Vec3 specularColor;
    bl_Vec3 groundColor;
} bl_HemisphericLightProperties;

bl_Status bl_createHemisphericLight(bl_Runtime* runtime, const bl_HemisphericLightOptions* options,
                                    bl_HemisphericLight* light);
bl_Status bl_getHemisphericLightProperties(bl_HemisphericLight light,
                                           bl_HemisphericLightProperties* properties);
bl_Status bl_setHemisphericLightProperties(bl_HemisphericLight light,
                                           const bl_HemisphericLightProperties* properties);
bl_Status bl_setLightIntensity(bl_Light light, double intensity);
bl_Status bl_markLightUboDirty(bl_Light light);

typedef struct bl_StandardMaterialProperties
{
    bl_Vec3 diffuseColor;
    double alpha;
    bl_Vec3 specularColor;
    double specularPower;
    bl_Vec3 emissiveColor;
    bl_Vec3 ambientColor;
    bool backFaceCulling;
    bool disableLighting;
} bl_StandardMaterialProperties;

typedef struct bl_RebuildMaterialOptions
{
    bl_OptionalBool rebuildViews;
    bl_OptionalBool rebuildFrameGraph;
} bl_RebuildMaterialOptions;

bl_Status bl_createStandardMaterial(bl_Runtime* runtime, bl_StandardMaterial* material);
bl_Status bl_getStandardMaterialProperties(bl_StandardMaterial material,
                                           bl_StandardMaterialProperties* properties);
bl_Status bl_setStandardMaterialProperties(bl_StandardMaterial material,
                                           const bl_StandardMaterialProperties* properties);
bl_Status bl_markMaterialUboDirty(bl_Material material);
bl_Status bl_rebuildMaterial(bl_SceneContext scene, bl_Material material,
                             const bl_RebuildMaterialOptions* options);
bl_Status bl_rebuildSceneRenderables(bl_SceneContext scene);
bl_Status bl_disposeStandardMaterial(bl_StandardMaterial material);

/*
 * Hemi defaults: direction(0,1,0), intensity1, diffuse/specular(1,1,1), ground0,
 * ordinary node TRS. RGB uses Vec3 doubles. Directions automatically dirty light
 * data; plain scalar/color property writes require markLightUboDirty after build.
 * setLightIntensity is the original helper (finite validation/error134, no-op
 * for equal values), not an intensity clamp. Zero/non-unit directions preserve
 * original world-normalization. Scene packing cap16, duplicates/insertion order
 * retained; excess members exist but are not packed. Light remove is not dispose,
 * and visible does not suppress packing. Filters/setMaxLights/shadows unsupported.
 *
 * Standard defaults: diffuse/specular1, alpha1, power64, emissive/ambient0,
 * culling=true, disableLighting=false. Set properties copies DATA only; explicit
 * markMaterialUboDirty refreshes UBOs, rebuildMaterial changes feature/bucket
 * state. Standard order is PER MESH, not ShaderMaterial merged-group minimum.
 * Rebuild options: views=true (no-op without exposed views), frameGraph=false.
 * frameGraph=true is UNSUPPORTED before mutation. Standard colored geometry
 * must be rejected at preparation until its automatic RGB behavior is supported.
 * Texture/PBR/plugins/fog/shadow/material-view features are not exposed.
 * Runtime WGSL compilation/reflection remains required. Disposal is BUSY while
 * referenced; GPU storage waits for actual submitted work. Public precision,
 * finite numeric validation, thread/runtime/kind/generation rules above apply.
 */

/*
 * Supported demo mapping summary:
 * createEngine/createSceneContext/onBeforeRender/registerScene/startEngine;
 * createFreeCamera + position/target/nearPlane/farPlane + scene.camera;
 * createMeshFromData + all Float32 streams/Uint32 indices + material/renderOrder;
 * createBox(number|options), createSphereData(options), createTransformNode;
 * position/scaling/Euler/quaternion/parent/children/visible/worldMatrix properties;
 * addToScene/removeFromScene/setSubtreeVisible, runtime streaming/material swaps;
 * createShaderMaterial + original declarations/defaults, setShaderFloat,
 * setShaderVector3, setShaderTexture and all opaque/cutout/blend/additive variants;
 * createTexture2DFromPixels + nearest/sRGB/clamp RGBA atlas;
 * createAudioEngineAsync/createSoundSourceAsync/unlockAudioEngineAsync plus
 * native equivalents of every WebAudio primitive actually used by audio.ts.
 *
 * Additional scoped support: ArcRotate camera/pointer-touch controls and limits,
 * untextured Standard material and hemispheric lights. This does not establish
 * support for all Standard, camera, control or lighting features.
 * Deliberately not complete: glTF/Draco/meshopt, other lights/shadows, PBR, physics,
 * particles subsystem (the demo's cubes are ordinary user-code meshes), full
 * frame graph, postprocess, additional camera kinds, multi-surface APIs, XR,
 * compute/storage shaders, skeletons, GUI, DOM and browser event emulation.
 * demoAssetUrl imports decoder setters only for an uncalled helper; Minecraft
 * calls neither. Radius-6 voxel meshing/light/water/mobs/collision/save/HUD and
 * atlas image composition remain ORIGINAL DEMO USER CODE, not hidden core APIs.
 */

#ifdef __cplusplus
}
#endif

#endif /* BABYLON_LITE_H */
