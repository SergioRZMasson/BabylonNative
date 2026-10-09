#include "Binding/bblite/runtime.hpp"

namespace bbl
{
    Engine create_engine(EngineOptions options)
    {
        if (options.width != 1280 || options.height != 720)
            throw std::runtime_error("Host viewport differs from canonical unchanged scene2");
        return {Scene2Host::CreateEngine(), {}, {}, {}};
    }
    Scene create_scene_context(Engine& engine)
    {
        bl_SceneContext scene{};
        Scene2Host::Check(bl_createSceneContext(engine.native, &scene));
        return {scene, {scene, {}}};
    }
    CameraHandle create_arc_rotate_camera(Engine& engine, double alpha, double beta, double radius, Vec3d target)
    {
        CameraHandle camera{};
        Scene2Host::Check(bl_createArcRotateCamera(Scene2Host::Runtime(), alpha, beta, radius, target, &camera));
        auto projection = std::make_unique<CameraProjection>();
        projection->identity = camera;
        projection->near_plane = {camera, &bl_ArcRotateCameraProperties::nearPlane};
        projection->far_plane = {camera, &bl_ArcRotateCameraProperties::farPlane};
        engine.cameras.values.push_back(std::move(projection));
        return camera;
    }
    void attach_control(Engine& engine, CameraHandle camera, Scene& scene)
    {
        Scene2Host::Check(bl_attachControl(camera, scene.native, nullptr,
            &handle_at(engine.cameras, camera).controls_enabled.control));
    }
    LightHandle create_directional_light(Engine& engine, Vec3 direction, double intensity)
    {
        LightHandle light{};
        const bl_DirectionalLightOptions options{direction, {true, intensity}};
        Scene2Host::Check(bl_createDirectionalLight(Scene2Host::Runtime(), &options, &light));
        auto projection = std::make_unique<LightProjection>();
        projection->identity = light;
        projection->diffuse_color = {light, false};
        projection->specular_color = {light, true};
        engine.lights.values.push_back(std::move(projection));
        return light;
    }
    MeshHandle create_sphere(Engine& engine, SphereOptions options)
    {
        MeshHandle mesh{};
        const bl_SphereOptions sphere{{true, double(options.segments)}, {},
            {true, options.diameter_x}, {true, options.diameter_y}, {true, options.diameter_z}};
        Scene2Host::Check(bl_createSphere(engine.native, &sphere, &mesh));
        auto projection = std::make_unique<MeshProjection>();
        projection->identity = mesh;
        engine.meshes.values.push_back(std::move(projection));
        return mesh;
    }
    MaterialHandle create_standard_material(Engine&)
    {
        MaterialHandle material{};
        Scene2Host::Check(bl_createStandardMaterial(Scene2Host::Runtime(), &material));
        return material;
    }
    void set_mesh_material(Engine&, MeshHandle mesh, MaterialHandle material)
    {
        bl_MeshProperties2 properties{};
        Scene2Host::Check(bl_getMeshProperties2(mesh, &properties));
        Scene2Host::Check(bl_standardMaterialAsMaterial(material, &properties.material));
        Scene2Host::Check(bl_setMeshProperties2(mesh, &properties));
    }
    void add_to_scene(Scene& scene, MeshHandle mesh)
    {
        bl_SceneNode node{};
        Scene2Host::Check(bl_meshNode(mesh, &node));
        Scene2Host::Check(bl_addToScene(scene.native, node));
    }
    void add_to_scene(Scene& scene, LightHandle light)
    {
        bl_Light family{};
        bl_SceneNode node{};
        Scene2Host::Check(bl_directionalLightAsLight(light, &family));
        Scene2Host::Check(bl_lightNode(family, &node));
        Scene2Host::Check(bl_addToScene(scene.native, node));
    }
    void register_scene(Scene& scene) { Scene2Host::Check(bl_registerScene(scene.native)); }
    void start_engine(Engine& engine)
    {
        Scene2Host::Check(bl_startEngine(engine.native, nullptr, nullptr));
        Scene2Host::Run(engine.native);
    }
}
