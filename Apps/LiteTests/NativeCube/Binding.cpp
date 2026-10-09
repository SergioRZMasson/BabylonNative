#include "Binding/bblite/runtime.hpp"
namespace bbl
{
    Engine create_engine(EngineOptions options)
    {
        if (options.width != 1280 || options.height != 720)
            throw std::runtime_error("Host viewport differs from unchanged compiler main ABI");
        return {CubeHost::CreateEngine(), {}};
    }
    Scene create_scene_context(Engine& engine)
    {
        bl_SceneContext scene{};
        CubeHost::Check(bl_createSceneContext(engine.native, &scene));
        return {scene, {scene}};
    }
    CameraHandle create_arc_rotate_camera(Engine&, double alpha, double beta, double radius, Vec3d target)
    {
        CameraHandle camera{};
        CubeHost::Check(bl_createArcRotateCamera(CubeHost::Runtime(), alpha, beta, radius, target, &camera));
        return camera;
    }
    LightHandle create_hemispheric_light(Engine&, Vec3 direction, double intensity)
    {
        LightHandle light{};
        const bl_HemisphericLightOptions options{true, direction, {true, intensity}};
        CubeHost::Check(bl_createHemisphericLight(CubeHost::Runtime(), &options, &light));
        return light;
    }
    MeshHandle create_box(Engine& engine, BoxOptions options)
    {
        MeshHandle mesh{};
        bl_BoxOptions box{};
        box.width = {true, options.width};
        box.height = {true, options.height};
        box.depth = {true, options.depth};
        CubeHost::Check(bl_createBox(engine.native, &box, &mesh));
        bl_SceneNode node{};
        CubeHost::Check(bl_meshNode(mesh, &node));
        engine.meshes.identity = mesh;
        engine.meshes.value.rotation.y.node = node;
        return mesh;
    }
    MaterialHandle create_standard_material(Engine&)
    {
        MaterialHandle material{};
        CubeHost::Check(bl_createStandardMaterial(CubeHost::Runtime(), &material));
        return material;
    }
    void set_material_diffuse_color(Engine&, MaterialHandle material, const js::Array<double>& color)
    {
        bl_StandardMaterialProperties properties{};
        CubeHost::Check(bl_getStandardMaterialProperties(material, &properties));
        properties.diffuseColor = {color[0], color[1], color[2]};
        CubeHost::Check(bl_setStandardMaterialProperties(material, &properties));
    }
    void set_mesh_material(Engine&, MeshHandle mesh, MaterialHandle material)
    {
        bl_MeshProperties2 properties{};
        CubeHost::Check(bl_getMeshProperties2(mesh, &properties));
        CubeHost::Check(bl_standardMaterialAsMaterial(material, &properties.material));
        CubeHost::Check(bl_setMeshProperties2(mesh, &properties));
    }
    void add_to_scene(Scene& scene, MeshHandle mesh)
    {
        bl_SceneNode node{};
        CubeHost::Check(bl_meshNode(mesh, &node));
        CubeHost::Check(bl_addToScene(scene.native, node));
    }
    void add_to_scene(Scene& scene, LightHandle light)
    {
        bl_Light family{};
        bl_SceneNode node{};
        CubeHost::Check(bl_hemisphericLightAsLight(light, &family));
        CubeHost::Check(bl_lightNode(family, &node));
        CubeHost::Check(bl_addToScene(scene.native, node));
    }
    void register_scene(Scene& scene) { CubeHost::Check(bl_registerScene(scene.native)); }
    void start_engine(Engine& engine)
    {
        CubeHost::Check(bl_startEngine(engine.native, nullptr, nullptr));
        CubeHost::Run(engine.native);
    }
}
