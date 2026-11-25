const { test } = require('node:test');
const assert = require('node:assert/strict');
const { validateCatalog, selectPage } = require('../wasm/web/catalog.js');

function entry(index, preview = null) {
    return {
        id: index.toString(16).padStart(16, '0'),
        sha256: 'a'.repeat(64),
        filename: `game${index}.mrp`,
        name: index === 1 ? '经典方块' : `Game ${index}`,
        vendor: '厂商',
        description: '经典游戏',
        appid: index,
        version: 1,
        size_bytes: 1024,
        resource_count: 3,
        errors: [], warnings: [],
        preview,
        capture_status: preview ? 'candidate' : 'runtime_error',
        download_url: `https://github.com/vmrp/vmrp.github.io/blob/master/public/mrps/game${index}.mrp`,
    };
}

test('catalog search combines metadata terms and filters before paginating', () => {
    const entries = Array.from({ length: 29 }, (_, index) => entry(index,
        index % 2 === 0 ? `previews/${'a'.repeat(64)}.png` : null));
    const selection = selectPage(entries, '厂商 GAME', 'previews', 2, 10);
    assert.equal(selection.total, 15);
    assert.equal(selection.page, 2);
    assert.equal(selection.pages, 2);
    assert.equal(selection.entries.length, 5);
    assert.deepEqual(selectPage(entries, '经典方块 １', 'unavailable', 99).entries.map(e => e.appid), [1]);
    const empty = selectPage(entries, 'not found', 'all', 99);
    assert.equal(empty.total, 0);
    assert.equal(empty.page, 1);
});

test('catalog rejects unsafe URLs, screenshot paths and startup screens presented as game previews', () => {
    const data = { schema_version: 1, summary: { total: 1 },
        source_url: 'https://github.com/vmrp/vmrp.github.io/tree/master/public/mrps', packages: [entry(1)] };
    assert.equal(validateCatalog(data), data);
    for (const patch of [
        { download_url: 'javascript:alert(1)' },
        { preview: '../../private.png', capture_status: 'candidate' },
        { preview: `previews/${'a'.repeat(64)}.png`, capture_status: 'startup_screen' },
        { capture_status: 'pretend_success' },
        { size_bytes: -1 },
    ]) {
        assert.throws(() => validateCatalog({ ...data, packages: [{ ...entry(1), ...patch }] }));
    }
    assert.throws(() => validateCatalog({ ...data, summary: { total: 2 } }));
});
