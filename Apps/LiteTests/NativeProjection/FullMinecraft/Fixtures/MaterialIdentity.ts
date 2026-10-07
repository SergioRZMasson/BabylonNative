import { createEngine, createSceneContext, createShaderMaterial, setShaderFloat } from "babylon-lite";

const engine = await createEngine({});
const scene = createSceneContext(engine);
const options = {
    vertexSource: "@vertex fn mainVertex(input: VertexInput) -> @builtin(position) vec4f { return vec4f(input.position, 1); }",
    fragmentSource: "@fragment fn mainFragment() -> @location(0) vec4f { return vec4f(shaderUniforms.amount); }",
    attributes: ["position"],
    uniforms: [{ name: "amount", type: "f32" }],
};
const first = createShaderMaterial(options);
const second = createShaderMaterial(options);
setShaderFloat(first, "amount", 0.25);
setShaderFloat(second, "amount", 0.75);
void scene;
