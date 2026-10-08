import { build } from "esbuild";
import ts from "typescript";
import { createHash } from "node:crypto";
import { readFile, readdir, mkdir, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const repo = path.resolve(root, "..", "..", "..", "..");
const adapter = path.join(repo, "Plugins", "LiteJSBinding", "JavaScript");
const demos = path.join(root, "Demos");
const outputArgument = process.argv.find(value => value.startsWith("--output="));
const output = outputArgument ? path.resolve(outputArgument.slice(9)) : path.join(root, "dist");
await mkdir(output, { recursive: true });

const nativePlugin = {
    name: "native-babylon-lite-contract",
    setup(builder) {
        builder.onResolve({ filter: /^babylon-lite$/ }, () => ({ path: path.join(adapter, "babylon-lite.js") }));
        builder.onResolve({ filter: /^babylon-lite\/shader\/wgsl\.js$/ }, () => ({ path: path.join(adapter, "wgsl.js") }));
        builder.onResolve({ filter: /^babylon-lite\// }, args => {
            throw new Error(`Unprojected original Babylon Lite application import: ${args.path}`);
        });
    },
};

async function sourceFiles(directory) {
    const result = [];
    for (const entry of await readdir(directory, { withFileTypes: true })) {
        const file = path.join(directory, entry.name);
        if (entry.isDirectory()) result.push(...await sourceFiles(file));
        else if (entry.name.endsWith(".ts")) result.push(file);
    }
    return result.sort();
}

const reached = new Map();
const hashes = {};
for (const file of await sourceFiles(demos)) {
    const source = await readFile(file, "utf8");
    const relative = path.relative(root, file).split(path.sep).join("/");
    hashes[relative] = createHash("sha256").update(source).digest("hex");
    const ast = ts.createSourceFile(file, source, ts.ScriptTarget.Latest, true);
    for (const statement of ast.statements) {
        if (!ts.isImportDeclaration(statement) || !ts.isStringLiteral(statement.moduleSpecifier)) continue;
        const module = statement.moduleSpecifier.text;
        if (module !== "babylon-lite" && module !== "babylon-lite/shader/wgsl.js") continue;
        const clause = statement.importClause;
        if (!clause || clause.isTypeOnly || !clause.namedBindings || !ts.isNamedImports(clause.namedBindings)) continue;
        for (const imported of clause.namedBindings.elements) {
            if (imported.isTypeOnly) continue;
            const name = imported.propertyName?.text ?? imported.name.text;
            const records = reached.get(name) ?? [];
            records.push(relative);
            reached.set(name, records);
        }
    }
}

const entries = [
    ["minecraft.native", path.join(demos, "minecraft.ts")],
    ["contract-tests.native", path.join(root, "Tests", "contract-tests.ts")],
    ["minecraft-shaders.native", path.join(root, "Tests", "minecraft-shaders.ts")],
    ["callback-throw.native", path.join(root, "Tests", "callback-throw.ts")],
    ["ui-contract.native", path.join(root, "Tests", "ui-contract.ts")],
    ["platform.native", path.join(root, "Platform", "platform.js")],
];
const bundles = {};
for (const [name, entry] of entries) {
    const result = await build({
        absWorkingDir: root,
        entryPoints: [entry],
        outfile: path.join(output, `${name}.js`),
        format: "iife",
        platform: "browser",
        target: "es2020",
        bundle: true,
        treeShaking: true,
        metafile: true,
        define: { "import.meta.url": JSON.stringify("app:///Demos/minecraft.ts") },
        plugins: [nativePlugin],
        logLevel: "warning",
    });
    const inputs = Object.keys(result.metafile.inputs);
    for (const input of inputs) {
        const absolute = path.resolve(root, input);
        if (!absolute.startsWith(demos + path.sep) && !absolute.startsWith(path.join(root, "Tests") + path.sep) &&
            !absolute.startsWith(path.join(root, "Platform") + path.sep) && !absolute.startsWith(adapter + path.sep)) {
            throw new Error(`Engine/runtime implementation leaked into application bundle: ${input}`);
        }
    }
    bundles[name] = { inputs, bytes: (await readFile(path.join(output, `${name}.js`))).byteLength };
}

const ignoredImports = new Set(["setDracoBaseUrl", "setMeshoptBaseUrl"]);
const coverage = {
    authority: "@babylonjs/lite 1.32.0 original TypeScript and complete original Minecraft demo",
    sourcePin: "2e064d88ec7422af946f8ec7f089ac6519f99295",
    generatedEngineIncluded: false,
    bblitecRuntimeIncluded: false,
    renderRadius: 6,
    scope: "Minecraft import reach inventory, not a claim that the demo or full 1814-export API passes",
    obligations: [...reached].sort(([a], [b]) => a.localeCompare(b)).map(([name, sources]) => ({
        name,
        sources,
        mapping: name === "wgsl" ? "template-string serialization only" :
            ignoredImports.has(name) ? "uncalled decoder helper; explicitly unsupported" : `bl_${name}`,
        verifiedByThisBundler: "application-only dependency boundary and import projection",
    })),
    runtimeValidation: "Separate LiteMinecraftFull180, LiteMinecraftReplay240 and LiteMinecraftNativeInputSaveLoad CTests; bundling itself is not runtime verification.",
    remainingScope: ["application-only native C99 projection", "complete bblitec corpus", "whole-package API coverage", "human audio audibility"],
    originalDemoFiles: hashes,
    bundles,
};
await writeFile(path.join(output, "coverage.json"), JSON.stringify(coverage, null, 2) + "\n");
console.log(`Built ${entries.length} application-only bundles; ${reached.size} import obligations; radius 6 unchanged; no engine TypeScript/C++ runtime bundled.`);
