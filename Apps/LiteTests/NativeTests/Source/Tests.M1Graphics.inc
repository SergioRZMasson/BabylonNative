TEST_F(LiteNativeGraphics, M1ComputeCullsAndCompactsMatricesColorsAndResetsIndirectCount)
{
    const auto compute = bl_computeShaderCompilerService();
    ASSERT_EQ(bl_setComputeShaderCompiler(runtime->runtime, &compute), BL_OK);
    bl_Mesh prototype{};
    ASSERT_EQ(bl_createBox(engine, nullptr, &prototype), BL_OK);
    bl_StandardMaterial standard{};
    ASSERT_EQ(bl_createStandardMaterial(runtime->runtime, &standard), BL_OK);
    bl_StandardMaterialProperties properties{};
    ASSERT_EQ(bl_getStandardMaterialProperties(standard, &properties), BL_OK);
    properties.disableLighting = true;
    properties.emissiveColor = {1, 1, 1};
    ASSERT_EQ(bl_setStandardMaterialProperties(standard, &properties), BL_OK);
    bl_MeshProperties2 meshProperties{};
    ASSERT_EQ(bl_standardMaterialAsMaterial(standard, &meshProperties.material), BL_OK);
    ASSERT_EQ(bl_setMeshProperties2(prototype, &meshProperties), BL_OK);
    std::array<float, 70 * 16> matrices{};
    std::array<float, 70 * 4> colors{};
    for (size_t i = 0; i < 70; ++i)
    {
        matrices[i * 16] = matrices[i * 16 + 5] = matrices[i * 16 + 10] = matrices[i * 16 + 15] = 1;
        matrices[i * 16 + 12] = i == 0 ? -.8f : i == 1 ? .8f
                                                       : static_cast<float>(1000 + i);
        colors[i * 4] = static_cast<float>((i + 1) / 100.0);
        colors[i * 4 + 1] = .2f;
        colors[i * 4 + 2] = .3f;
        colors[i * 4 + 3] = 1;
    }

    // Original GPU predicate includes zero-linear instances; the CPU world-bounds
    // helper deliberately skips them. This distinguishes the actual GPU algorithm.
    matrices[32] = matrices[37] = matrices[42] = matrices[44] = 0;
    ASSERT_EQ(bl_setThinInstances(prototype, {matrices.data(), matrices.size()}, 70), BL_OK);
    ASSERT_EQ(bl_setThinInstanceColors(prototype, {colors.data(), colors.size()}), BL_OK);
    bl_SceneNode node{};
    ASSERT_EQ(bl_meshNode(prototype, &node), BL_OK);
    ASSERT_EQ(bl_addToScene(scene, node), BL_OK);
    ASSERT_EQ(bl_registerScene(scene), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 0), BL_OK);
    ASSERT_EQ(bl_enableThinInstanceGpuCulling(prototype, true), BL_OK);
    const auto status = bl_renderFrame(engine, 0);
    if (status != BL_OK)
    {
        bl_Error error{};
        bl_getLastError(runtime->runtime, &error);
        FAIL() << std::string(error.message.data ? error.message.data : "", error.message.length);
    }
    auto* nativeScene = reinterpret_cast<L_Scene*>(l_peek(runtime->runtime, scene._id));
    ASSERT_NE(nativeScene->thinCulls, nullptr);
    auto* state = nativeScene->thinCulls;
    EXPECT_EQ(state->stride, 80u);
    std::array<uint32_t, 5> args{};
    auto readBuffer = [&](auto handle, void* destination, uint32_t size) {
        bgfx::BufferRegion region{};
        region.init(handle, 0, size);
        const uint32_t ready = bgfx::read(region, destination);
        for (unsigned i = 0; i < 64; ++i)
        {
            if (bgfx::frame() >= ready)
                return true;
        }
        return false;
    };
    ASSERT_TRUE(readBuffer(state->args, args.data(), sizeof(args)));
    EXPECT_EQ(args[0], 36u);
    EXPECT_EQ(args[1], 3u);
    EXPECT_EQ(args[2], 0u);
    EXPECT_EQ(args[3], 0u);
    EXPECT_EQ(args[4], 0u);
    ASSERT_EQ(args[1], 3u);
    std::array<float, 3 * 20> visible{};
    ASSERT_TRUE(readBuffer(state->visible, visible.data(), sizeof(visible)));
    std::array<bool, 3> seen{};
    for (size_t i = 0; i < 3; ++i)
    {
        size_t source = 3;
        for (size_t j = 0; j < 3; ++j)
        {
            if (visible[i * 20 + 16] == colors[j * 4])
                source = j;
        }
        ASSERT_LT(source, 3u);
        EXPECT_FALSE(seen[source]);
        seen[source] = true;
        EXPECT_EQ(std::memcmp(visible.data() + i * 20, matrices.data() + source * 16, 64), 0);
        EXPECT_EQ(std::memcmp(visible.data() + i * 20 + 16, colors.data() + source * 4, 16), 0);
    }
    const auto buffer = state->visible.idx;
    ASSERT_EQ(bl_renderFrame(engine, 0), BL_OK);
    ASSERT_TRUE(readBuffer(state->args, args.data(), sizeof(args)));
    EXPECT_EQ(args[1], 3u);
    EXPECT_EQ(state->visible.idx, buffer);
    bl_Mat4 moved{};
    ASSERT_EQ(bl_createTranslationMat4(0, .5, 0, false, &moved), BL_OK);
    ASSERT_EQ(bl_setThinInstanceMatrix(prototype, 3, &moved), BL_OK);
    ASSERT_EQ(bl_renderFrame(engine, 0), BL_OK);
    ASSERT_TRUE(readBuffer(state->args, args.data(), sizeof(args)));
    EXPECT_EQ(args[1], 4u);
    EXPECT_EQ(state->visible.idx, buffer);
    auto* nativeMesh = reinterpret_cast<L_Mesh*>(l_peek(runtime->runtime, prototype._id));
    const auto instanceBuffer = nativeMesh->instanceBufferIndex;
    bl_ThinInstanceView2 before{};
    ASSERT_EQ(bl_getThinInstances2(prototype, &before), BL_OK);
    matrices[3 * 16 + 12] = .25f;
    std::array<float, 32> replacementColors{};
    replacementColors.fill(.5f);
    bl_ThinInstanceUpdate update{{matrices.data(), matrices.size()}, {},
        {replacementColors.data(), replacementColors.size()}, 4, true, true};
    replacementColors.back() = NAN;
    EXPECT_EQ(bl_writeThinInstanceData(prototype, &update), BL_INVALID_ARGUMENT);
    bl_ThinInstanceView2 after{};
    ASSERT_EQ(bl_getThinInstances2(prototype, &after), BL_OK);
    EXPECT_EQ(after.base.version, before.base.version);
    EXPECT_EQ(after.base.count, 70u);
    EXPECT_EQ(after.base.matrices.data[3 * 16 + 12], 0.f);
    replacementColors.back() = .5f;
    const auto allocate = runtime->runtime->allocator.allocate;
    runtime->runtime->allocator.allocate = [](void*, size_t, size_t) -> void* { return nullptr; };
    const auto failed = bl_writeThinInstanceData(prototype, &update);
    runtime->runtime->allocator.allocate = allocate;
    EXPECT_EQ(failed, BL_OUT_OF_MEMORY);
    ASSERT_EQ(bl_getThinInstances2(prototype, &after), BL_OK);
    EXPECT_EQ(after.base.version, before.base.version);
    EXPECT_EQ(after.base.count, 70u);
    EXPECT_EQ(after.base.matrices.data[3 * 16 + 12], 0.f);
    ASSERT_EQ(bl_writeThinInstanceData(prototype, &update), BL_OK);
    ASSERT_EQ(bl_getThinInstances2(prototype, &after), BL_OK);
    EXPECT_EQ(after.base.matrices.data, before.base.matrices.data);
    EXPECT_EQ(after.base.matrices.data[3 * 16 + 12], .25f);
    EXPECT_EQ(after.base.count, 4u);
    ASSERT_EQ(bl_renderFrame(engine, 0), BL_OK);
    ASSERT_TRUE(readBuffer(state->args, args.data(), sizeof(args)));
    EXPECT_EQ(args[1], 4u);
    EXPECT_EQ(nativeMesh->instanceBufferIndex, instanceBuffer);
    EXPECT_EQ(state->visible.idx, buffer);

    bl_GeometryData geometryData{};
    ASSERT_EQ(bl_createBoxData(runtime->runtime, nullptr, &geometryData), BL_OK);
    geometryData.indices[0] = 1;
    const bl_MeshGeometry geometry{{geometryData.positions, geometryData.vertexCount * 3},
        {geometryData.normals, geometryData.vertexCount * 3},
        {geometryData.indices, geometryData.indexCount},
        {geometryData.uvs, geometryData.vertexCount * 2}, {}, {}, {}};
    const auto indexBuffer = nativeMesh->geometry.indexBuffer.idx;
    ASSERT_EQ(bl_updateMeshGeometry(engine, prototype, &geometry), BL_OK);
    EXPECT_EQ(nativeMesh->geometry.indexBuffer.idx, indexBuffer);
    std::array<uint32_t, 36> indices{};
    ASSERT_TRUE(readBuffer(nativeMesh->geometry.indexBuffer, indices.data(), sizeof(indices)));
    EXPECT_EQ(std::memcmp(indices.data(), geometryData.indices, sizeof(indices)), 0);
    ASSERT_EQ(bl_resizeMeshGeometry(engine, prototype, &geometry), BL_OK);
    EXPECT_NE(nativeMesh->geometry.indexBuffer.idx, indexBuffer);
    ASSERT_TRUE(readBuffer(nativeMesh->geometry.indexBuffer, indices.data(), sizeof(indices)));
    EXPECT_EQ(std::memcmp(indices.data(), geometryData.indices, sizeof(indices)), 0);
    ASSERT_EQ(bl_freeGeometryData(runtime->runtime, &geometryData), BL_OK);
    EXPECT_EQ(bl_setComputeShaderCompiler(runtime->runtime, &compute), BL_BUSY);
    ASSERT_EQ(bl_removeFromScene(scene, node), BL_OK);
    EXPECT_EQ(nativeScene->thinCulls, nullptr);
    EXPECT_EQ(bl_setThinInstanceMatrix(prototype, 0, &moved), BL_DISPOSED);
}

TEST_F(LiteNativeGraphics, M1FloatClearPreservesGpuConversionAndPaletteLeaseIsStable)
{
    ASSERT_EQ(bl_setNativeClearPaletteIndex(engine, 0), BL_OK);
    bl_SceneProperties properties{};
    ASSERT_EQ(bl_getSceneProperties(scene, &properties), BL_OK);
    properties.clearColor = {.06, .07, .1, 1};
    ASSERT_EQ(bl_setSceneProperties(scene, &properties), BL_OK);
    ASSERT_EQ(bl_registerScene(scene), BL_OK);
    EXPECT_EQ(bl_setNativeClearPaletteIndex(engine, 0), BL_OK);
    EXPECT_EQ(bl_setNativeClearPaletteIndex(engine, 1), BL_BUSY);
    ASSERT_EQ(bl_renderFrame(engine, 0), BL_OK);
    ASSERT_TRUE(Readback());
    const auto actual = pixels;

    const float originalClear[]{.06f, .07f, .1f, 1.f};
    bgfx::setPaletteColor(1, originalClear);
    bgfx::setViewFrameBuffer(16, target);
    bgfx::setViewRect(16, 0, 0, 64, 64);
    bgfx::setViewClear(16, BGFX_CLEAR_COLOR, 0.f, uint8_t{0}, uint8_t{1});
    bgfx::touch(16);
    ASSERT_TRUE(Readback());
    EXPECT_EQ(actual, pixels);
}

TEST_F(LiteNativeGraphics, M1InstanceColorClampsAfterMultiplicationOnHdrTargets)
{
    auto native = reinterpret_cast<L_Engine*>(l_peek(runtime->runtime, engine._id))->native;
    const auto hdr = bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT);
    const auto readback = bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::RGBA16F,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    const bgfx::TextureHandle attachments[]{hdr, depth};
    const auto framebuffer = bgfx::createFrameBuffer(2, attachments, false);
    ASSERT_TRUE(bgfx::isValid(hdr) && bgfx::isValid(readback) && bgfx::isValid(framebuffer));
    native.target.firstViewId = 8;
    bl_EngineContext hdrEngine{};
    ASSERT_EQ(bl_createEngine(runtime->runtime, &native, nullptr, &hdrEngine), BL_OK);
    ASSERT_EQ(bl_setNativeClearPaletteIndex(hdrEngine, 2), BL_OK);
    native.target.framebufferIndex = framebuffer.idx;
    native.target.colorFormat = BL_COLOR_RGBA16F;
    ASSERT_EQ(bl_setNativeTarget(hdrEngine, &native.target), BL_OK);
    bl_SceneContext hdrScene{};
    ASSERT_EQ(bl_createSceneContext(hdrEngine, &hdrScene), BL_OK);
    bl_SceneProperties sceneProperties{};
    ASSERT_EQ(bl_getSceneProperties(scene, &sceneProperties), BL_OK);
    ASSERT_EQ(bl_setSceneProperties(hdrScene, &sceneProperties), BL_OK);
    bl_Mesh prototype{};
    ASSERT_EQ(bl_createBox(hdrEngine, nullptr, &prototype), BL_OK);
    bl_StandardMaterial standard{};
    ASSERT_EQ(bl_createStandardMaterial(runtime->runtime, &standard), BL_OK);
    bl_StandardMaterialProperties properties{};
    ASSERT_EQ(bl_getStandardMaterialProperties(standard, &properties), BL_OK);
    properties.disableLighting = true;
    properties.emissiveColor = {1, 1, 1};
    ASSERT_EQ(bl_setStandardMaterialProperties(standard, &properties), BL_OK);
    bl_MeshProperties2 meshProperties{};
    ASSERT_EQ(bl_standardMaterialAsMaterial(standard, &meshProperties.material), BL_OK);
    ASSERT_EQ(bl_setMeshProperties2(prototype, &meshProperties), BL_OK);
    const float matrix[]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float colors[]{-.5f, .25f, 2.f, 1.f};
    ASSERT_EQ(bl_setThinInstances(prototype, {matrix, 16}, 1), BL_OK);
    ASSERT_EQ(bl_setThinInstanceColors(prototype, {colors, 4}), BL_OK);
    bl_SceneNode node{};
    ASSERT_EQ(bl_meshNode(prototype, &node), BL_OK);
    ASSERT_EQ(bl_addToScene(hdrScene, node), BL_OK);
    ASSERT_EQ(bl_registerScene(hdrScene), BL_OK);
    ASSERT_EQ(bl_renderFrame(hdrEngine, 0), BL_OK);
    bgfx::TextureRegion destination{};
    destination.init(readback);
    bgfx::TextureRegion source{};
    source.init(hdr);
    bgfx::blit(readbackView, destination, source);
    std::array<uint16_t, 64 * 64 * 4> halfPixels{};
    const auto ready = bgfx::read(destination, halfPixels.data());
    bool complete = false;
    for (unsigned i = 0; i < 64 && !complete; ++i)
        complete = bgfx::frame() >= ready;
    ASSERT_TRUE(complete);
    const size_t center = (32 * 64 + 32) * 4;
    EXPECT_EQ(halfPixels[center], 0u);
    EXPECT_EQ(halfPixels[center + 1], 0x3400u);
    EXPECT_EQ(halfPixels[center + 2], 0x4000u);
    runtime.reset();
    bgfx::destroy(framebuffer);
    bgfx::destroy(readback);
    bgfx::destroy(hdr);
}

TEST_F(LiteNativeGraphics, M1FloatingOriginCullUsesTheOriginalAbsoluteCameraFrustum)
{
    EXPECT_EQ(bl_enableFloatingOrigin(engine), BL_INVALID_ARGUMENT);
    const auto compute = bl_computeShaderCompilerService();
    ASSERT_EQ(bl_setComputeShaderCompiler(runtime->runtime, &compute), BL_OK);
    auto native = reinterpret_cast<L_Engine*>(l_peek(runtime->runtime, engine._id))->native;
    native.target.firstViewId = 8;
    bl_EngineContext floating{};
    const bl_EngineOptions options{BL_BOOL_TRUE};
    ASSERT_EQ(bl_createEngine(runtime->runtime, &native, &options, &floating), BL_OK);
    ASSERT_EQ(bl_enableFloatingOrigin(floating), BL_OK);
    bl_SceneContext floatingScene{};
    ASSERT_EQ(bl_createSceneContext(floating, &floatingScene), BL_OK);
    bl_FreeCamera camera{};
    ASSERT_EQ(bl_createFreeCamera(runtime->runtime, {1000000, 0, -3}, {1000000, 0, 0}, &camera), BL_OK);
    bl_SceneProperties properties{};
    ASSERT_EQ(bl_getSceneProperties(floatingScene, &properties), BL_OK);
    properties.camera = camera;
    ASSERT_EQ(bl_setSceneProperties(floatingScene, &properties), BL_OK);
    bl_Mesh prototype{};
    ASSERT_EQ(bl_createBox(floating, nullptr, &prototype), BL_OK);
    bl_SceneNode node{};
    ASSERT_EQ(bl_meshNode(prototype, &node), BL_OK);
    ASSERT_EQ(bl_setNodePosition(node, {1000000, 0, 0}), BL_OK);
    bl_StandardMaterial standard{};
    ASSERT_EQ(bl_createStandardMaterial(runtime->runtime, &standard), BL_OK);
    bl_MeshProperties2 meshProperties{};
    ASSERT_EQ(bl_standardMaterialAsMaterial(standard, &meshProperties.material), BL_OK);
    ASSERT_EQ(bl_setMeshProperties2(prototype, &meshProperties), BL_OK);
    const float matrix[]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    ASSERT_EQ(bl_setThinInstances(prototype, {matrix, 16}, 1), BL_OK);
    ASSERT_EQ(bl_enableThinInstanceGpuCulling(prototype, true), BL_OK);
    ASSERT_EQ(bl_addToScene(floatingScene, node), BL_OK);
    ASSERT_EQ(bl_registerScene(floatingScene), BL_OK);
    ASSERT_EQ(bl_renderFrame(floating, 0), BL_OK);
    auto* internal = reinterpret_cast<L_Scene*>(l_peek(runtime->runtime, floatingScene._id));
    ASSERT_NE(internal->thinCulls, nullptr);
    std::array<uint32_t, 5> args{};
    bgfx::BufferRegion region{};
    region.init(internal->thinCulls->args, 0, sizeof(args));
    const auto ready = bgfx::read(region, args.data());
    bool complete = false;
    for (unsigned i = 0; i < 64 && !complete; ++i)
        complete = bgfx::frame() >= ready;
    ASSERT_TRUE(complete);
    EXPECT_EQ(args[1], 1u);
}

TEST_F(LiteNativeGraphics, M1CloneRemovalChurnReusesRegistryStorageAndRejectsRetiredIdentities)
{
    bl_Mesh source{};
    ASSERT_EQ(bl_createBox(engine, nullptr, &source), BL_OK);
    bl_TransformNode node{};
    ASSERT_EQ(bl_meshNode(source, &node), BL_OK);
    bl_Mat4 matrix{};
    ASSERT_EQ(bl_createIdentityMat4(&matrix), BL_OK);
    std::array<float, 16> values{};
    for (size_t i = 0; i < 16; ++i)
        values[i] = static_cast<float>(matrix.values[i]);
    ASSERT_EQ(bl_setThinInstances(source, {values.data(), values.size()}, 1), BL_OK);
    ASSERT_EQ(bl_registerScene(scene), BL_OK);
    size_t slots{};
    for (unsigned iteration = 0; iteration < 128; ++iteration)
    {
        bl_TransformNode clone{};
        ASSERT_EQ(bl_cloneTransformNode(node, &clone), BL_OK);
        bl_Mesh clonedMesh{};
        ASSERT_EQ(bl_sceneNodeAsMesh(clone, &clonedMesh), BL_OK);
        ASSERT_EQ(bl_addToScene(scene, clone), BL_OK);
        ASSERT_EQ(bl_renderFrame(engine, 0), BL_OK);
        ASSERT_EQ(bl_removeFromScene(scene, clone), BL_OK);
        EXPECT_EQ(bl_setThinInstanceMatrix(clonedMesh, 0, &matrix), BL_DISPOSED);
        if (iteration == 15)
            slots = runtime->runtime->count;
        if (iteration >= 16)
            EXPECT_EQ(runtime->runtime->count, slots);
    }
}
