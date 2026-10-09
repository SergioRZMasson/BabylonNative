namespace
{
    bl_Status ProceduralMesh(unsigned family,bl_EngineContext engine,bl_Mesh* mesh)
    {
        const bl_Vec3 path[]{{-.4,0,0},{0,.2,.2},{.4,0,0}};
        const bl_Vec3 shape[]{{-.2,-.2,0},{.2,-.2,0},{.2,.2,0},{-.2,.2,0},{-.2,-.2,0}};
        const bl_Vec3 bottom[]{{-.5,-.5,0},{.5,-.5,0}};
        const bl_Vec3 top[]{{-.5,.5,0},{.5,.5,0}};
        const bl_Vec3Span rows[]{{bottom,2},{top,2}};
        const bl_RibbonOptions ribbon{{rows,2},false,false,{}};
        const bl_TubeOptions tube{{path,3},{true,.2},{true,8},BL_CAP_ALL,{}};
        const bl_ExtrudeShapeOptions extrude{{shape,5},{path,3},{},{},BL_CAP_ALL};
        switch(family)
        {
            case 0:return bl_createCylinder(engine,nullptr,mesh);
            case 1:return bl_createPlane(engine,nullptr,mesh);
            case 2:return bl_createDisc(engine,nullptr,mesh);
            case 3:return bl_createPolyhedron(engine,nullptr,mesh);
            case 4:return bl_createRibbon(engine,&ribbon,mesh);
            case 5:return bl_createTube(engine,&tube,mesh);
            case 6:return bl_createExtrudeShape(engine,&extrude,mesh);
            default:return BL_INVALID_ARGUMENT;
        }
    }
}

TEST_F(LiteNativeGraphics, SevenProceduralFactoriesIndependenceUpdatesRangesCapacityAndRetirement)
{
    CreateColoredBox();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(bl_setNodeVisible(meshNode,false),BL_OK);
    const char* names[]{"cylinder","plane","disc","polyhedron","ribbon","tube","extrude"};
    for(unsigned family=0;family<7;++family)
    {
        SCOPED_TRACE(family);
        bl_Mesh first{};
        bl_Mesh second{};
        ASSERT_EQ(ProceduralMesh(family,engine,&first),BL_OK);
        ASSERT_EQ(ProceduralMesh(family,engine,&second),BL_OK);
        auto* a=reinterpret_cast<L_Mesh*>(l_peek(runtime->runtime,first._id));
        auto* b=reinterpret_cast<L_Mesh*>(l_peek(runtime->runtime,second._id));
        ASSERT_NE(a,nullptr);
        ASSERT_NE(b,nullptr);
        EXPECT_NE(a->geometry.streams[0],b->geometry.streams[0]);
        EXPECT_NE(a->geometry.vertexBuffer.idx,b->geometry.vertexBuffer.idx);
        EXPECT_NE(a->geometry.indexBuffer.idx,b->geometry.indexBuffer.idx);
        bl_SceneNode node{};
        bl_SceneNode secondNode{};
        ASSERT_EQ(bl_meshNode(first,&node),BL_OK);
        ASSERT_EQ(bl_meshNode(second,&secondNode),BL_OK);
        bl_String name{};
        ASSERT_EQ(bl_getNodeName(node,&name),BL_OK);
        EXPECT_EQ(std::string(name.data,name.length),names[family]);
        EXPECT_EQ(a->node.sceneCount,0u);
        EXPECT_EQ(a->material._id,0u);
        const auto originalVertex=a->geometry.vertexBuffer.idx;
        const auto originalIndex=a->geometry.indexBuffer.idx;
        const auto originalMinimum=a->geometry.minimum;
        std::vector<float> positions(a->geometry.streams[0],a->geometry.streams[0]+a->geometry.vertices*3);
        std::vector<float> normals(a->geometry.streams[1],a->geometry.streams[1]+a->geometry.vertices*3);
        std::vector<float> uvs(a->geometry.streams[2],a->geometry.streams[2]+a->geometry.vertices*2);
        std::vector<uint32_t> indices(a->geometry.indices,a->geometry.indices+a->geometry.indexCount);
        for(size_t index=0;index<positions.size();index+=3)positions[index]+=.125f;
        const size_t one=1;
        ASSERT_EQ(bl_updateMeshPositions(engine,first,{positions.data(),positions.size()},0,&one,0),BL_OK);
        EXPECT_EQ(a->geometry.minimum.x,originalMinimum.x);
        EXPECT_NE(a->geometry.streams[0][0],positions[0]);
        const float oldNormal=a->geometry.streams[1][0];
        normals[0]=.25f;
        ASSERT_EQ(bl_updateMeshNormals(engine,first,{normals.data(),normals.size()},0,&one,0),BL_OK);
        EXPECT_EQ(a->geometry.streams[1][0],oldNormal);
        uvs[0]=.25f;
        ASSERT_EQ(bl_updateMeshUvs(engine,first,{uvs.data(),uvs.size()},0,&one,0),BL_OK);
        bl_MeshGeometry geometry{{positions.data(),positions.size()},{normals.data(),normals.size()},
            {indices.data(),indices.size()},{uvs.data(),uvs.size()},{},{},{}};
        ASSERT_EQ(bl_updateMeshGeometry(engine,first,&geometry),BL_OK);
        EXPECT_NEAR(a->geometry.minimum.x,originalMinimum.x+.125,1e-6);
        EXPECT_EQ(a->geometry.vertexBuffer.idx,originalVertex);
        EXPECT_EQ(a->geometry.indexBuffer.idx,originalIndex);
        EXPECT_EQ(b->geometry.minimum.x,originalMinimum.x);
        const bl_GeometryRange vertexRange{0,1};
        const bl_GeometryRange indexRange{0,3};
        const bl_GeometryUpdateRanges ranges{&vertexRange,1,&indexRange,1};
        bl_GeometryCapacityResult capacity{};
        ASSERT_EQ(bl_updateMeshGeometryCapacity(engine,first,&geometry,nullptr,&ranges,&capacity),BL_OK);
        EXPECT_TRUE(capacity.stable);
        EXPECT_EQ(a->geometry.vertexBuffer.idx,originalVertex);
        const size_t vertices=a->geometry.vertices;
        const std::array<float,3> extraPosition{positions[0],positions[1],positions[2]};
        const std::array<float,3> extraNormal{normals[0],normals[1],normals[2]};
        const std::array<float,2> extraUv{uvs[0],uvs[1]};
        positions.insert(positions.end(),extraPosition.begin(),extraPosition.end());
        normals.insert(normals.end(),extraNormal.begin(),extraNormal.end());
        uvs.insert(uvs.end(),extraUv.begin(),extraUv.end());
        geometry.positions={positions.data(),positions.size()};
        geometry.normals={normals.data(),normals.size()};
        geometry.uvs={uvs.data(),uvs.size()};
        const bl_OptionalNumber reserve{true,1.5};
        ASSERT_EQ(bl_updateMeshGeometryCapacity(engine,first,&geometry,&reserve,nullptr,&capacity),BL_OK);
        EXPECT_FALSE(capacity.stable);
        EXPECT_EQ(a->geometry.vertices,vertices+1);
        EXPECT_GE(capacity.vertexCapacity,vertices+1);
        EXPECT_NE(a->geometry.vertexBuffer.idx,originalVertex);
        const auto grownVertex=a->geometry.vertexBuffer.idx;
        const bl_GeometryUpdateRanges emptyRanges{};
        ASSERT_EQ(bl_updateMeshGeometryCapacity(engine,first,&geometry,&reserve,&emptyRanges,&capacity),BL_OK);
        EXPECT_TRUE(capacity.stable);
        EXPECT_EQ(a->geometry.vertexBuffer.idx,grownVertex);
        ASSERT_EQ(bl_resizeMeshGeometry(engine,first,&geometry),BL_OK);
        EXPECT_EQ(a->node.record.id,first._id);
        EXPECT_NE(a->geometry.vertexBuffer.idx,grownVertex);
        bl_MeshProperties properties{};
        properties.material=material;
        ASSERT_EQ(bl_setMeshProperties(first,&properties),BL_OK);
        ASSERT_EQ(bl_setMeshProperties(second,&properties),BL_OK);
        ASSERT_EQ(bl_addToScene(scene,node),BL_OK);
        bl_SceneContext other{};
        ASSERT_EQ(bl_createSceneContext(engine,&other),BL_OK);
        ASSERT_EQ(bl_addToScene(other,node),BL_OK);
        ASSERT_EQ(bl_renderFrame(engine,16),BL_OK);
        ASSERT_TRUE(Readback());
        ASSERT_EQ(bl_removeFromScene(scene,node),BL_OK);
        bl_Vec3 position{};
        EXPECT_EQ(bl_getNodePosition(node,&position),BL_OK);
        ASSERT_EQ(bl_removeFromScene(other,node),BL_OK);
        EXPECT_EQ(bl_getNodePosition(node,&position),BL_DISPOSED);
        EXPECT_EQ(bl_addToScene(scene,node),BL_DISPOSED);
        ASSERT_EQ(bl_waitForGpuResourceRetirements(engine),BL_OK);
        ASSERT_EQ(bl_disposeNode(secondNode),BL_OK);
        ASSERT_EQ(bl_disposeScene(other),BL_OK);
    }
}

TEST_F(LiteNativeGraphics, SevenProceduralFactoryAllocationFailuresAreAtomicAndNamesUnpublished)
{
    struct Failure{bl_Allocator original;size_t calls{};size_t fail{};};
    const auto live=[&]{
        size_t count{};
        for(size_t index=0;index<runtime->runtime->count;++index)
        {
            const auto* record=runtime->runtime->records[index];
            count+=record && !record->disposed;
        }
        return count;
    };
    for(unsigned family=0;family<7;++family)
    {
        SCOPED_TRACE(family);
        bool completed=false;
        for(size_t failure=1;failure<=48;++failure)
        {
            const size_t before=live();
            Failure state{runtime->runtime->allocator,0,failure};
            runtime->runtime->allocator={&state,
                [](void* user,size_t bytes,size_t alignment)->void*{
                    auto& value=*static_cast<Failure*>(user);
                    if(++value.calls==value.fail)return nullptr;
                    return value.original.allocate(value.original.userData,bytes,alignment);
                },
                [](void* user,void* memory,size_t bytes,size_t alignment){
                    const auto& value=*static_cast<Failure*>(user);
                    value.original.deallocate(value.original.userData,memory,bytes,alignment);
                }};
            bl_Mesh sentinel{nullptr,987};
            const auto status=ProceduralMesh(family,engine,&sentinel);
            runtime->runtime->allocator=state.original;
            if(state.calls>=failure)
            {
                EXPECT_EQ(status,BL_OUT_OF_MEMORY);
                EXPECT_EQ(sentinel._runtime,nullptr);
                EXPECT_EQ(sentinel._id,987u);
                EXPECT_EQ(live(),before);
            }
            else
            {
                ASSERT_EQ(status,BL_OK);
                bl_SceneNode node{};
                ASSERT_EQ(bl_meshNode(sentinel,&node),BL_OK);
                ASSERT_EQ(bl_disposeNode(node),BL_OK);
                EXPECT_EQ(live(),before);
                completed=true;
                break;
            }
        }
        EXPECT_TRUE(completed);
    }
}

TEST_F(LiteNativeGraphics, OriginalSafeEmptyAndLowCountFactoriesAndForeignEngineGuards)
{
    bl_DiscOptions disc{{},{true,.5},{true,.7}};
    bl_Mesh emptyIndices{};
    ASSERT_EQ(bl_createDisc(engine,&disc,&emptyIndices),BL_OK);
    auto* data=reinterpret_cast<L_Mesh*>(l_peek(runtime->runtime,emptyIndices._id));
    ASSERT_NE(data,nullptr);
    EXPECT_EQ(data->geometry.vertices,2u);
    EXPECT_EQ(data->geometry.indexCount,0u);
    const bl_Vec3 point{0,0,0};
    const bl_ExtrudeShapeOptions extrude{{nullptr,0},{&point,1},{},{},BL_CAP_NONE};
    bl_Mesh empty{};
    ASSERT_EQ(bl_createExtrudeShape(engine,&extrude,&empty),BL_OK);
    auto* blank=reinterpret_cast<L_Mesh*>(l_peek(runtime->runtime,empty._id));
    EXPECT_EQ(blank->geometry.vertices,0u);
    EXPECT_EQ(blank->geometry.indexCount,0u);
    bl_Mesh sentinel{nullptr,987};
    EXPECT_EQ(bl_createDisc({engine._runtime,empty._id},nullptr,&sentinel),BL_INVALID_HANDLE);
    EXPECT_EQ(sentinel._id,987u);
    EXPECT_EQ(bl_createTube(engine,nullptr,&sentinel),BL_INVALID_ARGUMENT);
    EXPECT_EQ(bl_createCylinder(engine,nullptr,nullptr),BL_INVALID_ARGUMENT);
    bl_EngineContext stale=engine;
    stale._id+=UINT64_C(1)<<32;
    EXPECT_EQ(bl_createPlane(stale,nullptr,&sentinel),BL_INVALID_HANDLE);
    EXPECT_EQ(sentinel._id,987u);
}
