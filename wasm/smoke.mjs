import assert from "node:assert/strict";
import path from "node:path";
import { pathToFileURL } from "node:url";

const modulePath = path.resolve(process.argv[2] || "build-wasm/mahjong.js");
const moduleDirectory = path.dirname(modulePath);
const { default: createMahjongModule } = await import(pathToFileURL(modulePath));
const module = await createMahjongModule({
  locateFile: (file) => path.join(moduleDirectory, file),
});

assert.equal(module.engineVersion(), "0.9.9");
const invalidResponse = JSON.parse(module.analyzeJson("{}"));
assert.equal(invalidResponse.success, false);
assert.match(invalidResponse.err_msg, /schema|required|invalid/i);
console.log(`WASM JSON API smoke test passed (${module.engineVersion()}).`);
