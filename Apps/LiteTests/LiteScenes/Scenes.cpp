#include "Scene.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace LiteScenes
{
    bl_String String(const char* text)
    {
        return {text, std::strlen(text)};
    }

    namespace
    {
        constexpr double Pi = 3.14159265358979323846;

        void Camera(bl_Runtime* runtime, bl_SceneContext scene, double alpha, double beta,
                    double radius, bl_Vec3 target, double nearPlane, double farPlane, bool controls)
        {
            bl_ArcRotateCamera camera{};
            Check(bl_createArcRotateCamera(runtime, alpha, beta, radius, target, &camera));
            bl_ArcRotateCameraProperties properties{};
            Check(bl_getArcRotateCameraProperties(camera, &properties));
            properties.nearPlane = nearPlane;
            properties.farPlane = farPlane;
            Check(bl_setArcRotateCameraProperties(camera, &properties));
            bl_SceneProperties2 state{};
            Check(bl_getSceneProperties2(scene, &state));
            Check(bl_arcRotateCameraAsCamera(camera, &state.camera));
            Check(bl_setSceneProperties2(scene, &state));
            if (controls)
            {
                bl_ArcRotateControl control{};
                Check(bl_attachControl(camera, scene, nullptr, &control));
            }
        }

        void Clear(bl_SceneContext scene, bl_Color4 color)
        {
            bl_SceneProperties2 state{};
            Check(bl_getSceneProperties2(scene, &state));
            state.clearColor = color;
            Check(bl_setSceneProperties2(scene, &state));
        }

        void Environment(bl_SceneContext scene, bool coreUrl)
        {
            bl_EnvironmentOptions options{};
            options.brdfUrl = String("/brdf-lut.png");
            options.skipSkybox = BL_BOOL_TRUE;
            options.skipGround = BL_BOOL_TRUE;
            bl_EnvironmentTextures environment{};
            Check(bl_loadEnvironment(
                scene,
                String(
                    coreUrl
                        ? "https://assets.babylonjs.com/core/environments/environmentSpecular.env"
                        : "https://assets.babylonjs.com/environments/environmentSpecular.env"),
                &options, &environment));
        }

        void Model(bl_EngineContext engine, bl_SceneContext scene, const char* url)
        {
            bl_AssetSource source{};
            source.kind = BL_ASSET_SOURCE_URI;
            source.uri = String(url);
            bl_AssetContainer container{};
            Check(bl_loadGltf(engine, &source, &container));
            Check(bl_addAssetContainerToScene(scene, container));
        }

        void CustomShader(uint32_t id, bl_Runtime* runtime, bl_EngineContext engine,
                          bl_SceneContext scene)
        {
            const char* vertex =
                "struct VertexOutput{@builtin(position) position:vec4<f32>,};\n"
                "@vertex fn mainVertex(input:VertexInput)->VertexOutput{var out:VertexOutput;"
                "out.position=shaderSystem.worldViewProjection*vec4<f32>(input.position,1.0);"
                "return out;}";
            const char* fragment =
                "struct VertexOutput{@builtin(position) position:vec4<f32>,};\n"
                "@fragment fn mainFragment(input:VertexOutput)->@location(0) vec4<f32>"
                "{return vec4<f32>(25.0/255.0,0.70,1.00,1.0);}";
            if (id == 161)
            {
                fragment = "struct VertexOutput{@builtin(position) position:vec4<f32>,};\n"
                           "@fragment fn mainFragment(input:VertexOutput)->@location(0) vec4<f32>"
                           "{return vec4<f32>(shaderUniforms.tint*shaderUniforms.intensity,1.0);}";
            }
            if (id == 163)
            {
                vertex =
                    "struct VertexOutput{@builtin(position) position:vec4<f32>,"
                    "@location(0) uv:vec2<f32>,};\n"
                    "@vertex fn mainVertex(input:VertexInput)->VertexOutput{var out:VertexOutput;"
                    "out.position=shaderSystem.worldViewProjection*vec4<f32>(input.position,1.0);"
                    "out.uv=input.uv;return out;}";
                fragment = "struct VertexOutput{@builtin(position) position:vec4<f32>,"
                           "@location(0) uv:vec2<f32>,};\n"
                           "@fragment fn mainFragment(input:VertexOutput)->@location(0) vec4<f32>"
                           "{if(distance(input.uv,vec2<f32>(0.5,0.5))<0.18){discard;}"
                           "return vec4<f32>(1.0,0.25,0.05,0.55);}";
            }
            const bl_VertexSemantic attributes[] = {BL_ATTRIBUTE_POSITION, BL_ATTRIBUTE_UV};
            const double white[] = {1, 1, 1};
            const double one[] = {1};
            const bl_ShaderUniformDecl uniforms[] = {
                {String("worldViewProjection"), BL_UNIFORM_MAT4, {}, true},
                {String("tint"), BL_UNIFORM_VEC3, {white, 3}, false},
                {String("intensity"), BL_UNIFORM_F32, {one, 1}, false}};
            bl_ShaderMaterialOptions options{};
            const char* name = id == 159   ? "scene159Shader"
                               : id == 161 ? "scene161Shader"
                                           : "scene163Shader";
            options.name = String(name);
            options.vertexSource = String(vertex);
            options.fragmentSource = String(fragment);
            options.attributes = attributes;
            options.attributeCount = id == 163 ? 2 : 1;
            options.uniforms = uniforms;
            options.uniformCount = id == 161 ? 3 : 1;
            if (id == 163)
            {
                options.needAlphaBlending = BL_BOOL_TRUE;
                options.needAlphaTesting = BL_BOOL_TRUE;
                options.backFaceCulling = BL_BOOL_FALSE;
            }
            bl_ShaderMaterial material{};
            Check(bl_createShaderMaterial(runtime, &options, &material));
            if (id == 161)
            {
                const double tint[] = {1, 0.35, 0.1};
                const double intensity[] = {0.85};
                Check(bl_setShaderUniform(material, String("tint"), {tint, 3}));
                Check(bl_setShaderUniform(material, String("intensity"), {intensity, 1}));
            }
            bl_Mesh mesh{};
            if (id == 163)
            {
                bl_PlaneOptions plane{};
                plane.width = {true, 3};
                plane.height = {true, 3};
                Check(bl_createPlane(engine, &plane, &mesh));
            }
            else
            {
                bl_SphereOptions sphere{};
                sphere.segments = {true, 32};
                sphere.diameter = {true, 2};
                Check(bl_createSphere(engine, &sphere, &mesh));
            }
            bl_MeshProperties properties{};
            Check(bl_getMeshProperties(mesh, &properties));
            properties.material = material;
            Check(bl_setMeshProperties(mesh, &properties));
            bl_SceneNode node{};
            Check(bl_meshNode(mesh, &node));
            Check(bl_addToScene(scene, node));
        }

        void StandardMesh(bl_Runtime* runtime, bl_SceneContext scene, bl_Mesh mesh,
                          bl_Vec3 position, bl_Vec3 scaling, bl_Vec3 diffuse, bl_Vec3 specular,
                          bl_Vec3 emissive)
        {
            bl_StandardMaterial material{};
            Check(bl_createStandardMaterial(runtime, &material));
            bl_StandardMaterialProperties properties{};
            Check(bl_getStandardMaterialProperties(material, &properties));
            properties.diffuseColor = diffuse;
            properties.specularColor = specular;
            properties.emissiveColor = emissive;
            Check(bl_setStandardMaterialProperties(material, &properties));
            bl_MeshProperties2 state{};
            Check(bl_getMeshProperties2(mesh, &state));
            Check(bl_standardMaterialAsMaterial(material, &state.material));
            Check(bl_setMeshProperties2(mesh, &state));
            bl_SceneNode node{};
            Check(bl_meshNode(mesh, &node));
            Check(bl_setNodePosition(node, position));
            Check(bl_setNodeScaling(node, scaling));
            Check(bl_addToScene(scene, node));
        }

        void Hpm(bl_Runtime* runtime, bl_EngineContext engine, bl_SceneContext scene)
        {
            constexpr double Offset = 5000000;
            Clear(scene, {0.05, 0.05, 0.08, 1});
            Camera(runtime, scene, Pi / 4, Pi / 3, 25, {Offset, 1, Offset}, 0.5, 500, false);
            bl_HemisphericLight hemi{};
            const bl_HemisphericLightOptions hemiOptions{true, {0, 1, 0}, {true, 0.4}};
            Check(bl_createHemisphericLight(runtime, &hemiOptions, &hemi));
            bl_HemisphericLightProperties hemiState{};
            Check(bl_getHemisphericLightProperties(hemi, &hemiState));
            hemiState.intensity = 0.4;
            Check(bl_setHemisphericLightProperties(hemi, &hemiState));
            bl_Light light{};
            Check(bl_hemisphericLightAsLight(hemi, &light));
            bl_SceneNode node{};
            Check(bl_lightNode(light, &node));
            Check(bl_addToScene(scene, node));
            bl_DirectionalLight dir{};
            const bl_DirectionalLightOptions dirOptions{{-0.4, -1, -0.2}, {}};
            Check(bl_createDirectionalLight(runtime, &dirOptions, &dir));
            bl_DirectionalLightProperties dirState{};
            Check(bl_getDirectionalLightProperties(dir, &dirState));
            dirState.diffuse = {1, 1, 1};
            dirState.specular = {0.3, 0.3, 0.3};
            Check(bl_setDirectionalLightProperties(dir, &dirState));
            Check(bl_directionalLightAsLight(dir, &light));
            Check(bl_lightNode(light, &node));
            Check(bl_addToScene(scene, node));
            bl_GroundOptions groundOptions{};
            groundOptions.width = {true, 40};
            groundOptions.height = {true, 40};
            groundOptions.subdivisions = {true, 1};
            bl_Mesh ground{};
            Check(bl_createGround(engine, &groundOptions, &ground));
            StandardMesh(runtime, scene, ground, {Offset, 0, Offset}, {1, 1, 1}, {0.25, 0.25, 0.3},
                         {1, 1, 1}, {});
            for (int i = 0; i < 5; ++i)
            {
                for (int j = 0; j < 5; ++j)
                {
                    bl_Mesh box{};
                    Check(bl_createBox(engine, nullptr, &box));
                    StandardMesh(runtime, scene, box,
                                 {Offset + (i - 2) * 4, 1, Offset + (j - 2) * 4}, {1, 1, 1},
                                 {0.3 + i / 4.0 * 0.6, 0.4, 0.3 + j / 4.0 * 0.6}, {0.4, 0.4, 0.4},
                                 {});
                }
            }
            bl_Mesh pillar{};
            Check(bl_createBox(engine, nullptr, &pillar));
            StandardMesh(runtime, scene, pillar, {Offset, 2, Offset}, {0.8, 4, 0.8},
                         {0.9, 0.5, 0.2}, {0.6, 0.6, 0.6}, {0.1, 0.05, 0.02});
        }
    }

    bl_SceneContext CreateScene(uint32_t id, bl_Runtime* runtime, bl_EngineContext engine)
    {
        bl_SceneContext scene{};
        Check(bl_createSceneContext(engine, &scene));
        Clear(scene, {0.2, 0.2, 0.3, 1});
        if (id == 159 || id == 161 || id == 163)
        {
            Clear(scene, {51.0 / 255, 51.0 / 255, 76.0 / 255, 1});
            Camera(runtime, scene, -Pi / 2, id == 163 ? Pi / 2 : Pi / 2.25, id == 163 ? 4 : 4.2, {},
                   0.1, 100, false);
            CustomShader(id, runtime, engine, scene);
        }
        else if (id == 200)
        {
            Hpm(runtime, engine, scene);
        }
        else if (id == 168)
        {
            Clear(scene, {0.05, 0.06, 0.09, 1});
            Camera(runtime, scene, -Pi / 2, Pi / 2, 6, {}, 0.1, 100, false);
            Environment(scene, true);
            Model(engine, scene, "/gltf-assets/MirroredDoubleSided/MirroredDoubleSided.gltf");
        }
        else if (id == 247 || id == 248 || id == 249 || id == 256)
        {
            const char* url =
                id == 247 ? "https://cx20.github.io/gltf-test/tutorialModels/TeapotsGalore/glTF/"
                            "TeapotsGalore.gltf"
                : id == 248
                    ? "https://cdn.jsdelivr.net/gh/KhronosGroup/glTF-Sample-Assets@main/Models/"
                      "TextureSettingsTest/glTF/TextureSettingsTest.gltf"
                : id == 249
                    ? "https://cx20.github.io/gltf-test/tutorialModels/VertexColorAlphaClipTest/glTF/"
                      "VertexColorAlphaClipTest.gltf"
                    : "https://cx20.github.io/gltf-test/tutorialModels/NormalTangentTest/glTF/"
                      "NormalTangentTest.gltf";
            Model(engine, scene, url);
            Environment(scene, false);
            const double radius = id == 247   ? 1440.14
                                  : id == 248 ? 21.64
                                  : id == 249 ? 28.22
                                              : 4.6377;
            const bl_Vec3 target = id == 247   ? bl_Vec3{0, 6.649, 0}
                                   : id == 248 ? bl_Vec3{0, -0.583, -0.025}
                                   : id == 249 ? bl_Vec3{0, 0.728, 0}
                                               : bl_Vec3{0, -0.125, 0.03525};
            Camera(runtime, scene, 1.5707963, id == 247 ? 0.15 : 1.5707963, radius, target,
                   radius * 0.01, radius * 1000, true);
        }
        else if (id == 257 || id == 258 || id == 259 || id == 260 || id == 266)
        {
            if (id == 257)
            {
                Camera(runtime, scene, -Pi / 2, Pi / 4, std::sqrt(800.0), {}, 1, 1000, true);
            }
            else if (id == 266)
            {
                Camera(runtime, scene, -Pi / 2, Pi / 2.15, 10.5, {0.35, -1.9, 0}, 0.1, 1000, true);
            }
            else
            {
                Camera(runtime, scene, Pi / 2, Pi / 2, 1.3, {}, 0.01, 1000, true);
            }
            Environment(scene, true);
            const char* url =
                id == 257   ? "/gltf-assets/Node_NegativeScale/Node_NegativeScale_01.gltf"
                : id == 258 ? "/gltf-assets/Buffer_Interleaved/Buffer_Interleaved_03.gltf"
                : id == 259 ? "/gltf-assets/Material/Material_03.gltf"
                : id == 260 ? "/gltf-assets/Mesh_PrimitiveMode/Mesh_PrimitiveMode_11.gltf"
                            : "/gltf-assets/NegativeScaleTest/NegativeScaleTest.glb";
            Model(engine, scene, url);
        }
        else
        {
            throw std::runtime_error("Scene not implemented");
        }
        Check(bl_registerScene(scene));
        return scene;
    }
}
