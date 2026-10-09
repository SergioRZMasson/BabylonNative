#include "Binding/bblite/runtime.hpp"
#include "Binding/OriginalPresetMetadata.h"

namespace bbl
{
    Engine create_engine(EngineOptions options)
    {
        if (options.width != 1280 || options.height != 720)
        {
            throw std::runtime_error("Host viewport differs from frozen canonical scene38 main");
        }
        return {Scene38Host::CreateEngine(), {}, {}, {}};
    }
    Scene create_scene_context(Engine& engine)
    {
        bl_SceneContext scene{};
        Scene38Host::Check(bl_createSceneContext(engine.native, &scene));
        return {scene, {scene}};
    }
    CameraHandle create_arc_rotate_camera(Engine& engine, double alpha, double beta, double radius, Vec3d target)
    {
        CameraHandle camera{};
        Scene38Host::Check(bl_createArcRotateCamera(Scene38Host::Runtime(), alpha, beta, radius, target, &camera));
        auto projection = std::make_unique<CameraProjection>();
        projection->identity = camera;
        projection->near_plane = {camera, &bl_ArcRotateCameraProperties::nearPlane};
        projection->far_plane = {camera, &bl_ArcRotateCameraProperties::farPlane};
        engine.cameras.values.push_back(std::move(projection));
        return camera;
    }
    static LightHandle retain_light(Engine& engine, LightHandle light)
    {
        auto projection = std::make_unique<LightProjection>();
        projection->identity = light;
        projection->diffuse_color = {light, false};
        projection->specular_color = {light, true};
        engine.lights.values.push_back(std::move(projection));
        return light;
    }
    LightHandle create_hemispheric_light(Engine& engine, Vec3 direction, double intensity)
    {
        bl_HemisphericLight light{};
        const bl_HemisphericLightOptions options{true, direction, {true, intensity}};
        Scene38Host::Check(bl_createHemisphericLight(Scene38Host::Runtime(), &options, &light));
        LightHandle result{};
        Scene38Host::Check(bl_hemisphericLightAsLight(light, &result));
        return retain_light(engine, result);
    }
    LightHandle create_directional_light(Engine& engine, Vec3 direction, double intensity)
    {
        bl_DirectionalLight light{};
        const bl_DirectionalLightOptions options{direction, {true, intensity}};
        Scene38Host::Check(bl_createDirectionalLight(Scene38Host::Runtime(), &options, &light));
        LightHandle result{};
        Scene38Host::Check(bl_directionalLightAsLight(light, &result));
        return retain_light(engine, result);
    }
    void LightColor::operator=(Color3 value)
    {
        bl_DirectionalLight directional{};
        const auto kind = bl_lightAsDirectionalLight(light, &directional);
        if (kind == BL_OK)
        {
            bl_DirectionalLightProperties properties{};
            Scene38Host::Check(bl_getDirectionalLightProperties(directional, &properties));
            (specular ? properties.specular : properties.diffuse) = {value.r, value.g, value.b};
            Scene38Host::Check(bl_setDirectionalLightProperties(directional, &properties));
        }
        else if (kind == BL_INVALID_HANDLE)
        {
            bl_HemisphericLightProperties properties{};
            const bl_HemisphericLight hemi{light._runtime, light._id};
            Scene38Host::Check(bl_getHemisphericLightProperties(hemi, &properties));
            (specular ? properties.specularColor : properties.diffuseColor) = {value.r, value.g, value.b};
            Scene38Host::Check(bl_setHemisphericLightProperties(hemi, &properties));
        }
        else
        {
            Scene38Host::Check(kind);
        }
    }
    static MeshHandle retain_mesh(Engine& engine, MeshHandle mesh)
    {
        auto projection = std::make_unique<MeshProjection>();
        projection->identity = mesh;
        Scene38Host::Check(bl_meshNode(mesh, &projection->position.node));
        engine.meshes.values.push_back(std::move(projection));
        return mesh;
    }
    MeshHandle create_cylinder(Engine& engine, CylinderOptions input)
    {
        bl_CylinderOptions options{};
        options.height = {true, input.height};
        options.diameter = {true, input.diameter_top};
        options.diameterBottom = {true, input.diameter_bottom};
        if (input.cone_tip)
        {
            options.diameterTop = {true, input.diameter_top};
        }
        options.tessellation = {true, input.tessellation};
        options.subdivisions = {true, input.subdivisions};
        MeshHandle mesh{};
        Scene38Host::Check(bl_createCylinder(engine.native, &options, &mesh));
        return retain_mesh(engine, mesh);
    }
    MeshHandle create_plane(Engine& engine, PlaneOptions input)
    {
        const bl_PlaneOptions options{{}, {true, input.width}, {true, input.height}};
        MeshHandle mesh{};
        Scene38Host::Check(bl_createPlane(engine.native, &options, &mesh));
        return retain_mesh(engine, mesh);
    }
    MeshHandle create_disc(Engine& engine, DiscOptions input)
    {
        const bl_DiscOptions options{{true, input.radius}, {true, input.tessellation}, {true, input.arc}};
        MeshHandle mesh{};
        Scene38Host::Check(bl_createDisc(engine.native, &options, &mesh));
        return retain_mesh(engine, mesh);
    }
    MeshHandle create_polyhedron(Engine& engine, const PolyhedronOptions& input)
    {
        // The compiler ABI carries its selected original numeric table, not type.
        // Recover only an EXACT source preset identity; arbitrary tables are unsupported.
        size_t type = 15;
        for (size_t index = 0; index < originalPresetMetadata.size(); ++index)
        {
            const auto& preset = originalPresetMetadata[index];
            if (input.vertices == preset.vertices && input.faces == preset.faces)
            {
                type = index;
                break;
            }
        }
        if (type == 15)
        {
            Scene38Host::Check(BL_UNSUPPORTED);
        }
        const bl_PolyhedronOptions options{{true, double(type)}, {}, {true, input.size_x},
            {true, input.size_y}, {true, input.size_z}, input.flat ? BL_BOOL_TRUE : BL_BOOL_FALSE};
        MeshHandle mesh{};
        Scene38Host::Check(bl_createPolyhedron(engine.native, &options, &mesh));
        return retain_mesh(engine, mesh);
    }
    MeshHandle create_ribbon(Engine& engine, const RibbonOptions& input)
    {
        std::vector<bl_Vec3Span> rows;
        rows.reserve(input.paths.size());
        for (const auto& row : input.paths)
        {
            rows.push_back({row.data(), row.size()});
        }
        const bl_RibbonOptions options{{rows.data(), rows.size()}, input.close_array, input.close_path, {}};
        MeshHandle mesh{};
        Scene38Host::Check(bl_createRibbon(engine.native, &options, &mesh));
        return retain_mesh(engine, mesh);
    }
    MeshHandle create_tube(Engine& engine, const std::vector<Vec3d>& path, double radius, double tessellation)
    {
        const bl_TubeOptions options{{path.data(), path.size()}, {true, radius},
            {true, tessellation}, BL_CAP_NONE, {}};
        MeshHandle mesh{};
        Scene38Host::Check(bl_createTube(engine.native, &options, &mesh));
        return retain_mesh(engine, mesh);
    }
    MeshHandle create_tube(Engine&, const std::vector<Vec3d>&, double, double,
        const std::function<double(size_t, double)>&)
    {
        Scene38Host::Check(BL_UNSUPPORTED);
        return {};
    }
    MeshHandle create_extrude_shape(Engine& engine, const std::vector<Vec3d>& shape,
        const std::vector<Vec3d>& path, double scale, double rotation)
    {
        const bl_ExtrudeShapeOptions options{{shape.data(), shape.size()}, {path.data(), path.size()},
            {true, scale}, {true, rotation}, BL_CAP_NONE};
        MeshHandle mesh{};
        Scene38Host::Check(bl_createExtrudeShape(engine.native, &options, &mesh));
        return retain_mesh(engine, mesh);
    }
    MaterialHandle create_standard_material(Engine&)
    {
        MaterialHandle material{};
        Scene38Host::Check(bl_createStandardMaterial(Scene38Host::Runtime(), &material));
        return material;
    }
    void set_material_diffuse_color(Engine&, MaterialHandle material, const js::Array<double>& color)
    {
        if (color.size() != 3)
        {
            throw std::runtime_error("Standard diffuseColor requires three components");
        }
        bl_StandardMaterialProperties properties{};
        Scene38Host::Check(bl_getStandardMaterialProperties(material, &properties));
        properties.diffuseColor = {color[0], color[1], color[2]};
        Scene38Host::Check(bl_setStandardMaterialProperties(material, &properties));
    }
    void set_mesh_material(Engine&, MeshHandle mesh, MaterialHandle material)
    {
        bl_MeshProperties2 properties{};
        Scene38Host::Check(bl_getMeshProperties2(mesh, &properties));
        Scene38Host::Check(bl_standardMaterialAsMaterial(material, &properties.material));
        Scene38Host::Check(bl_setMeshProperties2(mesh, &properties));
    }
    void add_to_scene(Scene& scene, MeshHandle mesh)
    {
        bl_SceneNode node{};
        Scene38Host::Check(bl_meshNode(mesh, &node));
        Scene38Host::Check(bl_addToScene(scene.native, node));
    }
    void add_to_scene(Scene& scene, LightHandle light)
    {
        bl_SceneNode node{};
        Scene38Host::Check(bl_lightNode(light, &node));
        Scene38Host::Check(bl_addToScene(scene.native, node));
    }
    void register_scene(Scene& scene) { Scene38Host::Check(bl_registerScene(scene.native)); }
    void start_engine(Engine& engine)
    {
        Scene38Host::Check(bl_startEngine(engine.native, nullptr, nullptr));
        Scene38Host::Run(engine.native);
    }
}
