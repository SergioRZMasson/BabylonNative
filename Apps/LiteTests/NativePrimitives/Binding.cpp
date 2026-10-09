#include "Binding/bblite/runtime.hpp"

namespace bbl
{
    Engine create_engine(EngineOptions options)
    {
        if (options.width != 1280 || options.height != 720)
            throw std::runtime_error("Host viewport differs from canonical unchanged main");
        return {PrimitivesHost::CreateEngine(), {}, {}};
    }
    Scene create_scene_context(Engine& engine)
    {
        bl_SceneContext scene{};
        PrimitivesHost::Check(bl_createSceneContext(engine.native, &scene));
        return {scene, {scene}, {scene}};
    }
    CameraHandle create_arc_rotate_camera(Engine& engine, double alpha, double beta, double radius, Vec3d target)
    {
        CameraHandle camera{};
        PrimitivesHost::Check(bl_createArcRotateCamera(PrimitivesHost::Runtime(), alpha, beta, radius, target, &camera));
        auto projection = std::make_unique<CameraProjection>();
        projection->identity = camera;
        engine.cameras.values.push_back(std::move(projection));
        return camera;
    }
    void attach_control(Engine& engine, CameraHandle camera, Scene& scene)
    {
        PrimitivesHost::Check(bl_attachControl(camera, scene.native, nullptr,
            &handle_at(engine.cameras, camera).controls_enabled.control));
    }
    LightHandle create_hemispheric_light(Engine&, Vec3 direction, double intensity)
    {
        LightHandle light{};
        const bl_HemisphericLightOptions options{true, direction, {true, intensity}};
        PrimitivesHost::Check(bl_createHemisphericLight(PrimitivesHost::Runtime(), &options, &light));
        return light;
    }
    static MeshHandle retain_mesh(Engine& engine, MeshHandle mesh)
    {
        auto projection = std::make_unique<MeshProjection>();
        projection->identity = mesh;
        PrimitivesHost::Check(bl_meshNode(mesh, &projection->position.node));
        engine.meshes.values.push_back(std::move(projection));
        return mesh;
    }
    MeshHandle create_box(Engine& engine, BoxOptions options)
    {
        MeshHandle mesh{};
        const bl_BoxOptions box{{}, {true, options.width}, {true, options.height}, {true, options.depth}};
        PrimitivesHost::Check(bl_createBox(engine.native, &box, &mesh));
        return retain_mesh(engine, mesh);
    }
    MeshHandle create_ground(Engine& engine, GroundOptions options)
    {
        MeshHandle mesh{};
        const bl_GroundOptions ground{{true, options.width}, {true, options.height},
            {true, double(options.subdivisions)}, true, {options.uvScale.x, options.uvScale.y}};
        PrimitivesHost::Check(bl_createGround(engine.native, &ground, &mesh));
        return retain_mesh(engine, mesh);
    }
    MaterialHandle create_standard_material(Engine&)
    {
        MaterialHandle material{};
        PrimitivesHost::Check(bl_createStandardMaterial(PrimitivesHost::Runtime(), &material));
        return material;
    }
    void set_material_diffuse_color(Engine&, MaterialHandle material, const js::Array<double>& color)
    {
        bl_StandardMaterialProperties properties{};
        PrimitivesHost::Check(bl_getStandardMaterialProperties(material, &properties));
        properties.diffuseColor = {color[0], color[1], color[2]};
        PrimitivesHost::Check(bl_setStandardMaterialProperties(material, &properties));
    }
    void set_mesh_material(Engine&, MeshHandle mesh, MaterialHandle material)
    {
        bl_MeshProperties2 properties{};
        PrimitivesHost::Check(bl_getMeshProperties2(mesh, &properties));
        PrimitivesHost::Check(bl_standardMaterialAsMaterial(material, &properties.material));
        PrimitivesHost::Check(bl_setMeshProperties2(mesh, &properties));
    }
    void add_to_scene(Scene& scene, MeshHandle mesh)
    {
        bl_SceneNode node{};
        PrimitivesHost::Check(bl_meshNode(mesh, &node));
        PrimitivesHost::Check(bl_addToScene(scene.native, node));
    }
    void add_to_scene(Scene& scene, LightHandle light)
    {
        bl_Light family{};
        bl_SceneNode node{};
        PrimitivesHost::Check(bl_hemisphericLightAsLight(light, &family));
        PrimitivesHost::Check(bl_lightNode(family, &node));
        PrimitivesHost::Check(bl_addToScene(scene.native, node));
    }
    void register_scene(Scene& scene) { PrimitivesHost::Check(bl_registerScene(scene.native)); }
    void start_engine(Engine& engine)
    {
        PrimitivesHost::Check(bl_startEngine(engine.native, nullptr, nullptr));
        PrimitivesHost::Run(engine.native);
    }
}
