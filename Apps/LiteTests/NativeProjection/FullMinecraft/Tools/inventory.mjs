import { createHash } from "node:crypto";
import { mkdir, readFile, readdir, writeFile } from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";

const [checkoutArgument, nativeArgument, outputArgument] = process.argv.slice(2);
if (!checkoutArgument || !nativeArgument || !outputArgument) throw new Error("Usage: inventory.mjs <bblitec> <BabylonNative> <output>");
const checkout = path.resolve(checkoutArgument);
const native = path.resolve(nativeArgument);
const output = path.resolve(outputArgument);
const { default: ts } = await import(pathToFileURL(path.join(checkout, "node_modules", "typescript", "lib", "typescript.js")));
const api = JSON.parse(await readFile(path.join(checkout, "upstream", "babylon-lite-api.json"), "utf8"));
const functionOwners = new Set(api.items.filter(item => item.kind === "function").map(item => item.owner));
const publicHeader = await readFile(path.join(native, "Core", "LiteLayer", "Include", "babylon_lite.h"), "utf8");
const publicFunctions = new Set([...publicHeader.matchAll(/^\s*bl_Status\s+(bl_[A-Za-z0-9_]+)\s*\(/gm)].map(match => match[1]));
const records = new Map();
const features = new Map();
const typesSource = ts.createSourceFile("types.ts", await readFile(path.join(checkout, "src", "compiler", "types.ts"), "utf8"),
    ts.ScriptTarget.Latest, true);
const declaredFeatures = new Set();
const featureType = typesSource.statements.find(statement => ts.isTypeAliasDeclaration(statement) && statement.name.text === "Feature");
function featureNames(node) {
    if (ts.isLiteralTypeNode(node) && ts.isStringLiteral(node.literal)) declaredFeatures.add(node.literal.text);
    ts.forEachChild(node, featureNames);
}
if (featureType) featureNames(featureType.type);
const hash = text => createHash("sha256").update(text).digest("hex");
let testFiles = 0;

function imports(text, file, line, origin) {
    const source = ts.createSourceFile(file, text, ts.ScriptTarget.Latest, true);
    for (const statement of source.statements) {
        if (!ts.isImportDeclaration(statement) || !ts.isStringLiteral(statement.moduleSpecifier) ||
            !["babylon-lite", "@babylonjs/lite"].includes(statement.moduleSpecifier.text)) continue;
        const clause = statement.importClause;
        if (!clause || clause.isTypeOnly || !clause.namedBindings || !ts.isNamedImports(clause.namedBindings)) continue;
        for (const element of clause.namedBindings.elements) {
            if (element.isTypeOnly) continue;
            const name = (element.propertyName ?? element.name).text;
            const entries = records.get(name) ?? [];
            entries.push({ file, line, origin });
            records.set(name, entries);
        }
    }
}

for (const name of (await readdir(path.join(checkout, "test"))).sort()) {
    if (!name.endsWith(".test.ts")) continue;
    ++testFiles;
    const file = `test/${name}`;
    const text = await readFile(path.join(checkout, "test", name), "utf8");
    const source = ts.createSourceFile(file, text, ts.ScriptTarget.Latest, true);
    imports(text, file, 1, "test-module-import");
    function visit(node) {
        if (ts.isStringLiteral(node) || ts.isNoSubstitutionTemplateLiteral(node)) {
            const line = source.getLineAndCharacterOfPosition(node.getStart(source)).line + 1;
            if (node.text.includes("import ") && (node.text.includes("babylon-lite") || node.text.includes("@babylonjs/lite"))) {
                imports(node.text, file, line, "static-test-application-literal");
            }
            if (declaredFeatures.has(node.text)) {
                const entries = features.get(node.text) ?? [];
                entries.push({ file, line });
                features.set(node.text, entries);
            }
        }
        ts.forEachChild(node, visit);
    }
    visit(source);
}

const demoImports = new Set();
async function inspectDemos(directory) {
    for (const entry of await readdir(directory, { withFileTypes: true })) {
        const file = path.join(directory, entry.name);
        if (entry.isDirectory()) await inspectDemos(file);
        else if (entry.name.endsWith(".ts")) {
            const source = ts.createSourceFile(file, await readFile(file, "utf8"), ts.ScriptTarget.Latest, true);
            for (const statement of source.statements) {
                if (!ts.isImportDeclaration(statement) || !ts.isStringLiteral(statement.moduleSpecifier) ||
                    statement.moduleSpecifier.text !== "babylon-lite") continue;
                const clause = statement.importClause;
                if (!clause || clause.isTypeOnly || !clause.namedBindings || !ts.isNamedImports(clause.namedBindings)) continue;
                for (const element of clause.namedBindings.elements) {
                    if (!element.isTypeOnly) demoImports.add((element.propertyName ?? element.name).text);
                }
            }
        }
    }
}
await inspectDemos(path.join(native, "Apps", "LiteTests", "LitePlayground", "JavaScript", "Demos"));
const imported = [...records].sort(([a], [b]) => a.localeCompare(b)).map(([name, sites]) => ({
    name, functionOwner: functionOwners.has(api.exports[name] ?? name),
    minecraftImport: demoImports.has(name),
    exactNamedC99Declaration: publicFunctions.has(`bl_${name}`) ? `bl_${name}` : null,
    status: publicFunctions.has(`bl_${name}`) ? "declaration-match-only-not-semantic-coverage" : "no-exact-current-C99-declaration",
    testSites: sites,
}));
const report = {
    scope: "Static imported API/function reach in existing test modules and complete static application literals; dynamic templates, fixtures and semantic parity are not inferred",
    packagePin: api.pin,
    packageExports: Object.keys(api.exports).length,
    packageFunctionOwners: functionOwners.size,
    currentC99DeclarationCount: publicFunctions.size,
    currentContractSha256: hash(publicHeader),
    scannedTestFiles: testFiles,
    importedExportsFound: imported.length,
    importedFunctionOwnersFound: imported.filter(entry => entry.functionOwner).length,
    beyondMinecraftFunctionOwnersFound: imported.filter(entry => entry.functionOwner && !entry.minecraftImport).length,
    exactDeclarationMatches: imported.filter(entry => entry.exactNamedC99Declaration).length,
    semanticOwnersProvenCovered: null,
    explanation: "98 C ABI helpers are not 1035 TypeScript function owners. Name availability, tested source reach, and executed native parity are distinct. Legacy native-babylon tests exercise the older fork layer, not this LiteLayer.",
    importedApis: imported,
    featureAssertionStrings: [...features].sort(([a], [b]) => a.localeCompare(b)).map(([name, sites]) => ({ name, sites })),
};
await mkdir(output, { recursive: true });
await writeFile(path.join(output, "tested-api-inventory.json"), JSON.stringify(report, null, 2) + "\n");
console.log(JSON.stringify({ testFiles, packageExports: report.packageExports, functionOwners: report.packageFunctionOwners,
    c99Declarations: report.currentC99DeclarationCount, importedApis: imported.length,
    beyondMinecraftFunctionOwners: report.beyondMinecraftFunctionOwnersFound, exactDeclarationMatches: report.exactDeclarationMatches }));
