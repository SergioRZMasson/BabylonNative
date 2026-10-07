#include "C99Client.h"
#include <cmath>
#include <iostream>

namespace LiteMinecraft
{
    namespace
    {
        void Require(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(message);
            }
        }
    }

    void RunAdapterContract()
    {
        auto engine = bbl::babylon::createEngine({"Adapter contract", 1280, 720});
        auto scene = bbl::babylon::createSceneContext(engine);
        const auto first = bbl::babylon::createShaderMaterial(engine, 6);
        const auto second = bbl::babylon::createShaderMaterial(engine, 6);
        bbl::set_shader_uniform_value(engine, first, 0, 0.2f, 0.4f, 0.6f);
        bl_ShaderUniformView a{};
        bl_ShaderUniformView b{};
        bbl::Check(bl_getShaderUniform(first.native, bbl::String("fogColor"), &a));
        bbl::Check(bl_getShaderUniform(second.native, bbl::String("fogColor"), &b));
        Require(a.values.count == 3 && std::abs(a.values.data[0] - 0.2f) < 1e-6f &&
                    b.values.data[0] == 0,
                "Material-instance uniform state was incorrectly shared.");
        const auto mesh = bbl::babylon::createBox(engine, {2, 3, 4});
        const auto meshNode = bbl::Node(mesh);
        const auto parent = bbl::babylon::createTransformNode(engine, "parent", {10, 20, 30},
                                                              {0, 0, 0, 1}, {1, 1, 1});
        bbl::set_mesh_material(engine, mesh, first);
        bbl::set_mesh_transform_parent(engine, mesh, parent);
        bbl::push_transform_node_child(engine, parent, mesh);
        bbl::handle_at(engine.meshes, mesh).position.x = 3;
        bbl::handle_at(engine.meshes, mesh).position.y =
            bbl::handle_at(engine.meshes, mesh).position.x;
        bbl::handle_at(engine.meshes, mesh).scaling = {0, 1, 1};
        bl_Vec3 position{};
        bl_Vec3 scaling{};
        bbl::Check(bl_getNodePosition(bbl::Node(mesh), &position));
        bbl::Check(bl_getNodeScaling(bbl::Node(mesh), &scaling));
        Require(position.x == 3 && position.y == 3 && scaling.x == 0,
                "Mutable node-property forwarding failed.");
        bl_SceneNode actualParent{};
        bbl::Check(bl_getNodeParent(bbl::Node(mesh), &actualParent));
        bl_String name{};
        bbl::Check(bl_getNodeName(actualParent, &name));
        Require(std::string(name.data, name.length) == "parent",
                "Native parent identity was lost.");
        const auto camera = bbl::babylon::createFreeCamera(engine, {1, 2, 3}, {4, 5, 6});
        bbl::write_camera_vector_component(bbl::handle_at(engine.cameras, camera),
                                           &bbl::CameraRecord::target, &bbl::Vec3d::y, 8);
        bl_Vec3 target{};
        bbl::Check(bl_getCameraTarget(camera, &target));
        Require(target.x == 4 && target.y == 8 && target.z == 6,
                "Camera component mutation did not preserve other components.");
        scene.camera = camera;
        scene.clear_color = {0.7, 0.82, 0.92, 1};
        bbl::babylon::addToScene(scene, parent);
        bbl::babylon::setSubtreeVisible(engine, parent, false);
        bool visible{};
        bbl::Check(bl_getNodeVisible(bbl::Node(mesh), &visible));
        Require(!visible, "Native subtree visibility was not forwarded.");
        bbl::babylon::removeFromScene(scene, parent);
        Require(bl_getNodePosition(meshNode, &position) == BL_DISPOSED,
                "Retired native mesh identity was unexpectedly revived.");
        std::cout << "C99 application adapters: independent materials, geometry, hierarchy, "
                     "properties, camera, visibility and retirement passed.\n";
    }
}
