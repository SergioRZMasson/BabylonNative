// Handwritten ports of the pinned original scene USER code, not engine bodies.
#include "Scene.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace LiteScenes
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;

        void Clear(bl_SceneContext scene, bl_Color4 color)
        {
            bl_SceneProperties2 p{};
            Check(bl_getSceneProperties2(scene, &p));
            p.clearColor = color;
            Check(bl_setSceneProperties2(scene, &p));
        }

        bl_SceneNode Node(bl_Mesh mesh)
        {
            bl_SceneNode node{};
            Check(bl_meshNode(mesh, &node));
            return node;
        }

        void Add(bl_SceneContext scene, bl_Mesh mesh)
        {
            Check(bl_addToScene(scene, Node(mesh)));
        }

        bl_ArcRotateCamera Camera(bl_Runtime* r, bl_SceneContext scene, double alpha, double beta,
                                  double radius, bl_Vec3 target = {}, double nearPlane = .1,
                                  double farPlane = 1000, bool control = false)
        {
            bl_ArcRotateCamera camera{};
            Check(bl_createArcRotateCamera(r, alpha, beta, radius, target, &camera));
            bl_ArcRotateCameraProperties p{};
            Check(bl_getArcRotateCameraProperties(camera, &p));
            p.nearPlane = nearPlane;
            p.farPlane = farPlane;
            Check(bl_setArcRotateCameraProperties(camera, &p));
            bl_SceneProperties2 properties{};
            Check(bl_getSceneProperties2(scene, &properties));
            Check(bl_arcRotateCameraAsCamera(camera, &properties.camera));
            Check(bl_setSceneProperties2(scene, &properties));
            if (control)
            {
                bl_ArcRotateControl value{};
                Check(bl_attachControl(camera, scene, nullptr, &value));
            }
            return camera;
        }

        bl_StandardMaterial Standard(bl_Runtime* r, bl_Vec3 diffuse, bl_Vec3 specular = {1, 1, 1},
                                     bl_Vec3 emissive = {}, double alpha = 1, bool unlit = false)
        {
            bl_StandardMaterial material{};
            Check(bl_createStandardMaterial(r, &material));
            bl_StandardMaterialProperties p{};
            Check(bl_getStandardMaterialProperties(material, &p));
            p.diffuseColor = diffuse;
            p.specularColor = specular;
            p.emissiveColor = emissive;
            p.alpha = alpha;
            p.disableLighting = unlit;
            Check(bl_setStandardMaterialProperties(material, &p));
            return material;
        }

        void Material(bl_Mesh mesh, bl_StandardMaterial material)
        {
            bl_MeshProperties2 p{};
            Check(bl_getMeshProperties2(mesh, &p));
            Check(bl_standardMaterialAsMaterial(material, &p.material));
            Check(bl_setMeshProperties2(mesh, &p));
        }

        void Shader(bl_Mesh mesh, bl_ShaderMaterial material)
        {
            bl_MeshProperties p{};
            Check(bl_getMeshProperties(mesh, &p));
            p.material = material;
            Check(bl_setMeshProperties(mesh, &p));
        }

        void Hemi(bl_Runtime* r, bl_SceneContext scene, double intensity)
        {
            bl_HemisphericLight light{};
            const bl_HemisphericLightOptions options{true, {0, 1, 0}, {true, intensity}};
            Check(bl_createHemisphericLight(r, &options, &light));
            bl_Light base{};
            bl_SceneNode node{};
            Check(bl_hemisphericLightAsLight(light, &base));
            Check(bl_lightNode(base, &node));
            Check(bl_addToScene(scene, node));
        }

        void Directional(bl_Runtime* r, bl_SceneContext scene, bl_Vec3 direction,
                         double intensity = 1)
        {
            bl_DirectionalLight light{};
            const bl_DirectionalLightOptions options{direction, {true, intensity}};
            Check(bl_createDirectionalLight(r, &options, &light));
            bl_Light base{};
            bl_SceneNode node{};
            Check(bl_directionalLightAsLight(light, &base));
            Check(bl_lightNode(base, &node));
            Check(bl_addToScene(scene, node));
        }

        bl_Mesh Box(bl_EngineContext engine, double size = 1)
        {
            bl_BoxOptions options{};
            options.size = {true, size};
            bl_Mesh mesh{};
            Check(bl_createBox(engine, &options, &mesh));
            return mesh;
        }

        void Translation(float* values, double x, double y, double z)
        {
            for (unsigned i = 0; i < 16; ++i)
            {
                values[i] = 0;
            }
            values[0] = values[5] = values[10] = values[15] = 1;
            values[12] = static_cast<float>(x);
            values[13] = static_cast<float>(y);
            values[14] = static_cast<float>(z);
        }

        void ColoredInstances(uint32_t id, bl_Runtime* r, bl_EngineContext engine,
                              bl_SceneContext scene)
        {
            Camera(r, scene, -Pi / 5, Pi / 3, id == 16 ? 200 : 40, {}, .1, 1000, id == 16);
            bl_Mesh mesh = Box(engine);
            if (id == 16)
            {
                Material(mesh, Standard(r, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, 1, true));
            }
            else
            {
                const bl_VertexSemantic attributes[] = {BL_ATTRIBUTE_POSITION};
                const bl_ShaderUniformDecl uniforms[] = {
                    {String("viewProjection"), BL_UNIFORM_MAT4, {}, true},
                    {String("world"), BL_UNIFORM_MAT4, {}, true}};
                bl_ShaderMaterialOptions options{};
                options.vertexSource = String(
                    "struct VertexOutput{@builtin(position) position:vec4<f32>,"
                    "@location(0) vColor:vec4<f32>,};"
                    "@vertex fn mainVertex(input:VertexInput)->VertexOutput{var out:VertexOutput;"
                    "let iw=mat4x4<f32>(input.world0,input.world1,input.world2,input.world3);"
                    "out.position=shaderSystem.viewProjection*(shaderSystem.world*iw)*"
                    "vec4<f32>(input.position,1.0);out.vColor=input.instanceColor;return out;}");
                options.fragmentSource =
                    String("struct VertexOutput{@builtin(position) position:vec4<f32>,"
                           "@location(0) vColor:vec4<f32>,};"
                           "@fragment fn mainFragment(input:VertexOutput)->@location(0) vec4<f32>"
                           "{return input.vColor;}");
                options.attributes = attributes;
                options.attributeCount = 1;
                options.uniforms = uniforms;
                options.uniformCount = 2;
                bl_ShaderMaterial shader{};
                Check(bl_createShaderMaterial(r, &options, &shader));
                Shader(mesh, shader);
            }
            const unsigned side = id == 16 ? 40 : 8;
            const size_t count = side * side * side;
            const double spacing = id == 16 ? 100.0 / (side - 1) : 2;
            const double origin = id == 16 ? -50 : -7;
            std::vector<float> matrices(count * 16);
            std::vector<float> colors(count * 4);
            size_t index = 0;
            double col = 0;
            for (unsigned x = 0; x < side; ++x)
            {
                for (unsigned y = 0; y < side; ++y)
                {
                    for (unsigned z = 0; z < side; ++z, ++index)
                    {
                        Translation(matrices.data() + index * 16, origin + spacing * x,
                                    origin + spacing * y, origin + spacing * z);
                        uint32_t color =
                            static_cast<uint32_t>(id == 16 ? col : (index * 16777215.0) / count);
                        colors[index * 4] = static_cast<float>(((color >> 16) & 255) / 255.0);
                        colors[index * 4 + 1] = static_cast<float>(((color >> 8) & 255) / 255.0);
                        colors[index * 4 + 2] = static_cast<float>((color & 255) / 255.0);
                        colors[index * 4 + 3] = 1;
                        col += 16777215.0 / count;
                    }
                }
            }
            Check(bl_setThinInstances(mesh, {matrices.data(), matrices.size()}, count));
            Check(bl_setThinInstanceColors(mesh, {colors.data(), colors.size()}));
            Add(scene, mesh);
        }

        void FloatingInstances(bl_Runtime* r, bl_EngineContext engine, bl_SceneContext scene)
        {
            constexpr double Offset = 5000000;
            Clear(scene, {.05, .05, .08, 1});
            Camera(r, scene, Pi / 4, Pi / 3, 22, {Offset, 1, Offset}, .5, 500);
            Hemi(r, scene, .4);
            bl_DirectionalLight light{};
            const bl_DirectionalLightOptions options{{-.4, -1, -.2}, {}};
            Check(bl_createDirectionalLight(r, &options, &light));
            bl_DirectionalLightProperties lp{};
            Check(bl_getDirectionalLightProperties(light, &lp));
            lp.specular = {.3, .3, .3};
            Check(bl_setDirectionalLightProperties(light, &lp));
            bl_Light base{};
            bl_SceneNode node{};
            Check(bl_directionalLightAsLight(light, &base));
            Check(bl_lightNode(base, &node));
            Check(bl_addToScene(scene, node));
            bl_GroundOptions groundOptions{};
            groundOptions.width = {true, 60};
            groundOptions.height = {true, 60};
            bl_Mesh ground{};
            Check(bl_createGround(engine, &groundOptions, &ground));
            Material(ground, Standard(r, {.25, .25, .3}));
            Check(bl_setNodePosition(Node(ground), {Offset, 0, Offset}));
            Add(scene, ground);
            bl_Mesh mesh = Box(engine);
            Material(mesh, Standard(r, {1, 1, 1}, {.4, .4, .4}));
            Check(bl_setNodePosition(Node(mesh), {Offset, 1, Offset}));
            std::array<float, 25 * 16> matrices{};
            std::array<float, 25 * 4> colors{};
            size_t index = 0;
            for (int i = 0; i < 5; ++i)
            {
                for (int j = 0; j < 5; ++j, ++index)
                {
                    Translation(matrices.data() + index * 16, (i - 2) * 4, ((i + j) % 3) * .75,
                                (j - 2) * 4);
                    colors[index * 4] = static_cast<float>(.3 + i / 4.0 * .6);
                    colors[index * 4 + 1] = .4f;
                    colors[index * 4 + 2] = static_cast<float>(.3 + j / 4.0 * .6);
                    colors[index * 4 + 3] = 1;
                }
            }
            Check(bl_setThinInstances(mesh, {matrices.data(), matrices.size()}, 25));
            Check(bl_setThinInstanceColors(mesh, {colors.data(), colors.size()}));
            Add(scene, mesh);
        }

        void Grid(bl_Runtime* r, bl_EngineContext engine, bl_SceneContext scene)
        {
            Clear(scene, {.08, .08, .11, 1});
            Camera(r, scene, -Pi / 2.3, Pi / 3, 16, {0, 1.2, 0}, .1, 200, true);
            const bl_Vec3 main[] = {
                {.06, .07, .1}, {.1, .05, .05}, {.05, .08, .12}, {.08, .05, .1}};
            const bl_Vec3 line[] = {{0, .5, .5}, {1, .55, .1}, {.2, .9, 1}, {.9, .2, .6}};
            const bl_Vec3 positions[] = {{}, {-3.6, 1.6, 0}, {3.6, 1.2, 0}, {0, .8, 3.4}};
            const double ratio[] = {1, .5, .5, .3};
            const double major[] = {10, 5, 4, 3};
            const double minor[] = {.45, .5, .4, .6};
            const char* names[] = {"groundGrid", "sphereGrid", "boxGrid", "hardGrid"};
            for (unsigned i = 0; i < 4; ++i)
            {
                bl_Mesh mesh{};
                if (i == 0)
                {
                    bl_GroundOptions options{};
                    options.width = options.height = {true, 14};
                    Check(bl_createGround(engine, &options, &mesh));
                }
                else if (i == 1)
                {
                    bl_SphereOptions options{};
                    options.segments = {true, 48};
                    options.diameter = {true, 3};
                    Check(bl_createSphere(engine, &options, &mesh));
                }
                else
                {
                    mesh = Box(engine, i == 2 ? 2.4 : 1.6);
                }
                bl_GridMaterialOptions options{};
                options.name = String(names[i]);
                options.hasMainColor = options.hasLineColor = true;
                options.mainColor = main[i];
                options.lineColor = line[i];
                options.gridRatio = {true, ratio[i]};
                options.majorUnitFrequency = {true, major[i]};
                options.minorUnitVisibility = {true, minor[i]};
                options.antialias = i == 3 ? BL_BOOL_FALSE : BL_BOOL_TRUE;
                options.useMaxLine = i == 1 ? BL_BOOL_TRUE : BL_BOOL_DEFAULT;
                options.opacity = {true, i == 2 ? .6 : 1};
                bl_ShaderMaterial material{};
                Check(bl_createGridMaterial(r, &options, &material));
                Shader(mesh, material);
                Check(bl_setNodePosition(Node(mesh), positions[i]));
                Add(scene, mesh);
            }
        }

        void Ortho(bl_Runtime* r, bl_EngineContext engine, bl_SceneContext scene)
        {
            Clear(scene, {.06, .07, .1, 1});
            auto camera = Camera(r, scene, -Pi / 2 + .4, Pi / 3, 30, {}, .1, 100);
            bl_Camera base{};
            Check(bl_arcRotateCameraAsCamera(camera, &base));
            const bl_OrthographicBounds bounds{6, {}, {}, {}, {}};
            Check(bl_enableOrthographicCamera(base, &bounds));
            Hemi(r, scene, 1);
            const double depths[] = {-7, -3.5, 0, 3.5, 7};
            const bl_Vec3 colors[] = {
                {.85, .25, .25}, {.9, .6, .2}, {.35, .75, .4}, {.25, .55, .9}, {.65, .35, .85}};
            for (double x : {-4.0, 4.0})
            {
                for (unsigned i = 0; i < 5; ++i)
                {
                    bl_Mesh mesh = Box(engine, 2);
                    Check(bl_setNodePosition(Node(mesh), {x, 0, depths[i]}));
                    Material(mesh, Standard(r, colors[i], {}));
                    Add(scene, mesh);
                }
            }
        }

        void Require(bl_Status status) noexcept
        {
            if (status != BL_OK)
            {
                std::fprintf(stderr, "Scene callback C99 error %u\n",
                             static_cast<unsigned>(status));
                std::abort();
            }
        }

        struct MirrorState
        {
            bl_SceneNode runtime;
            bl_SceneNode parent;
            unsigned frame{};
        };

        void Mirrored(bl_Runtime* r, bl_EngineContext engine, bl_SceneContext scene)
        {
            Clear(scene, {.15, .16, .22, 1});
            Camera(r, scene, -Pi / 2 + .5, Pi / 3, 26, {}, .1, 200, true);
            Hemi(r, scene, .7);
            Directional(r, scene, {-.4, -1, .6}, .8);
            auto material = Standard(r, {.35, .6, .9}, {});
            bl_SceneNode nodes[4]{};
            const double x[] = {-9, -3, 3, 0};
            for (unsigned i = 0; i < 4; ++i)
            {
                bl_Mesh mesh = Box(engine, 4);
                Material(mesh, material);
                nodes[i] = Node(mesh);
                Check(bl_setNodePosition(nodes[i], {x[i], 0, 0}));
                Add(scene, mesh);
            }
            Check(bl_setNodeScaling(nodes[1], {-1, 1, 1}));
            bl_TransformNode parent{};
            Check(bl_createTransformNode(r, String("mirrorParent"), nullptr, &parent));
            Check(bl_setNodePosition(nodes[3], {-9, 0, 0}));
            Check(bl_setNodeParent(nodes[3], parent));
            Check(bl_appendNodeChild(parent, nodes[3]));
            Check(bl_enableMirroredMeshes(scene));
            auto* state = new MirrorState{nodes[2], parent};
            bl_CallbackToken before{};
            bl_Status status = bl_onBeforeRender(
                scene,
                [](void* user, double) {
                    auto& s = *static_cast<MirrorState*>(user);
                    if (++s.frame == 1)
                    {
                        Require(bl_setNodeScaling(s.runtime, {-1, 1, 1}));
                        Require(bl_setNodeScaling(s.parent, {-1, 1, 1}));
                    }
                },
                state, &before);
            if (status != BL_OK)
            {
                delete state;
                Check(status);
            }
            bl_CallbackToken dispose{};
            status = bl_onSceneDispose(
                scene, [](void* user) { delete static_cast<MirrorState*>(user); }, state, &dispose);
            if (status != BL_OK)
            {
                Check(bl_removeSceneCallback(scene, before));
                delete state;
                Check(status);
            }
        }

        void Coverage(bl_Runtime* r, bl_EngineContext engine, bl_SceneContext scene)
        {
            Clear(scene, {.035, .045, .07, 1});
            const char* vertex =
                "struct VertexOutput{@builtin(position) position:vec4<f32>,};"
                "@vertex fn mainVertex(input:VertexInput)->VertexOutput{"
                "let c=cos(shaderUniforms.angle);let s=sin(shaderUniforms.angle);"
                "let local=input.position.xy*1.65;"
                "let rotated=vec2<f32>(local.x*c-local.y*s,local.x*s+local.y*c);"
                "let world=shaderUniforms.center+rotated;var out:VertexOutput;"
                "out.position=vec4<f32>(world.x/3.3,world.y/2.2,shaderUniforms.depth,1.0);return out;}";
            const char* fragment =
                "@fragment fn mainFragment()->@location(0) vec4<f32>{"
                "return vec4<f32>(shaderUniforms.color,shaderUniforms.opacity);}";
            const bl_VertexSemantic attributes[] = {BL_ATTRIBUTE_POSITION};
            const bl_ShaderUniformDecl uniforms[] = {
                {String("center"), BL_UNIFORM_VEC2, {}, false},
                {String("angle"), BL_UNIFORM_F32, {}, false},
                {String("depth"), BL_UNIFORM_F32, {}, false},
                {String("color"), BL_UNIFORM_VEC3, {}, false},
                {String("opacity"), BL_UNIFORM_F32, {}, false}};
            for (unsigned panel = 0; panel < 2; ++panel)
            {
                for (unsigned row = 0; row < 2; ++row)
                {
                    for (unsigned card = 0; card < 2; ++card)
                    {
                        bl_ShaderMaterialOptions options{};
                        options.name = String("a2c-card");
                        options.vertexSource = String(vertex);
                        options.fragmentSource = String(fragment);
                        options.attributes = attributes;
                        options.attributeCount = 1;
                        options.uniforms = uniforms;
                        options.uniformCount = 5;
                        options.backFaceCulling = BL_BOOL_FALSE;
                        options.depthWrite = BL_BOOL_TRUE;
                        bl_ShaderMaterial material{};
                        Check(bl_createShaderMaterial(r, &options, &material));
                        const double center[] = {(panel ? 1.65 : -1.65) + (card ? .08 : -.08),
                                                 (row ? -1.05 : 1.05) + (card ? .04 : -.04)};
                        const double angles[2][2] = {{-.08, .1}, {.1, -.07}};
                        const bool front = card == row;
                        Check(bl_setShaderUniform(material, String("center"), {center, 2}));
                        Check(bl_setShaderFloat(material, String("angle"), angles[row][card]));
                        Check(bl_setShaderFloat(material, String("depth"), front ? .6 : .4));
                        Check(bl_setShaderVector3(
                            material, String("color"),
                            card ? bl_Vec3{26.0 / 255, 217.0 / 255, 83.0 / 255}
                                 : bl_Vec3{242.0 / 255, 31.0 / 255, 41.0 / 255}));
                        Check(bl_setShaderFloat(material, String("opacity"), front ? .5 : 1));
                        if (panel)
                        {
                            bl_Material base{};
                            Check(bl_shaderMaterialAsMaterial(material, &base));
                            Check(bl_setAlphaToCoverage(base, true));
                        }
                        bl_Mesh mesh{};
                        Check(bl_createPlane(engine, nullptr, &mesh));
                        Shader(mesh, material);
                        Add(scene, mesh);
                    }
                }
            }
            Camera(r, scene, 0, Pi / 2, 1);
        }

        void Lines(uint32_t id, bl_Runtime* r, bl_EngineContext engine, bl_SceneContext scene)
        {
            if (id == 279)
            {
                Clear(scene, {.02, .025, .05, 1});
                const bl_Vec3 initial[] = {
                    {-1, -.6, 0}, {-.35, .75, 0}, {.35, -.05, 0}, {1, .65, 0}};
                const bl_Vec3 updated[] = {
                    {-1.1, -.8, 0}, {-.25, .95, 0}, {.25, -.15, 0}, {1.1, .8, 0}};
                const bl_Vec3Span initialPath{initial, 4};
                bl_LineMaterialOptions options{};
                options.useVertexAlpha = options.useThinInstances = options.useThinInstanceColors =
                    BL_BOOL_TRUE;
                bl_ShaderMaterial material{};
                Check(bl_createLineMaterial(r, &options, &material));
                bl_LineSystemOptions system{};
                system.name = String("updated-instanced-lines");
                system.lines = {&initialPath, 1};
                system.material = material;
                bl_Mesh mesh{};
                Check(bl_createLineSystem(engine, &system, &mesh));
                const bl_Vec3Span updatedPath{updated, 4};
                const bl_LineSystemUpdateOptions update{{&updatedPath, 1}, false, {}};
                Check(bl_updateLineSystem(engine, mesh, &update));
                const bl_Vec3 positions[] = {
                    {-3.6, 1.5, 0}, {0, 1.5, 0}, {3.6, 1.5, 0}, {-1.8, -1.7, 0}, {1.8, -1.7, 0}};
                float matrices[80]{};
                for (unsigned i = 0; i < 5; ++i)
                {
                    Translation(matrices + i * 16, positions[i].x, positions[i].y, positions[i].z);
                }
                const float colors[] = {1, .2f,  .2f, .9f,  .2f,  1,    .35f, .75f, .2f, .55f,
                                        1, .65f, 1,   .75f, .15f, .55f, .85f, .25f, 1,   .45f};
                Check(bl_setThinInstances(mesh, {matrices, 80}, 5));
                Check(bl_setThinInstanceColors(mesh, {colors, 20}));
                Add(scene, mesh);
            }
            else
            {
                Clear(scene, {.025, .035, .065, 1});
                const bl_Vec3 a[] = {{-4.6, -2.1, 0},
                                     {-4.6, 2.1, 0},
                                     {-1.4, 2.1, 0},
                                     {-1.4, -2.1, 0},
                                     {-4.6, -2.1, 0}};
                const bl_Vec3 b[] = {{-4.2, -1.6, 0}, {-1.8, 1.6, 0}};
                const bl_Vec3 c[] = {{-4.2, 1.6, 0}, {-1.8, -1.6, 0}};
                const bl_Vec3Span paths[] = {{a, 5}, {b, 2}, {c, 2}};
                bl_LineSystemOptions options{};
                options.name = String("uniform-lines");
                options.lines = {paths, 3};
                options.hasColor = true;
                options.color = {.25, .85, 1, 1};
                options.useVertexAlpha = BL_BOOL_FALSE;
                bl_Mesh mesh{};
                Check(bl_createLineSystem(engine, &options, &mesh));
                Add(scene, mesh);
                const bl_Vec3 d[] = {{1.4, -2.1, 0}, {3, 2.2, 0},  {4.6, -2.1, 0},
                                     {1.4, .5, 0},   {4.6, .5, 0}, {1.4, -2.1, 0}};
                const bl_Vec3 e[] = {{1.6, -1.5, 0}, {4.4, 1.6, 0}};
                const bl_Vec3Span coloredPaths[] = {{d, 6}, {e, 2}};
                const bl_Color4 rgba[] = {{1, .2, .15, .9},    {1, .85, .1, .75},
                                          {.15, .9, .35, .65}, {.1, .75, 1, .55},
                                          {.65, .25, 1, .45},  {1, .2, .15, .9}};
                const bl_Color4 rgba2[] = {{.2, .9, 1, .35}, {1, .25, .75, .8}};
                const bl_Color4Span colorPaths[] = {{rgba, 6}, {rgba2, 2}};
                options = {};
                options.name = String("vertex-color-lines");
                options.lines = {coloredPaths, 2};
                options.hasColors = true;
                options.colors = {colorPaths, 2};
                options.useVertexAlpha = BL_BOOL_TRUE;
                Check(bl_createLineSystem(engine, &options, &mesh));
                Add(scene, mesh);
            }
            Camera(r, scene, -Pi / 2, Pi / 2, 12);
        }

        struct Proximity
        {
            bl_SceneNode ball;
            bl_SceneNode radius;
            bl_SceneNode highlight;
            std::array<bl_Vec3, 100> blocks{};
            unsigned frame{};
        };

        double Rand(uint32_t& seed)
        {
            seed = seed * 1664525u + 1013904223u;
            return seed / 4294967296.0;
        }

        void ProximityScene(bl_Runtime* r, bl_EngineContext engine, bl_SceneContext scene)
        {
            Camera(r, scene, Pi / 3, .85, 270, {}, .1, 1000, true);
            Hemi(r, scene, .7);
            bl_SphereOptions sphere{};
            sphere.diameter = {true, 2};
            sphere.segments = {true, 32};
            bl_Mesh ball{};
            Check(bl_createSphere(engine, &sphere, &ball));
            Material(ball, Standard(r, {1, 1, 1}));
            Add(scene, ball);
            sphere.diameter.value = 20;
            bl_Mesh radius{};
            Check(bl_createSphere(engine, &sphere, &radius));
            Material(radius, Standard(r, {0, 1, 0}, {1, 1, 1}, {}, .5));
            Add(scene, radius);
            std::array<bl_Vec3, 97> path{};
            for (unsigned i = 0; i <= 96; ++i)
            {
                double angle = (i / 96.0) * Pi * 2;
                path[i] = {std::cos(angle) * 50, .5, std::sin(angle) * 50};
            }
            bl_TubeOptions tube{};
            tube.path = {path.data(), path.size()};
            tube.radius = {true, .35};
            tube.tessellation = {true, 8};
            bl_Mesh pathMesh{};
            Check(bl_createTube(engine, &tube, &pathMesh));
            Material(pathMesh, Standard(r, {.1, .35, .5}, {1, 1, 1}, {.35, .75, 1}));
            Add(scene, pathMesh);
            bl_GroundOptions groundOptions{};
            groundOptions.width = groundOptions.height = {true, 200};
            bl_Mesh ground{};
            Check(bl_createGround(engine, &groundOptions, &ground));
            Material(ground, Standard(r, {.45, .45, .45}));
            Add(scene, ground);
            bl_CylinderOptions cylinder{};
            cylinder.diameter = cylinder.height = {true, 4};
            cylinder.subdivisions = {true, 10};
            cylinder.tessellation = {true, 48};
            bl_Mesh base{};
            Check(bl_createCylinder(engine, &cylinder, &base));
            Material(base, Standard(r, {1, 1, 1}));
            auto* state = new Proximity{Node(ball), Node(radius), {}};
            std::array<float, 1600> matrices{};
            std::array<float, 400> colors{};
            uint32_t seed = 0x424321;
            for (unsigned i = 0; i < 100; ++i)
            {
                double x = (Rand(seed) - .5) * 200;
                double z = (Rand(seed) - .5) * 200;
                state->blocks[i] = {x, 2, z};
                Translation(matrices.data() + i * 16, x, 2, z);
                colors[i * 4] = static_cast<float>(Rand(seed));
                colors[i * 4 + 1] = static_cast<float>(Rand(seed));
                colors[i * 4 + 2] = static_cast<float>(Rand(seed));
                colors[i * 4 + 3] = 1;
            }
            Check(bl_setThinInstances(base, {matrices.data(), matrices.size()}, 100));
            Check(bl_setThinInstanceColors(base, {colors.data(), colors.size()}));
            Add(scene, base);
            cylinder = {};
            cylinder.diameter = cylinder.height = {true, 6};
            cylinder.tessellation = {true, 48};
            bl_Mesh highlight{};
            Check(bl_createCylinder(engine, &cylinder, &highlight));
            Material(highlight, Standard(r, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}));
            Add(scene, highlight);
            state->highlight = Node(highlight);
            bl_CallbackToken before{};
            bl_Status status = bl_onBeforeRender(
                scene,
                [](void* user, double) {
                    auto& s = *static_cast<Proximity*>(user);
                    const double angle = ((s.frame % 480) / 480.0) * Pi * 2;
                    const bl_Vec3 p{std::cos(angle) * 50, .5, std::sin(angle) * 50};
                    Require(bl_setNodePosition(s.ball, p));
                    Require(bl_setNodePosition(s.radius, p));
                    const bl_Vec3* nearest = nullptr;
                    double best = INFINITY;
                    for (const auto& block : s.blocks)
                    {
                        const double dx = block.x - p.x;
                        const double dy = block.y - 2;
                        const double dz = block.z - p.z;
                        const double distance = dx * dx + dy * dy + dz * dz;
                        if (distance < best && distance <= 100)
                        {
                            best = distance;
                            nearest = &block;
                        }
                    }
                    Require(bl_setNodeVisible(s.highlight, nearest != nullptr));
                    if (nearest)
                    {
                        Require(bl_setNodePosition(s.highlight, *nearest));
                    }
                    ++s.frame;
                },
                state, &before);
            if (status != BL_OK)
            {
                delete state;
                Check(status);
            }
            bl_CallbackToken dispose{};
            status = bl_onSceneDispose(
                scene, [](void* user) { delete static_cast<Proximity*>(user); }, state, &dispose);
            if (status != BL_OK)
            {
                Check(bl_removeSceneCallback(scene, before));
                delete state;
                Check(status);
            }
        }
    }

    bool CreateM1Scene(uint32_t id, bl_Runtime* r, bl_EngineContext engine, bl_SceneContext scene)
    {
        switch (id)
        {
            case 16:
            case 165:
                ColoredInstances(id, r, engine, scene);
                return true;
            case 43:
                ProximityScene(r, engine, scene);
                return true;
            case 204:
                FloatingInstances(r, engine, scene);
                return true;
            case 213:
                Grid(r, engine, scene);
                return true;
            case 268:
                Ortho(r, engine, scene);
                return true;
            case 270:
                Mirrored(r, engine, scene);
                return true;
            case 274:
                Coverage(r, engine, scene);
                return true;
            case 278:
            case 279:
                Lines(id, r, engine, scene);
                return true;
            default:
                return false;
        }
    }
}
