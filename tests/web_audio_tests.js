const { test } = require('node:test');
const assert = require('node:assert/strict');
const MidiPlayer = require('../wasm/web/midi.js');

function midiData(note = 60) {
    return Uint8Array.from([
        77, 84, 104, 100, 0, 0, 0, 6, 0, 0, 0, 1, 0, 96,
        77, 84, 114, 107, 0, 0, 0, 12,
        0, 0x90, note, 100, 96, 0x80, note, 0, 0, 0xff, 0x2f, 0,
    ]).buffer;
}

function setup(fetchSoundBank = async () => ({ ok: true, arrayBuffer: async () => new ArrayBuffer(8) })) {
    const calls = [], errors = [], events = {};
    const gain = { gain: { value: 0 }, connect() {} };
    const context = {
        state: 'suspended', destination: {},
        createGain: () => gain,
        resume() { calls.push('resume'); this.state = 'running'; return Promise.resolve(); },
        audioWorklet: { async addModule() { calls.push('worklet'); } },
    };
    class WorkletSynthesizer {
        isReady = Promise.resolve();
        soundBankManager = { async addSoundBank() { calls.push('bank'); } };
        connect() { calls.push('connect'); }
        stopAll(force) { calls.push(['stop', force]); }
        destroy() { calls.push('destroy'); }
    }
    class Sequencer {
        eventHandler = { addEvent(type, id, callback) { events[type] = callback; } };
        pause() { calls.push('pause'); }
        loadNewSongList(list) { calls.push(['load', list[0].binary, this.loopCount]); }
        play() { calls.push('play'); }
    }
    const player = MidiPlayer({
        createContext() { calls.push('context'); return context; },
        async loadLibrary() { calls.push('import'); return { WorkletSynthesizer, Sequencer }; },
        fetchSoundBank, onError: error => errors.push(error),
    });
    return { player, calls, errors, events, gain, context };
}

test('stop before activation allocates no audio resources; activation resumes inside the gesture', async () => {
    const { player, calls } = setup();
    assert.equal(player.stop(), 0);
    assert.deepEqual(calls, []);
    const enabling = player.enable();
    assert.deepEqual(calls.slice(0, 3), ['context', 'resume', 'import']);
    assert.equal(player.enable(), enabling);
    assert.equal(await enabling, true);
    assert.equal(calls.filter(call => call === 'context').length, 1);
});

test('pending playback owns its bytes and only the latest request starts, with explicit loop control', async () => {
    const { player, calls, gain } = setup();
    player.play(midiData(48), false);
    const bytes = midiData(72);
    player.play(bytes, true);
    new Uint8Array(bytes).fill(0);
    assert.equal(await player.enable(), true);
    const loaded = calls.filter(call => Array.isArray(call) && call[0] === 'load');
    assert.equal(loaded.length, 1);
    assert.equal(new Uint8Array(loaded[0][1])[24], 72);
    assert.equal(loaded[0][2], Infinity);
    player.play(midiData(), false);
    assert.equal(calls.findLast(call => Array.isArray(call) && call[0] === 'load')[2], 0);
    assert.equal(gain.gain.value, 1);
    player.stop();
    assert.equal(gain.gain.value, 0);
    assert.deepEqual(calls.at(-1), ['stop', true]);
});

test('stop during initialization cancels queued playback, including a replacement request', async () => {
    let finish;
    const bank = new Promise(resolve => { finish = resolve; });
    const { player, calls, gain } = setup(() => bank);
    player.play(midiData(), true);
    const enabling = player.enable();
    player.stop();
    player.play(midiData(70), false);
    player.stop();
    finish({ ok: true, arrayBuffer: async () => new ArrayBuffer(8) });
    assert.equal(await enabling, true);
    assert.equal(calls.includes('play'), false);
    assert.equal(gain.gain.value, 0);
});

test('missing assets report failure, clean up the synth and permit a successful retry', async () => {
    let failed = true;
    const { player, calls, errors } = setup(async () => ({
        ok: !failed, status: 404, arrayBuffer: async () => new ArrayBuffer(8),
    }));
    player.play(midiData(), false);
    assert.equal(await player.enable(), false);
    assert.match(errors[0].message, /404/);
    assert.equal(calls.includes('destroy'), true);
    failed = false;
    assert.equal(await player.enable(), true);
    assert.equal(calls.includes('play'), false);
});

test('invalid input returns failure and worklet parse errors silence output and reach the caller', async () => {
    const { player, errors, events, gain } = setup();
    assert.equal(player.play(new ArrayBuffer(14), false), -1);
    assert.match(errors[0].message, /MThd/);
    await player.enable();
    player.play(midiData(), false);
    const error = new Error('Bad MIDI track');
    events.midiError(error);
    assert.equal(errors.at(-1), error);
    assert.equal(gain.gain.value, 0);
});

test('a suspended context queues MIDI until the user resumes it', async () => {
    const { player, calls, context } = setup();
    await player.enable();
    context.state = 'suspended';
    player.play(midiData(), false);
    assert.equal(calls.includes('play'), false);
    assert.equal(await player.enable(), true);
    assert.equal(calls.filter(call => call === 'play').length, 1);
    assert.equal(calls.filter(call => call === 'worklet').length, 1);
});
