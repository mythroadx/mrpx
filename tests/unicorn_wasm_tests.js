const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const path = require("node:path");
const test = require("node:test");

test("Unicorn 2.1.4 WASM executes the VMRP smoke program", () => {
    assert.ok(process.argv[2],
        "Usage: node tests\\unicorn_wasm_tests.js <compiled smoke.js>");
    const result = spawnSync(process.execPath, [path.resolve(process.argv[2])], {
        encoding: "utf8",
        timeout: 20000,
        maxBuffer: 1024 * 1024,
    });
    if (result.error) {
        throw result.error;
    }
    process.stdout.write(result.stdout);
    process.stderr.write(result.stderr);
    assert.equal(result.status, 0, `WASM exit status: ${result.status}`);
    assert.match(result.stdout, /^VMRP_UNICORN_WASM_OK\r?$/m);
});
