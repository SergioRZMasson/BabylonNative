import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const [checkoutArgument, applicationArgument, outputArgument] = process.argv.slice(2);
assert(checkoutArgument && applicationArgument && outputArgument, "Usage: project.mjs <bblitec-checkout> <application.ts> <output>");
const checkout = path.resolve(checkoutArgument);
const application = path.resolve(applicationArgument);
const output = path.resolve(outputArgument);
const repository = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "..", "..");
const hash = data => createHash("sha256").update(data).digest("hex");

// bblitec resolves its pinned package from cwd. All output stays in this build.
process.chdir(checkout);
const { compileSource } = await import(pathToFileURL(path.join(checkout, "dist", "src", "compiler.js")));
const { cppTokens } = await import(pathToFileURL(path.join(checkout, "dist", "src", "compiler", "cpp-identifiers.js")));
const { default: ts } = await import(pathToFileURL(path.join(checkout, "node_modules", "typescript", "lib", "typescript.js")));
const source = await readFile(application, "utf8");
const syntax = ts.createSourceFile(application, source, ts.ScriptTarget.Latest, true);
const native = new Map();
const exports = new Map();

for (const statement of syntax.statements) {
    assert(ts.isFunctionDeclaration(statement) && statement.name, "The initial scalar application profile admits named functions only.");
    assert(statement.body, "External declarations need an explicit projection-required body.");
    const name = statement.name.text;
    const returns = statement.type?.getText(syntax);
    assert(["number", "void"].includes(returns), `Unsupported application result for ${name}.`);
    assert(statement.parameters.every(parameter => ts.isIdentifier(parameter.name) && parameter.type?.kind === ts.SyntaxKind.NumberKeyword &&
        !parameter.questionToken && !parameter.dotDotDotToken && !parameter.initializer), `Unsupported application ABI for ${name}.`);
    const mapping = ts.getJSDocTags(statement).find(tag => tag.tagName.text === "liteNative")?.comment;
    if (mapping) {
        assert(typeof mapping === "string" && mapping.startsWith("bl_"), `Invalid explicit C99 mapping for ${name}.`);
        assert(statement.body.statements.length === 1 && ts.isThrowStatement(statement.body.statements[0]),
            "Projection declarations must refuse execution, not provide fake implementations.");
        native.set(name, mapping);
    } else {
        assert(statement.modifiers?.some(modifier => modifier.kind === ts.SyntaxKind.ExportKeyword),
            `Unmapped private functions are outside this first projection profile: ${name}.`);
        exports.set(name, { returns: returns === "number" ? "double" : "void", parameters: statement.parameters.map(parameter => parameter.name.text) });
    }
}
assert(native.size && exports.size, "Application must reach both explicit C99 declarations and actual exported user code.");
const result = compileSource(source, { fileName: application, libraryName: "lite_projected_application" });
assert.deepEqual(result.manifest.features, ["core"], "This scalar test projection refuses renderer/engine lowering.");
assert.deepEqual([...result.cppFiles.keys()].sort(), ["include/lite_projected_application.hpp", "main.cpp"],
    "Only the user application's one source unit may enter this projection.");
assert.equal(result.manifest.shaderVariants.length, 0);
const tokens = [...cppTokens(result.cpp)];

function matching(index, opening, closing, sequence = tokens) {
    assert.equal(sequence[index].text, opening);
    let depth = 1;
    for (let next = index + 1; next < sequence.length; ++next) {
        if (sequence[next].text === opening) ++depth;
        if (sequence[next].text === closing && --depth === 0) return next;
    }
    throw new Error(`Unbalanced emitted C++ ${opening}.`);
}

const namespace = tokens.findIndex((token, index) => token.text === "namespace" && tokens[index + 1]?.text === "bblscene" && tokens[index + 2]?.text === "{");
assert(namespace >= 0, "Application namespace is missing.");
const namespaceEnd = matching(namespace + 2, "{", "}");
const removed = [];
const reached = new Set();
let depth = 1;
for (let index = namespace + 3; index < namespaceEnd; ++index) {
    const token = tokens[index];
    if (depth === 1 && native.has(token.text) && tokens[index + 1]?.text === "(") {
        const end = matching(index + 1, "(", ")");
        if (tokens[end + 1]?.text === "{") {
            const bodyEnd = matching(end + 1, "{", "}");
            assert(["double", "void"].includes(tokens[index - 1].text), "Only scalar ABI declarations can be substituted.");
            assert(!reached.has(token.text), "Duplicate projection definition.");
            reached.add(token.text);
            removed.push([tokens[index - 1].start, tokens[bodyEnd].end]);
            index = bodyEnd;
            continue;
        }
    }
    if (token.text === "{") ++depth;
    if (token.text === "}") --depth;
}
assert.deepEqual([...reached].sort(), [...native.keys()].sort(), "Every explicit projection slot must actually be reached.");
const applicationStart = tokens[namespace].start;
let projected = result.cpp.slice(applicationStart, tokens[namespaceEnd].end);
for (const [begin, end] of removed.reverse()) {
    projected = projected.slice(0, begin - applicationStart) + projected.slice(end - applicationStart);
}
const projectedTokens = [...cppTokens(projected)];
for (let index = 0; index < projectedTokens.length; ++index) {
    if (projectedTokens[index].text === "bbl" && projectedTokens[index + 1]?.text === "::") {
        const name = projectedTokens.slice(index, index + 5).map(token => token.text).join("");
        assert(["bbl::js::TraceVisitor", "bbl::js::make_error"].includes(name),
            `Non-scalar/generated-engine runtime dependency refused: ${name}`);
    }
}

const closures = new Map();
for (let index = namespaceEnd + 1; index < tokens.length; ++index) {
    if (tokens[index].text === "make_closure" && tokens[index + 1]?.text === "(") {
        const end = matching(index + 1, "(", ")");
        const values = tokens.slice(index + 2, end).map(token => token.text);
        assert.deepEqual([values[0], values[1], values[3], values[4], values[5], values[6], values[7]], ["bblscene", "::", "{", "}", ",", "bblscene", "::"],
            "Captured user environments require a separate supported semantics profile.");
        assert.equal(tokens[index - 5].text, "=");
        closures.set(tokens[index - 6].text, { environment: values[2], function: values[8] });
    }
}
const bindings = new Map();
for (let index = namespaceEnd + 1; index < tokens.length; ++index) {
    const token = tokens[index].text;
    if (token.startsWith("bbl_library_export_") && tokens[index + 1]?.text === "=" && tokens[index + 2]?.text === "std" &&
        tokens[index + 3]?.text === "::" && tokens[index + 4]?.text === "move") {
        bindings.set(token.slice("bbl_library_export_".length), closures.get(tokens[index + 6].text));
    }
}
let wrappers = "";
let declarations = "#pragma once\n\nnamespace LiteProjectedApplication\n{\n";
for (const [name, descriptor] of exports) {
    const closure = bindings.get(name);
    assert(closure, `Application export closure missing for ${name}.`);
    const signature = `${descriptor.returns} ${name}(${descriptor.parameters.map(parameter => `double ${parameter}`).join(", ")})`;
    declarations += `    ${signature};\n`;
    wrappers += `${signature}\n{\n    bblscene::${closure.environment} environment{};\n    ${descriptor.returns === "void" ? "" : "return "}bblscene::${closure.function}(environment${descriptor.parameters.map(parameter => `, ${parameter}`).join("")});\n}\n`;
}
declarations += "}\n";
const cpp = `// bblitec-transpiled test application with explicit C99 projection.\n#include "Application.h"\n#include "C99Projection.h"\n#include "UserSemantics.h"\n\n${projected}\n\nnamespace LiteProjectedApplication\n{\n${wrappers}}\n`;
assert(!cpp.includes("runtime.hpp") && !cpp.includes("pal.hpp") && !cpp.includes("HostWebGpu"), "Renderer/runtime code leaked into user projection.");
const publicHeader = await readFile(path.join(repository, "Core", "LiteLayer", "Include", "babylon_lite.h"), "utf8");
const headerTokens = [...cppTokens(publicHeader)];
for (const mapping of native.values()) {
    const [symbol, field] = mapping.split(".");
    assert(publicHeader.includes(symbol), `C99 mapping is absent from the public contract: ${mapping}`);
    if (field) {
        const record = headerTokens.findIndex((token, index) => token.text === "struct" && headerTokens[index + 1]?.text === symbol && headerTokens[index + 2]?.text === "{");
        assert(record >= 0, `Public C99 record is absent: ${symbol}`);
        const end = matching(record + 2, "{", "}", headerTokens);
        assert(headerTokens.slice(record + 3, end).some((token, index, values) => token.text === field &&
            [";", "["].includes(values[index + 1]?.text)), `Public C99 field is absent: ${mapping}`);
    }
}
await mkdir(output, { recursive: true });
await writeFile(path.join(output, "Application.cpp"), cpp);
await writeFile(path.join(output, "Application.h"), declarations);
const compiler = await readFile(path.join(checkout, "dist", "src", "compiler.js"));
const stamp = await readFile(path.join(checkout, "dist", ".build-stamp"));
const status = execFileSync("git", ["status", "--porcelain"], { cwd: checkout, encoding: "utf8" });
const adapter = await readFile(path.resolve(path.dirname(application), "C99Projection.cpp"));
await writeFile(path.join(output, "provenance.json"), JSON.stringify({
    scope: "Two scalar test applications, not full Minecraft or complete bblitec corpus",
    transpiler: { checkout, commit: execFileSync("git", ["rev-parse", "HEAD"], { cwd: checkout, encoding: "utf8" }).trim(),
        workingTreeStatusSha256: hash(status), compilerSha256: hash(compiler), buildStampSha256: hash(stamp) },
    application: { path: application, sha256: hash(source) },
    publicContractSha256: hash(publicHeader),
    c99AdapterSha256: hash(adapter),
    compiledApplicationSources: ["Application.cpp"],
    generatedEngineSourcesCompiled: [],
    bblitecRuntimeCompiled: false,
    ignoredTranspilerDependencySuggestions: { runtime: result.manifest.runtimeSources, engine: result.manifest.generatedSources },
    projectedNativeCalls: [...native].map(([sourceName, nativeName]) => ({ sourceName, nativeName })),
    applicationExports: [...exports.keys()],
}, null, 2) + "\n");
console.log(`Projected ${exports.size} actual bblitec test applications and ${native.size} explicit C99 mappings; zero generated engine/runtime objects.`);
