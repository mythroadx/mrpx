const { test } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');
const vm = require('node:vm');

function setupFilesystem(query = {}, fetchManifest = async () => ({
    ok: true,
    json: async () => JSON.parse(readFileSync(join(__dirname, '..', 'assets', 'runtime', 'manifest.json'), 'utf8')),
})) {
    const files = [], dependencies = new Set();
    const Module = {};
    const context = vm.createContext({
        Module,
        GetQueryString: name => query[name] ?? null,
        fetch: fetchManifest,
        addRunDependency: name => dependencies.add(name),
        removeRunDependency: name => dependencies.delete(name),
        abort(message) { throw new Error(message); },
        FS: { mkdirTree() {}, createPreloadedFile(...args) { files.push(args); } },
    });
    vm.runInContext(readFileSync(join(__dirname, '..', 'wasm', 'web', 'filesystem.js'), 'utf8'), context);
    return { Module, files, dependencies, args: Array.from(Module.arguments) };
}

test('MRP loader preserves the default alias and supports named packages without duplicate preloads', async () => {
    async function load(query) {
        const runtime = setupFilesystem(query);
        await runtime.Module.preRun[0]();
        return runtime;
    }
    const defaults = await load({});
    assert.deepEqual(defaults.args, ['--mrp', 'dsm_gm.mrp']);
    assert.equal(defaults.files.find(args => args[1] === 'cfunction.ext')[2], './runtime/cfunction.ext');
    assert.equal(defaults.files.some(args => ['mpc.mrp', 'ydqtwo.mrp'].includes(args[1])), false);
    const legacy = await load({ f: 'custom.mrp' });
    assert.deepEqual(legacy.files.find(args => args[1] === 'dsm_gm.mrp').slice(0, 3),
        ['/mythroad', 'dsm_gm.mrp', 'custom.mrp']);
    for (const name of ['asm_thumb.mrp', 'mpc.mrp']) {
        const named = await load({ f: 'custom.mrp', name });
        assert.deepEqual(named.args, ['--mrp', name]);
        assert.equal(named.files.filter(args => args[1] === name).length, 1);
        assert.equal(named.files.find(args => args[1] === name)[2], 'custom.mrp');
    }
    assert.throws(() => setupFilesystem({ f: 'custom.mrp', name: '../escape.mrp' }),
        /Invalid MRP package name/);
});

test('runtime manifest blocks startup until files are scheduled for preloading', async () => {
    let finish;
    const runtime = setupFilesystem({}, () => new Promise(resolve => { finish = resolve; }));
    const preloading = runtime.Module.preRun[0]();
    assert.equal(runtime.dependencies.size, 1);
    assert.equal(runtime.files.length, 0);
    finish({ ok: true, json: async () => ({ files: ['cfunction.ext'] }) });
    await preloading;
    assert.equal(runtime.files.length, 1);
    assert.equal(runtime.dependencies.size, 0);
});

test('missing or malformed runtime manifests abort startup explicitly', async () => {
    for (const response of [
        { ok: false, status: 404 },
        { ok: true, json: async () => { throw new SyntaxError('Malformed JSON'); } },
    ]) {
        const runtime = setupFilesystem({}, async () => response);
        await assert.rejects(runtime.Module.preRun[0](), /Unable to preload runtime assets:.*(404|Malformed JSON)/);
        assert.equal(runtime.files.length, 0);
    }
});

function setup() {
    class Element extends EventTarget {
        value = '';
        disabled = false;
        hidden = false;
        style = {};
        dataset = {};
        textContent = '';
        scrollTop = 0;
        clientHeight = 100;
        scrollHeight = 100;
        classList = { add() {}, remove() {} };
        focus() {}
        select() {}
        setPointerCapture() {}
    }
    const elements = new Map();
    const document = Object.assign(new EventTarget(), {
        getElementById(id) {
            if (!elements.has(id)) elements.set(id, new Element());
            return elements.get(id);
        },
        hidden: false,
    });
    const window = new EventTarget();
    const events = [], inputs = [];
    let cancellations = 0;
    const Module = {
        HEAPU8: new Uint8Array(4096),
        _queuePhoneKey(pressed, key) { events.push([pressed, key]); return 0; },
        _malloc() { return 16; },
        _free() {},
        _submitEditText(pointer) {
            const end = Module.HEAPU8.indexOf(0, pointer);
            inputs.push(new TextDecoder().decode(Module.HEAPU8.subarray(pointer, end)));
            window.vmrpEditChanged(null, '', 0, 0);
        },
        _cancelEdit() { cancellations++; window.vmrpEditChanged(null, '', 0, 0); },
    };
    const context = vm.createContext({ window, document, Module, TextEncoder });
    // Browser globals also appear as properties on window.
    for (const name of ['vmrpConsoleLog', 'vmrpEditChanged']) {
        Object.defineProperty(context, name, { get: () => window[name] });
    }
    vm.runInContext(readFileSync(join(__dirname, '..', 'wasm', 'web', 'controls.js'), 'utf8'), context);
    function fire(id, type, properties = {}) {
        const event = new Event(type, { cancelable: true });
        Object.assign(event, { button: 0, pointerId: 1 }, properties);
        document.getElementById(id).dispatchEvent(event);
    }
    return { window, document, events, inputs, fire, get cancellations() { return cancellations; } };
}

test('all phone keys pair press/release, cancellation never doubles release', () => {
    const ui = setup();
    ui.window.vmrpControlsReady(true);
    const ids = ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'star',
        'pound', 'up', 'down', 'left', 'right', 'power', 'sleft', 'sright', 'send', 'ok'];
    ids.forEach((id, code) => {
        ui.fire('key_' + id, 'pointerdown');
        ui.fire('key_' + id, 'pointerdown', { pointerId: 2 });
        ui.fire('key_' + id, 'pointerup', { pointerId: 2 });
        ui.fire('key_' + id, 'pointercancel');
        ui.fire('key_' + id, 'lostpointercapture');
        assert.deepEqual(ui.events.slice(-2), [[1, code], [0, code]]);
    });
    assert.equal(ui.events.length, 42);
});

test('blur and modal dialogs release held keys and block new presses', () => {
    const ui = setup();
    assert.equal(ui.document.getElementById('import').disabled, true);
    ui.fire('key_1', 'pointerdown');
    assert.equal(ui.events.length, 0);
    ui.window.vmrpControlsReady(true);
    assert.equal(ui.document.getElementById('import').disabled, false);
    assert.equal(ui.document.getElementById('runtimeState').dataset.state, 'ready');
    ui.fire('key_1', 'pointerdown');
    ui.window.dispatchEvent(new Event('blur'));
    assert.deepEqual(ui.events, [[1, 1], [0, 1]]);
    ui.fire('key_2', 'keydown', { key: ' ', repeat: false });
    ui.fire('key_2', 'keydown', { key: ' ', repeat: true });
    ui.window.vmrpSetModal(true);
    assert.equal(ui.document.getElementById('runtimeState').textContent, '对话框');
    ui.fire('key_2', 'keyup', { key: ' ' });
    ui.fire('key_3', 'pointerdown');
    assert.deepEqual(ui.events.slice(2), [[1, 2], [0, 2]]);
});

test('edit input transfers UTF-8, masks passwords and clears fields after completion', () => {
    const ui = setup();
    ui.window.vmrpControlsReady(true);
    ui.window.vmrpEditChanged('Title', 'initial', 0, 10);
    assert.equal(ui.document.getElementById('runtimeState').textContent, '等待输入');
    assert.equal(ui.document.getElementById('key_1').disabled, true);
    ui.document.getElementById('consoleInput').value = '中文\nA';
    ui.fire('consoleSubmit', 'click');
    assert.deepEqual(ui.inputs, ['中文\nA']);
    assert.equal(ui.document.getElementById('consoleInput').value, '');
    assert.equal(ui.document.getElementById('runtimeState').textContent, '运行中');
    ui.window.vmrpEditChanged('Password', 'secret', 2, 10);
    assert.equal(ui.document.getElementById('consoleInput').hidden, true);
    assert.equal(ui.document.getElementById('consolePassword').hidden, false);
    ui.fire('consoleCancel', 'click');
    assert.equal(ui.cancellations, 1);
    assert.equal(ui.document.getElementById('consolePassword').value, '');
    assert.equal(ui.document.getElementById('consoleOutput').value.includes('secret'), false);
});

test('NUL and unsupported Unicode are rejected; log output stays bounded plain text', () => {
    const ui = setup();
    ui.window.vmrpControlsReady(true);
    ui.window.vmrpEditChanged('Input', '', 0, 20);
    for (const invalid of ['a\0b', '\ud800', '\ud83d\ude00']) {
        ui.document.getElementById('consoleInput').value = invalid;
        ui.fire('consoleSubmit', 'click');
    }
    assert.equal(ui.inputs.length, 0);
    ui.window.vmrpConsoleLog('x'.repeat(70000));
    ui.window.vmrpConsoleLog('<img src=x onerror=alert(1)>');
    const output = ui.document.getElementById('consoleOutput');
    assert.equal(output.value.length, 65536);
    assert.match(output.value, /<img src=x onerror=alert\(1\)>/);
    ui.fire('consoleClear', 'click');
    assert.equal(output.value, '');
});
