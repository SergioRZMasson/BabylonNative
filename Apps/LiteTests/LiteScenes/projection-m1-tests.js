import {
    createEngine, createSceneContext, createArcRotateCamera, createTransformNode, createBox,
    createSphereData, createTorusData, createGroundFromHeightMap, composeMat4,
    createIdentityMat4, createTranslationMat4, setParent, enableOrthographicCamera,
    disableOrthographicCamera, createGridMaterial, createLineMaterial, createLineSystem,
    updateLineSystem, setThinInstances, setThinInstanceMatrix, setThinInstanceColors,
    enableThinInstanceGpuCulling, setAlphaToCoverage, getAlphaToCoverage, getShaderUniform,
    addToScene, removeFromScene, registerScene, startEngine, createFreeCamera, attachFreeControl,
    cloneTransformNode,
    setThinInstanceCount, flushThinInstances,
} from "babylon-lite";

function check(value, message) {
    if (!value) throw Error(message);
}
function reject(callback) {
    let failed = false;
    try { callback(); } catch { failed = true; }
    check(failed, "Expected native rejection");
}

async function main() {
    const canvas = document.getElementById("renderCanvas");
    let floatingRejected=false;
    try { await createEngine(canvas,{useFloatingOrigin:true}); } catch { floatingRejected=true; }
    check(floatingRejected,"Floating origin without HPM did not preserve original error48");
    const engine = await createEngine(canvas, {useHighPrecisionMatrix: true, useFloatingOrigin: true});
    check(engine.msaaSamples === 4 && engine.useFloatingOrigin, "Creation/target metadata forwarding");
    const identity = createIdentityMat4();
    const translated = createTranslationMat4(5000000.123456, -2, 1);
    check(identity instanceof Float64Array && translated[12] === 5000000.123456,
        "Original HPM matrix storage was rounded prematurely");
    const matrix = composeMat4(1,2,3,0,0,0,1,-2,3,4);
    check(matrix[0] === -2 && matrix[12] === 1, "Compose forwarding");
    const sphere = createSphereData({segments: 3});
    check(sphere.vertexCount === sphere.positions.length / 3, "Sphere source vertexCount missing");
    const torus = createTorusData({tessellation: 3, diameter: 2});
    check(torus.positions.length === 48 && torus.indices.length === 96, "Torus stream forwarding");
    const parent = createTransformNode("parent", 7,3,1,0,0,0,1,-2,3,4);
    const child = createTransformNode("child",1,2,3);
    const before = Array.from(child.worldMatrix);
    setParent(child, parent);
    check(child.parent === parent && parent.children[0] === child, "World-preserving hierarchy links");
    check(child.worldMatrix.every((v,i) => Math.abs(v-before[i]) < 1e-12), "World preservation");
    setParent(child,parent);
    check(parent.children.length === 1, "Duplicate child created on repeated parenting");
    reject(() => setParent(parent,child));
    setParent(child,null);
    check(child.parent === null && parent.children.length === 0, "Detach forwarding");
    const camera = createArcRotateCamera(-Math.PI/2,Math.PI/2,10,{x:0,y:0,z:0});
    const ortho = enableOrthographicCamera(camera,{halfHeight:6});
    ortho.halfHeight=7;
    ortho.left=-4;
    check(camera.ortho===ortho && ortho.halfHeight===7 && ortho.left===-4, "Live ortho fields");
    reject(() => { ortho.halfHeight=0; });
    check(ortho.halfHeight===7, "Rejected bounds changed native state");
    const replacement=enableOrthographicCamera(camera,{halfHeight:4});
    ortho.halfHeight=9;
    check(replacement.halfHeight===4, "Replaced bounds still controlled projection");
    disableOrthographicCamera(camera);
    check(camera.ortho===null, "Perspective restore");
    enableOrthographicCamera(camera,{halfHeight:5});
    const grid=createGridMaterial({gridRatio:.5,majorUnitFrequency:3.6,mainColor:[.1,.2,.3],
        gridOffset:[1,2,3],visibility:.75,preMultiplyAlpha:true,opacity:.6,antialias:false,useMaxLine:true});
    const control=getShaderUniform(grid,"gridControl");
    check(control[0]===.5 && control[1]===4 && Math.abs(control[3]-.6)<1e-7, "Grid options/default UBO");
    check(getShaderUniform(grid,"visibility")===.75, "Grid visibility forwarding");
    reject(() => createLineMaterial({useThinInstanceColors:true}));
    const lineMaterial=createLineMaterial({useThinInstances:true,useThinInstanceColors:true,useVertexAlpha:false});
    const scene=createSceneContext(engine);
    scene.camera=camera;
    const free=createFreeCamera({x:0,y:0,z:-3},{x:0,y:0,z:0});
    const detach=attachFreeControl(free,canvas,scene);
    canvas.dispatchEvent({type:"keydown",code:"KeyW"});
    const lines=[[{x:-1,y:0,z:0},{x:1,y:0,z:0}]];
    const line=createLineSystem(engine,{lines,material:lineMaterial});
    updateLineSystem(engine,line,{lines:[[{x:-2,y:0,z:0},{x:2,y:0,z:0}]]});
    check(line.boundMin[0]===-2 && line.boundMax[0]===2, "Line CPU bounds were not refreshed");
    reject(() => updateLineSystem(engine,line,{lines:[[{x:0,y:0,z:0}]]}));
    const slab=new Float64Array(32);
    slab.set(identity); slab.set(createTranslationMat4(0,1,0),16);
    setThinInstances(line,slab,2);
    const colors=new Float32Array([1,0,0,1,0,1,0,1]);
    setThinInstanceColors(line,colors);
    check(line.thinInstances.matrices===slab && line.thinInstances.colors===colors &&
        line.thinInstances.count===2, "Thin slab/reference identity");
    const moved=createTranslationMat4(.1234567890123,1,0);
    setThinInstanceMatrix(line,1,moved);
    check(slab[28]===moved[12], "F64 matrix update lost precision or array identity");
    const retained=slab[28];
    reject(() => setThinInstanceMatrix(line,2,moved));
    check(slab[28]===retained, "Rejected instance write changed source slab");
    const thin=line.thinInstances;
    thin.count=3;
    reject(() => globalThis._liteFlushReferenceValues());
    thin.count=1;
    globalThis._liteFlushReferenceValues();
    setThinInstanceCount(line,2);
    check(thin===line.thinInstances && thin._capacity===2 && thin.count===2,
        "Dynamic count replaced the fixed-capacity thin data");
    slab[28]=NaN;
    thin._version++;
    reject(() => globalThis._liteFlushReferenceValues());
    slab[28]=retained;
    flushThinInstances(line);
    colors[0]=NaN;
    thin._colorVersion++;
    reject(() => globalThis._liteFlushReferenceValues());
    colors[0]=1;
    thin._colorVersion++;
    globalThis._liteFlushReferenceValues();
    line.parent=parent;
    parent.children.push(line);
    const cloned=cloneTransformNode(parent);
    const clonedLine=cloned.children[0];
    check(cloned!==parent && clonedLine!==line && clonedLine.parent===cloned,
        "Deep clone identities/typed children");
    check(clonedLine.material===line.material && clonedLine.thinInstances===line.thinInstances,
        "Clone lost original shared material/thin data identity");
    addToScene(scene,line);
    const prototype=createBox(engine);
    prototype.material=grid;
    enableThinInstanceGpuCulling(line,false);
    reject(() => enableThinInstanceGpuCulling(prototype));
    reject(() => setThinInstanceColors(camera,colors));
    setAlphaToCoverage(grid,true);
    check(getAlphaToCoverage(grid), "Alpha coverage native state");
    setAlphaToCoverage(grid,false);
    check(!getAlphaToCoverage(grid), "Alpha coverage disable");
    const removed=createBox(engine);
    addToScene(scene,removed);
    removeFromScene(scene,removed);
    reject(() => setThinInstances(removed,identity,1));
    let heightRejected=false;
    try { await createGroundFromHeightMap(engine,"missing-m1-heightmap.png",{}); } catch { heightRejected=true; }
    check(heightRejected, "Missing host image was silently substituted");
    await registerScene(scene);
    await startEngine(engine);
    check(free.position.z>-3,"Free-control delta/inertia hook was not native-forwarded");
    canvas.dispatchEvent({type:"keyup",code:"KeyW"});
    detach();
    detach();
    canvas.dataset.ready="true";
    console.log("M1 geometry, F64 matrices, parenting, ortho, lines/grid, instances, A2C forwarding passed.");
}
main().catch(error => {
    console.error(error);
    document.getElementById("renderCanvas").dataset.error=String(error);
});
