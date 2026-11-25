import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { existsSync } from "node:fs";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { setTimeout as sleep } from "node:timers/promises";

const base = process.argv[2] ?? process.env.BROWSER_URL ?? "http://127.0.0.1:18766";
const chrome = process.env.CHROME_PATH ?? (process.platform === "win32"
    ? "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe" : "google-chrome");
const profile = await mkdtemp(join(tmpdir(), "vmrp-browser-"));
const browser = spawn(chrome, [
    "--headless=new", "--remote-debugging-address=127.0.0.1", "--remote-debugging-port=0",
    `--user-data-dir=${profile}`, "--no-first-run", "--no-default-browser-check",
    "--disable-background-networking", "--disable-background-timer-throttling",
    "--disable-renderer-backgrounding", "--disable-backgrounding-occluded-windows",
    "--disable-extensions", "--mute-audio", "--window-size=1400,2400", "about:blank",
], { stdio: ["ignore", "ignore", "pipe"] });
let startupError;
let diagnostics = "";
browser.on("error", error => { startupError = error; });
browser.stderr.on("data", chunk => { diagnostics = (diagnostics + chunk).slice(-4000); });
const exited = new Promise(resolve => {
    browser.once("exit", resolve);
    browser.once("error", resolve);
});
let socket;
try {
    const portFile = join(profile, "DevToolsActivePort");
    const deadline = Date.now() + 30000;
    while (!existsSync(portFile) && Date.now() < deadline) {
        if (startupError) throw startupError;
        if (browser.exitCode !== null && browser.exitCode !== 0) throw new Error(`Browser exited: ${diagnostics}`);
        await sleep(100);
    }
    assert(existsSync(portFile), `Browser did not become ready: ${diagnostics}`);
    const port = (await readFile(portFile, "utf8")).split("\n")[0];
    const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    const target = targets.find(item => item.type === "page");
    assert(target, "Browser has no page target");
    socket = new WebSocket(target.webSocketDebuggerUrl);
    await once(socket, "open");
    let nextId = 0;
    const pending = new Map();
    socket.addEventListener("message", event => {
        const message = JSON.parse(event.data);
        const request = pending.get(message.id);
        if (!request) return;
        pending.delete(message.id);
        if (message.error) request.reject(new Error(JSON.stringify(message.error)));
        else request.resolve(message.result);
    });
    const call = (method, params = {}) => new Promise((resolve, reject) => {
        const id = ++nextId;
        pending.set(id, { resolve, reject });
        socket.send(JSON.stringify({ id, method, params }));
    });
    const evaluate = async expression => {
        const result = await call("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });
        if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
        return result.result.value;
    };
    await call("Page.enable");
    await call("Runtime.enable");
    await call("Emulation.setDeviceMetricsOverride", { width: 1400, height: 2400, deviceScaleFactor: 1, mobile: false });
    for (const [mode, page] of [["mrp", "/tests/wasm_mrp_browser_tests.html"],
        ["audio", "/tests/web_audio_browser_tests.html"], ["catalog", "/build/web/main.html"]]) {
        const url = `${base}${page}`;
        const response = await fetch(url);
        assert(response.ok, `Test HTTP server is not ready: ${response.status}`);
        let onLoad;
        const loaded = new Promise(resolve => {
            onLoad = event => {
                if (JSON.parse(event.data).method === "Page.loadEventFired") resolve();
            };
            socket.addEventListener("message", onLoad);
        });
        try {
            await call("Page.navigate", { url });
            await Promise.race([loaded, sleep(15000, undefined, { ref: false }).then(() => { throw new Error("Page load timed out"); })]);
        } finally {
            socket.removeEventListener("message", onLoad);
        }
        if (mode === "catalog") {
            const report = await evaluate(`(async () => {
                const search = document.querySelector('#catalogSearch');
                const filter = document.querySelector('#catalogFilter');
                const deadline = Date.now() + 15000;
                while (search.disabled && Date.now() < deadline)
                    await new Promise(resolve => setTimeout(resolve, 100));
                if (search.disabled) throw new Error(document.querySelector('#catalogState').textContent);
                const data = await (await fetch('./catalog/catalog.json')).json();
                if (!data.packages.length || data.summary.pending) throw new Error('Catalog generation is incomplete');
                const count = document.querySelectorAll('.catalog-card').length;
                if (count !== Math.min(24, data.packages.length)) throw new Error('Incorrect first page size');
                const expected = data.packages.find(entry => entry.preview);
                if (!expected) throw new Error('Catalog has no captured previews');
                search.value = expected.filename;
                search.dispatchEvent(new Event('input'));
                const link = document.querySelector('.catalog-download');
                if (!link || link.href !== expected.download_url) throw new Error('Search result links to the wrong package');
                const image = document.querySelector('.catalog-preview img');
                if (!image) throw new Error('Search result has no image');
                await image.decode();
                if (image.naturalWidth !== 240 || image.naturalHeight !== 320) throw new Error('Incorrect preview dimensions');
                search.value = 'mrpx-no-such-package-zzzz';
                search.dispatchEvent(new Event('input'));
                if (document.querySelector('#catalogEmpty').hidden) throw new Error('Empty state is missing');
                search.value = '';
                search.dispatchEvent(new Event('input'));
                const next = document.querySelector('#catalogNext');
                if (data.packages.length > 24) {
                    next.click();
                    if (!document.querySelector('#catalogPage').textContent.startsWith('2 /')) throw new Error('Pagination failed');
                }
                filter.value = 'previews';
                filter.dispatchEvent(new Event('change'));
                if ([...document.querySelectorAll('.catalog-card')].some(card => !card.querySelector('img')))
                    throw new Error('Preview filter includes missing screenshots');
                if (document.querySelector('a[download]')) throw new Error('Catalog must link externally, not claim local downloads');
                return {packages: data.packages.length, previews: data.summary.previews, firstPage: count};
            })()`);
            await call("Emulation.setDeviceMetricsOverride", { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
            assert(await evaluate("document.documentElement.scrollWidth <= innerWidth"), "Catalog overflows mobile viewport");
            console.log(`PASS catalog: ${JSON.stringify(report)}`);
            continue;
        }
        const ready = await evaluate(`(async () => {
            const deadline = Date.now() + 15000;
            while ((location.href !== ${JSON.stringify(url)} || !document.querySelector(${JSON.stringify(mode === "audio" ? "#run" : "#results")})) && Date.now() < deadline)
                await new Promise(resolve => setTimeout(resolve, 100));
            return location.href === ${JSON.stringify(url)} && !!document.querySelector(${JSON.stringify(mode === "audio" ? "#run" : "#results")});
        })()`);
        assert(ready, `${mode} page did not initialize`);
        if (mode === "audio") {
            const point = await evaluate(`(() => { const r = document.querySelector("#run").getBoundingClientRect(); return {x: r.x + r.width / 2, y: r.y + r.height / 2}; })()`);
            for (const type of ["mousePressed", "mouseReleased"])
                await call("Input.dispatchMouseEvent", { ...point, type, button: "left", clickCount: 1 });
        }
        const report = await evaluate(`(async () => {
            const deadline = Date.now() + 90000;
            while (!document.body.dataset.result && Date.now() < deadline)
                await new Promise(resolve => setTimeout(resolve, 100));
            return {status: document.body.dataset.result, text: document.querySelector(${JSON.stringify(mode === "audio" ? "#result" : "#results")}).textContent};
        })()`);
        assert.equal(report.status, "passed", JSON.stringify(report));
        const data = JSON.parse(report.text);
        if (mode === "mrp") {
            assert.deepEqual(data.map(item => item.name), ["launcher", "mpc", "arm", "thumb", "arm-key", "thumb-touch", "mpc-console"]);
            assert(data.every(item => item.passed));
        } else {
            assert(data.playing >= 0.001 && data.looping >= 0.001);
            assert(data.ended <= 0.000001 && data.stopped <= 0.000001);
        }
        console.log(`PASS ${mode}: ${JSON.stringify(data)}`);
    }
} finally {
    if (socket?.readyState === WebSocket.OPEN) {
        const closed = once(socket, "close");
        socket.send(JSON.stringify({ id: 999999, method: "Browser.close" }));
        await Promise.race([closed, sleep(10000, undefined, { ref: false }).then(() => { throw new Error("Browser debugging endpoint did not close"); })]);
    } else if (browser.exitCode === null && browser.pid) {
        browser.kill();
    }
    if (browser.pid) await Promise.race([exited, sleep(10000, undefined, { ref: false }).then(() => { throw new Error("Browser did not exit"); })]);
    await rm(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 });
}
