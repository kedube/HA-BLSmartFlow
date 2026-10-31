// Run with: node --test test/settings-page.test.js
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');
const { test } = require('node:test');
const vm = require('node:vm');

const html = readFileSync(join(__dirname, '../src/www/fanpage.html'), 'utf8');
const script = [...html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)]
  .map(match => match[1]).find(script => script.includes('function updateOptions()'));
const settle = () => new Promise(resolve => setImmediate(resolve));
const copy = value => JSON.parse(JSON.stringify(value));
const points1 = [{ temp: 0, speed: 0 }, { temp: 200, speed: 60 }, { temp: 400, speed: 100 }];
const points2 = [{ temp: 0, speed: 20 }, { temp: 200, speed: 40 }, { temp: 400, speed: 70 }];
const baseConfig = {
  firmwareversion: 'Stable 1.1.0 (2026.10.23)', ip: '192.0.2.83', code: '*****123', id: '************456',
  staticfans: false, staticfanspeed: 0, debuging: true, debugingchange: false, mqttdebug: false,
};
const separateConfig = {
  ...baseConfig, fansSynchronized: false, fan2Configured: true, staticfans2: true, staticfanspeed2: 69,
};

function loadPage({ chartAvailable = true } = {}) {
  const elements = new Map([...html.matchAll(/<[^>]+\bid=['"]([^'"]+)['"][^>]*>/g)].map(match => {
    const tag = match[0];
    const element = {
      value: /\bvalue=['"]([^'"]*)['"]/.exec(tag)?.[1] || '',
      name: /\bname=['"]([^'"]*)['"]/.exec(tag)?.[1],
      type: /\btype=['"]([^'"]*)['"]/.exec(tag)?.[1],
      checked: /\bchecked\b/.test(tag), disabled: /\bdisabled\b/.test(tag), hidden: /\bhidden\b/.test(tag),
      textContent: '', style: {}, listeners: {},
      addEventListener(event, handler) { this.listeners[event] = handler; },
      reportValidity() { return true; },
    };
    return [match[1], element];
  }));
  const requests = [], fetches = [], events = {}, charts = [], timers = new Map();
  let now = 0, timerId = 0;
  class XMLHttpRequest {
    constructor() { requests.push(this); }
    open(method, url) { this.method = method; this.url = url; }
    setRequestHeader(name, value) { (this.headers ||= {})[name] = value; }
    send(body) { this.body = body; }
    respond(status, body) {
      this.status = status; this.responseText = body; this.readyState = 4;
      this.onreadystatechange();
    }
  }
  class FormData {
    constructor() {
      this.entries = [...elements.values()].filter(element => element.name && !element.disabled &&
        (element.type !== 'checkbox' || element.checked)).map(element => [element.name, element.type === 'checkbox' ? 'on' : element.value]);
    }
    [Symbol.iterator]() { return this.entries[Symbol.iterator](); }
  }
  const context = vm.createContext({
    XMLHttpRequest, FormData, URLSearchParams,
    alert() { assert.fail('Unexpected blocking alert'); }, console: { error() {} }, setInterval() {},
    setTimeout(callback, delay) { timers.set(++timerId, { callback, at: now + delay }); return timerId; },
    clearTimeout(id) { timers.delete(id); },
    fetch(url, options) {
      return new Promise((resolve, reject) => fetches.push({ url, options, resolve, reject, done: false }));
    },
    window: { addEventListener(event, handler) { events[event] = handler; } },
    document: {
      querySelectorAll: () => [],
      getElementById(id) { assert.ok(elements.has(id), `Missing element: ${id}`); return elements.get(id); },
    },
    ...(chartAvailable ? { Chart: class {
      constructor(canvas, config) { this.data = config.data; this.options = config.options; charts.push(this); }
      update() {}
      resize() {}
    } } : {}),
  });
  vm.runInContext(script, context);
  const page = {
    context, elements, requests, fetches, events, charts,
    get: id => elements.get(id),
    advanceTime(milliseconds) {
      now += milliseconds;
      for (const [id, timer] of timers) {
        if (timer.at <= now) { timers.delete(id); timer.callback(); }
      }
    },
    state: name => JSON.parse(vm.runInContext(`JSON.stringify(${name})`, context)),
    trigger(id, event) { elements.get(id).listeners[event]({ preventDefault() {} }); },
    pending(url) { const request = fetches.find(request => request.url === url && !request.done); assert.ok(request, `No pending ${url}`); return request; },
    async respond(url, body, status = 200) {
      const request = this.pending(url); request.done = true;
      request.resolve({ ok: status >= 200 && status < 300, json: async () => copy(body), text: async () => typeof body === 'string' ? body : JSON.stringify(body) });
      await settle();
    },
  };
  return page;
}

async function readyPage(config = baseConfig) {
  const page = loadPage();
  page.events.DOMContentLoaded();
  page.requests[0].respond(200, JSON.stringify(config));
  await page.respond('/getFanConfig', { points: points1, points2, fansSynchronized: config.fansSynchronized !== false });
  await page.respond('/sensorData', { temp: 100, speed: 30, speed2: config.fansSynchronized === false ? 69 : 30 });
  return page;
}

test('settings load without waiting for external images or the chart library', () => {
  const page = loadPage({ chartAvailable: false });
  assert.equal(page.requests.length, 1);
  assert.equal(page.requests[0].url, '/getOptions');
  assert.equal(page.get('saveOptions').disabled, true);
  page.requests[0].respond(200, JSON.stringify(baseConfig));
  assert.equal(page.get('printerip').value, baseConfig.ip);
  assert.equal(page.get('printercode').value, baseConfig.code);
  assert.equal(page.get('printerserial').value, baseConfig.id);
  assert.equal(page.get('debuging').checked, true);
  assert.equal(page.get('saveOptions').disabled, false);
  assert.equal(page.get('settings-status').textContent, '');
  assert.ok(page.get('firmwareversionText').textContent.includes(baseConfig.firmwareversion));
  page.events.DOMContentLoaded();
  assert.match(page.get('curve-status').textContent, /chart could not load/i);
  assert.equal(page.get('createPointBtn').disabled, true);
});

test('legacy options default to one synchronized curve and feature section', async () => {
  const page = await readyPage();
  assert.equal(page.get('fansSynchronized').checked, true);
  assert.equal(page.get('fan2Features').hidden, true);
  assert.equal(page.get('fan1FeaturesTitle').hidden, true);
  assert.equal(page.get('fan1FeaturesTitle').textContent, '');
  assert.equal(page.get('fan2Indicator').hidden, true);
  assert.equal(page.charts[0].data.datasets.length, 1);
  assert.equal(page.charts[0].data.datasets[0].label, 'Both fans');
  assert.equal(page.get('followPrinterFan').checked, false);
  assert.equal(page.get('followPrinterFan2').checked, false);
  assert.equal(page.get('printerFanSource').value, 'chamber');
  assert.equal(page.get('fan1Source').hidden, false);
  assert.equal(page.get('fan1Source').textContent, '');
});

for (const suffix of ['', '2']) {
  test(`Fan ${suffix || '1'} static and follow switches exclude each other but can both be off`, async () => {
    const page = await readyPage(separateConfig);
    page.get('staticfan' + suffix).checked = true;
    page.trigger('staticfan' + suffix, 'change');
    assert.equal(page.get('followPrinterFan' + suffix).checked, false);
    page.get('followPrinterFan' + suffix).checked = true;
    page.trigger('followPrinterFan' + suffix, 'change');
    assert.equal(page.get('staticfan' + suffix).checked, false);
    assert.equal(page.get('staticfan' + suffix).disabled, false);
    assert.equal(page.get('followPrinterFan' + suffix).disabled, false);
    page.get('followPrinterFan' + suffix).checked = false;
    page.trigger('followPrinterFan' + suffix, 'change');
    assert.equal(page.get('staticfan' + suffix).checked, false);
    page.get('followPrinterFan' + suffix).checked = true;
    page.trigger('followPrinterFan' + suffix, 'change');
    page.get('staticfan' + suffix).checked = true;
    page.trigger('staticfan' + suffix, 'change');
    assert.equal(page.get('followPrinterFan' + suffix).checked, false);
    page.get('staticfan' + suffix).checked = false;
    page.trigger('staticfan' + suffix, 'change');
    assert.equal(page.get('followPrinterFan' + suffix).checked, false);
  });
}

test('following is saved separately for each Fan and labels change only after successful reload', async () => {
  const page = await readyPage(separateConfig);
  for (const [suffix, source] of [['', 'chamber'], ['2', 'heatbreak']]) {
    page.get('followPrinterFan' + suffix).checked = true;
    page.trigger('followPrinterFan' + suffix, 'change');
    page.get('printerFanSource' + suffix).value = source;
  }
  assert.equal(page.get('fan1Source').hidden, false);
  assert.equal(page.get('fan1Source').textContent, '');
  assert.equal(page.get('fan2Source').hidden, false);
  assert.equal(page.get('fan2Source').textContent, '');
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.get('followPrinterFan'), 'on');
  assert.equal(body.get('printerFanSource'), 'chamber');
  assert.equal(body.get('followPrinterFan2'), 'on');
  assert.equal(body.get('printerFanSource2'), 'heatbreak');
  assert.equal(body.get('staticfan'), 'off');
  assert.equal(body.get('staticfan2'), 'off');
  page.requests[1].respond(200, 'Saved');
  page.requests[2].respond(200, JSON.stringify({ ...separateConfig, staticfans2: false,
    followPrinterFan: true, printerFanSource: 'chamber', followPrinterFan2: true, printerFanSource2: 'heatbreak' }));
  assert.equal(page.get('fan1Source').textContent, 'Chamber Fan');
  assert.equal(page.get('fan2Source').textContent, 'Hotend Fan');
  assert.equal(page.get('fan1Source').hidden, false);
  assert.equal(page.get('fan2Source').hidden, false);
});

test('following can be turned off explicitly and a failed save retains the active label', async () => {
  const page = await readyPage({ ...baseConfig, followPrinterFan: true, printerFanSource: 'auxiliary' });
  assert.equal(page.get('fan1Source').textContent, 'Auxiliary Fan');
  page.get('followPrinterFan').checked = false;
  page.trigger('followPrinterFan', 'change');
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.get('followPrinterFan'), 'off');
  assert.equal(body.get('staticfan'), 'off');
  page.requests[1].respond(500, 'Flash write failed');
  assert.equal(page.get('fan1Source').textContent, 'Auxiliary Fan');
  assert.equal(page.get('fan1Source').hidden, false);
  page.context.submitOptions({ preventDefault() {} });
  page.requests[2].respond(200, 'Saved');
  page.requests[3].respond(200, JSON.stringify(baseConfig));
  assert.equal(page.get('fan1Source').hidden, false);
  assert.equal(page.get('fan1Source').textContent, '');
});

test('first independent draft copies follow settings and sync saves preserve dormant Fan 2', async () => {
  const page = await readyPage({ ...baseConfig, followPrinterFan: true, printerFanSource: 'part' });
  page.get('fansSynchronized').checked = false;
  page.trigger('fansSynchronized', 'change');
  assert.equal(page.get('followPrinterFan2').checked, true);
  assert.equal(page.get('printerFanSource2').value, 'part');
  assert.equal(page.get('followPrinterFan2').disabled, false);
  page.get('printerFanSource2').value = 'heatbreak';
  page.get('fansSynchronized').checked = true;
  page.trigger('fansSynchronized', 'change');
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.has('followPrinterFan2'), false);
  assert.equal(body.has('printerFanSource2'), false);
  assert.equal(page.get('printerFanSource2').value, 'heatbreak');
});

test('synchronized indicators use Fan 1 and retain saved Fan 2 follow settings', async () => {
  const page = await readyPage({ ...baseConfig, fansSynchronized: true, fan2Configured: true,
    followPrinterFan: true, printerFanSource: 'chamber', followPrinterFan2: true, printerFanSource2: 'part' });
  assert.equal(page.get('fan1Source').textContent, 'Chamber Fan');
  assert.equal(page.get('fan2Indicator').hidden, true);
  page.get('fansSynchronized').checked = false;
  page.trigger('fansSynchronized', 'change');
  assert.equal(page.get('followPrinterFan2').checked, true);
  assert.equal(page.get('printerFanSource2').value, 'part');
});

test('mixed follow modes retain an empty source row for the other Fan', async () => {
  for (const followsFirst of [false, true]) {
    const page = await readyPage({ ...separateConfig, staticfans2: false,
      followPrinterFan: followsFirst, printerFanSource: 'chamber',
      followPrinterFan2: !followsFirst, printerFanSource2: 'part' });
    assert.equal(page.get('fan1Source').hidden, false);
    assert.equal(page.get('fan2Source').hidden, false);
    assert.equal(page.get('fan1Source').textContent, followsFirst ? 'Chamber Fan' : '');
    assert.equal(page.get('fan2Source').textContent, followsFirst ? '' : 'Part Cooling Fan');
  }
});

test('submitting before settings arrive cannot overwrite saved credentials', () => {
  const page = loadPage(); let prevented = false;
  assert.equal(page.context.submitOptions({ preventDefault() { prevented = true; } }), false);
  assert.equal(prevented, true); assert.equal(page.requests.length, 1);
});

for (const [name, fail] of [
  ['HTTP error', xhr => xhr.respond(500, 'Failed')],
  ['invalid JSON', xhr => xhr.respond(200, '{')],
  ['missing settings', xhr => xhr.respond(200, '{}')],
  ['network error', xhr => xhr.onerror()],
  ['timeout', xhr => xhr.ontimeout()],
]) {
  test(`${name} shows an error, keeps Save disabled, and permits recovery`, () => {
    const page = loadPage();
    assert.equal(page.requests[0].timeout, 30000);
    fail(page.requests[0]);
    assert.match(page.get('settings-status').textContent, /unable to load/i);
    assert.equal(page.get('saveOptions').disabled, true);
    page.context.updateOptions();
    page.requests[1].respond(200, JSON.stringify(baseConfig));
    assert.equal(page.get('saveOptions').disabled, false);
  });
}

test('independent mode is only applied after successful settings save and reload', async () => {
  const page = await readyPage();
  page.get('staticfan').checked = true; page.get('staticfanspeed').value = '37';
  page.get('fansSynchronized').checked = false; page.trigger('fansSynchronized', 'change');
  assert.equal(page.get('fan1FeaturesTitle').textContent, 'Fan 1');
  assert.equal(page.get('fan1FeaturesTitle').hidden, false);
  assert.equal(page.get('fan2Features').hidden, false);
  assert.equal(page.get('staticfan2').checked, true);
  assert.equal(page.get('staticfanspeed2').value, '37');
  assert.equal(page.get('fan2Indicator').hidden, true);
  assert.equal(page.charts[0].data.datasets.length, 1);
  assert.equal(page.get('createPointBtn').disabled, true);
  assert.match(page.get('mode-status').textContent, /save settings/i);
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.get('fansSynchronized'), 'off');
  assert.equal(body.get('staticfan2'), 'on');
  assert.equal(body.get('staticfanspeed2'), '37');
  assert.equal(page.get('settings-fields').disabled, true);
  page.requests[1].respond(200, 'Settings saved');
  page.requests[2].respond(200, JSON.stringify({ ...separateConfig, staticfans: true, staticfanspeed: 37, staticfanspeed2: 37 }));
  await page.respond('/getFanConfig', { points: points1, points2: points1, fansSynchronized: false });
  assert.equal(page.get('fan2Indicator').hidden, false);
  assert.equal(page.charts[0].data.datasets.length, 2);
  assert.equal(page.get('createPointBtn').disabled, false);
});

test('returning to synchronized mode omits hidden Fan 2 settings without clearing them', async () => {
  const page = await readyPage(separateConfig);
  assert.equal(page.get('fansSynchronized').checked, false);
  page.get('fansSynchronized').checked = true; page.trigger('fansSynchronized', 'change');
  assert.equal(page.get('fan2Features').hidden, true);
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.get('fansSynchronized'), 'on');
  assert.equal(body.has('staticfan2'), false); assert.equal(body.has('staticfanspeed2'), false);
  page.requests[1].respond(200, 'Settings saved');
  page.requests[2].respond(200, JSON.stringify({ ...separateConfig, fansSynchronized: true }));
  await page.respond('/getFanConfig', { points: points1, points2, fansSynchronized: true });
  assert.equal(page.charts[0].data.datasets.length, 1);
  page.get('fansSynchronized').checked = false; page.trigger('fansSynchronized', 'change');
  assert.equal(page.get('staticfan2').checked, true);
  assert.equal(page.get('staticfanspeed2').value, 69);
});

test('a failed settings save retains the active synchronized view and shows the error', async () => {
  const page = await readyPage();
  page.get('fansSynchronized').checked = false; page.trigger('fansSynchronized', 'change');
  page.context.submitOptions({ preventDefault() {} });
  page.requests[1].respond(500, 'Failed to save settings to flash');
  assert.match(page.get('settings-status').textContent, /failed to save/i);
  assert.equal(page.get('fan2Indicator').hidden, true);
  assert.equal(page.charts[0].data.datasets.length, 1);
  assert.equal(page.get('saveOptions').disabled, false);
  assert.equal(page.get('save-notice').hidden, true);
  page.advanceTime(5000);
  assert.match(page.get('settings-status').textContent, /failed to save/i);
});

test('settings save shows a temporary notice that survives reloading settings', async () => {
  const page = await readyPage();
  page.context.submitOptions({ preventDefault() {} });
  page.requests[1].respond(200, 'Settings saved');
  assert.equal(page.get('save-notice').textContent, 'Settings saved successfully.');
  assert.equal(page.get('save-notice').hidden, false);
  page.requests[2].respond(200, JSON.stringify(baseConfig));
  page.advanceTime(3999);
  assert.equal(page.get('save-notice').hidden, false);
  page.advanceTime(1);
  assert.equal(page.get('save-notice').hidden, true);
  assert.equal(page.get('save-notice').textContent, '');
});

test('a newer save receives a full notice duration and a failed save has no stale success', async () => {
  const page = await readyPage(separateConfig);
  page.context.saveCurve(points1);
  await page.respond('/updateFanConfig', 'Saved');
  page.advanceTime(3000);
  page.context.saveCurve(points1);
  assert.equal(page.get('save-notice').hidden, true);
  await page.respond('/updateFanConfig', 'Saved');
  page.advanceTime(1000);
  assert.equal(page.get('save-notice').hidden, false);
  page.advanceTime(3000);
  assert.equal(page.get('save-notice').hidden, true);
  page.context.saveCurve(points1);
  await page.respond('/updateFanConfig', 'Saved');
  page.context.saveCurve(points1);
  await page.respond('/updateFanConfig', 'Flash write failed', 500);
  page.advanceTime(5000);
  assert.equal(page.get('save-notice').hidden, true);
  assert.match(page.get('curve-status').textContent, /Flash write failed/);
});

test('clicking either curve switches the editor and switch to the clicked Fan', async () => {
  const page = await readyPage(separateConfig);
  for (const fan of [1, 0]) {
    page.charts[0].options.onClick({}, [{ datasetIndex: fan, index: 1 }]);
    assert.equal(page.state('selectedFan'), fan);
    assert.equal(page.get('selectedFan').checked, fan === 1);
    assert.equal(page.get('editSection').style.display, 'block');
    assert.equal(page.get('pointHeading').textContent, 'Selected point — Fan ' + (fan + 1));
    assert.equal(page.get('edit-temp').value, 200);
    assert.equal(page.get('edit-speed').value, [60, 40][fan]);
  }
  page.get('edit-speed').value = '45';
  page.trigger('editPointBtn', 'click');
  const body = new URLSearchParams(page.pending('/updateFanConfig').options.body);
  assert.equal(body.get('fan'), '1');
  page.charts[0].options.onClick({}, [{ datasetIndex: 1, index: 1 }]);
  assert.equal(page.state('selectedFan'), 0, 'Ignore clicks while saving');
  await page.respond('/updateFanConfig', 'Saved');
  assert.deepEqual(page.state('curves[1]'), points2);
  assert.equal(page.state('curves[0][1].speed'), 45);
});

test('overlapping points prefer the currently selected Fan', async () => {
  const page = await readyPage(separateConfig);
  page.get('selectedFan').checked = true;
  page.trigger('selectedFan', 'change');
  page.charts[0].options.onClick({}, [{ datasetIndex: 0, index: 1 }, { datasetIndex: 1, index: 1 }]);
  assert.equal(page.state('selectedFan'), 1);
  assert.equal(page.get('edit-speed').value, 40);
  page.charts[0].options.onClick({}, []);
  assert.equal(page.state('selectedFan'), 1);
  assert.equal(page.state('selectedIndex'), 1);
});

test('editing Fan 2 targets only Fan 2 and only changes the saved display on success', async () => {
  const page = await readyPage(separateConfig);
  page.get('selectedFan').checked = true; page.trigger('selectedFan','change');
  page.charts[0].options.onClick({}, [{ datasetIndex: 1, index: 1 }]);
  page.get('edit-temp').value = '180'; page.get('edit-speed').value = '55';
  page.trigger('editPointBtn','click');
  const request = page.pending('/updateFanConfig');
  const body = new URLSearchParams(request.options.body);
  assert.equal(body.get('fan'), '2');
  assert.deepEqual(JSON.parse(body.get('points')).points[1], { temp: 180, speed: 55 });
  assert.deepEqual(page.state('curves'), [points1, points2]);
  assert.equal(page.get('selectedFan').disabled, true);
  assert.equal(page.get('saveOptions').disabled, true);
  await page.respond('/updateFanConfig', 'Fan curve saved');
  assert.deepEqual(page.state('curves[0]'), points1);
  assert.deepEqual(page.state('curves[1][1]'), { temp: 180, speed: 55 });
  assert.equal(page.get('save-notice').textContent, 'Fan 2 curve saved.');
  assert.equal(page.get('save-notice').hidden, false);
  page.advanceTime(4000);
  assert.equal(page.get('save-notice').hidden, true);
});

test('failed curve saves leave the previous curve visible', async () => {
  const page = await readyPage(separateConfig);
  page.get('selectedFan').checked = true; page.trigger('selectedFan','change');
  const saving = page.context.saveCurve([{ temp: 10, speed: 80 }]);
  await page.respond('/updateFanConfig','Flash write failed',500); await saving;
  assert.deepEqual(page.state('curves'), [points1, points2]);
  assert.match(page.get('curve-status').textContent, /flash write failed/i);
  assert.equal(page.get('createPointBtn').disabled, false);
});

test('point creation, validation, and removal do not overwrite the other fan', async () => {
  const page = await readyPage(separateConfig);
  page.get('selectedFan').checked = true; page.trigger('selectedFan','change');
  page.trigger('createPointBtn','click');
  assert.equal(page.fetches.filter(r => r.url === '/updateFanConfig').length, 0);
  page.get('edit-temp').value = '200'; page.get('edit-speed').value = '30';
  page.trigger('editPointBtn','click');
  assert.match(page.get('curve-status').textContent, /unique/i);
  page.get('edit-temp').value = '100'; page.get('edit-speed').value = '30.5';
  page.trigger('editPointBtn','click');
  assert.match(page.get('curve-status').textContent, /whole-number/i);
  assert.equal(page.fetches.filter(r => r.url === '/updateFanConfig').length, 0);
  page.get('edit-speed').value = '30'; page.trigger('editPointBtn','click');
  await page.respond('/updateFanConfig','Saved');
  assert.equal(page.state('curves[1]').length, 4);
  assert.deepEqual(page.state('curves[0]'),points1);
  page.charts[0].options.onClick({}, [{ datasetIndex: 1, index: 1 }]);
  page.trigger('removePointBtn','click'); await page.respond('/updateFanConfig','Saved');
  assert.deepEqual(page.state('curves'),[points1,points2]);
  await page.context.saveCurve([]);
  assert.match(page.get('curve-status').textContent, /at least one/i);
});

test('sensor data controls two distinct animations and speed labels', async () => {
  const page = await readyPage(separateConfig);
  const polling = page.context.fetchSensorData();
  await page.respond('/sensorData', { temp: 180, speed: 0, speed2: 100 }); await polling;
  assert.equal(page.get('blades1').style.animation,'none');
  assert.equal(page.get('blades2').style.animation,'spin 0.5s linear infinite');
  assert.equal(page.get('fan1Percent').textContent,0);
  assert.equal(page.get('fan2Percent').textContent,100);
});

test('curve loading failures disable editing and successful retry restores it', async () => {
  const page = loadPage(); page.events.DOMContentLoaded();
  page.requests[0].respond(200, JSON.stringify(separateConfig));
  assert.equal(page.get('createPointBtn').disabled, true);
  await page.respond('/getFanConfig', {},500);
  assert.equal(page.get('retryCurves').hidden, false);
  assert.equal(page.get('createPointBtn').disabled, true);
  page.trigger('retryCurves','click');
  await page.respond('/getFanConfig',{ points: points1, points2, fansSynchronized: false });
  assert.equal(page.get('createPointBtn').disabled,false);
});

test('switching the editing selector clears the previous point selection', async () => {
  const page = await readyPage(separateConfig);
  page.charts[0].options.onClick({}, [{ datasetIndex: 0, index: 1 }]);
  assert.equal(page.get('editSection').style.display,'block');
  page.get('selectedFan').checked = true; page.trigger('selectedFan','change');
  assert.equal(page.get('editSection').style.display,'none');
  assert.equal(page.state('selectedIndex'),null);
});

test('Post Print defaults off and can be enabled alongside either normal override', async () => {
  const page = await readyPage(separateConfig);
  for (const suffix of ['', '2']) {
    assert.equal(page.get('postPrint' + suffix).checked, false);
    assert.equal(page.get('postPrintSeconds' + suffix).value, 600);
    assert.equal(page.get('postPrintSpeed' + suffix).value, 100);
    page.get('postPrint' + suffix).checked = true;
    page.trigger('postPrint' + suffix, 'change');
    for (const mode of ['staticfan', 'followPrinterFan']) {
      page.get(mode + suffix).checked = true;
      page.trigger(mode + suffix, 'change');
      assert.equal(page.get('postPrint' + suffix).checked, true);
      page.get('postPrint' + suffix).checked = false;
      page.trigger('postPrint' + suffix, 'change');
      assert.equal(page.get(mode + suffix).checked, true);
      page.get('postPrint' + suffix).checked = true;
      page.trigger('postPrint' + suffix, 'change');
      assert.equal(page.get(mode + suffix).checked, true);
    }
  }
});

test('Post Print saves independent values and explicitly saves Off without altering following', async () => {
  const page = await readyPage({ ...separateConfig, staticfans2: false,
    followPrinterFan: true, printerFanSource: 'chamber', postPrint: true, postPrintSeconds: 600, postPrintSpeed: 100,
    postPrint2: true, postPrintSeconds2: 30, postPrintSpeed2: 40 });
  assert.equal(page.get('postPrint2').checked, true);
  assert.equal(page.get('postPrintSeconds2').value, 30);
  page.get('postPrint').checked = false;
  page.trigger('postPrint', 'change');
  page.get('postPrintSeconds2').value = '120';
  page.get('postPrintSpeed2').value = '0';
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.get('postPrint'), 'off');
  assert.equal(body.get('followPrinterFan'), 'on');
  assert.equal(body.get('postPrint2'), 'on');
  assert.equal(body.get('postPrintSeconds2'), '120');
  assert.equal(body.get('postPrintSpeed2'), '0');
  page.requests[1].respond(500, 'Flash write failed');
  assert.equal(page.get('fan1Source').textContent, 'Chamber Fan');
  assert.match(page.get('settings-status').textContent, /Flash write failed/);
});

test('first split clones Post Print and synchronization omits all dormant Fan 2 inputs', async () => {
  const page = await readyPage({ ...baseConfig, postPrint: true, postPrintSeconds: 90, postPrintSpeed: 70 });
  page.get('fansSynchronized').checked = false;
  page.trigger('fansSynchronized', 'change');
  assert.equal(page.get('postPrint2').checked, true);
  assert.equal(page.get('postPrintSeconds2').value, 90);
  assert.equal(page.get('postPrintSpeed2').value, 70);
  assert.equal(page.get('postPrintSeconds2').disabled, false);
  page.get('postPrintSeconds2').value = '45';
  page.get('fansSynchronized').checked = true;
  page.trigger('fansSynchronized', 'change');
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  for (const name of ['postPrint2', 'postPrintSeconds2', 'postPrintSpeed2']) {
    assert.equal(body.has(name), false);
    assert.equal(page.get(name).disabled, true);
  }
  assert.equal(page.get('postPrintSeconds2').value, '45');
});

test('telemetry shows separate Post Print countdowns and restores normal source labels at expiration', async () => {
  const page = await readyPage({ ...separateConfig, followPrinterFan: true, printerFanSource: 'chamber' });
  for (const [remaining, remaining2] of [[600, 90], [510, 0], [0, 0]]) {
    const polling = page.context.fetchSensorData();
    await page.respond('/sensorData', { temp: 20, speed: remaining ? 100 : 70, speed2: remaining2 ? 50 : 69,
      postPrintRemaining: remaining, postPrintRemaining2: remaining2 });
    await polling;
    assert.equal(page.get('fan1Source').textContent, remaining ? `Post Print: ${remaining}s` : 'Chamber Fan');
    assert.equal(page.get('fan2Source').textContent, remaining2 ? `Post Print: ${remaining2}s` : '');
    assert.equal(page.get('fan1Source').hidden, false);
    assert.equal(page.get('fan2Source').hidden, false);
    assert.equal(page.get('fan1Percent').textContent, remaining ? 100 : 70);
  }
});

test('failed telemetry clears stale countdowns and pending polls cannot pile up', async () => {
  const page = await readyPage();
  page.context.fetchSensorData();
  await page.respond('/sensorData', { temp: 20, speed: 100, speed2: 100, postPrintRemaining: 10, postPrintRemaining2: 10 });
  assert.equal(page.get('fan1Source').textContent, 'Post Print: 10s');
  page.context.fetchSensorData();
  page.context.fetchSensorData();
  assert.equal(page.fetches.filter(request => request.url === '/sensorData' && !request.done).length, 1);
  await page.respond('/sensorData', {}, 500);
  assert.equal(page.get('fan1Source').textContent, '');
  page.context.fetchSensorData();
  await page.respond('/sensorData', { temp: 20, speed: 100, speed2: 100, postPrintRemaining: 8, postPrintRemaining2: 8 });
  assert.equal(page.get('fan1Source').textContent, 'Post Print: 8s');
});

const haConfig = {
  ...baseConfig, haEnabled: true, haHost: 'homeassistant.local', haPort: 1884, haUser: 'mqtt-user',
  haPasswordSet: true, settingsRevision: 3,
};

test('Home Assistant settings load without the saved password and save explicit switch values', async () => {
  const page = await readyPage(haConfig);
  assert.equal(page.get('haEnabled').checked, true);
  assert.equal(page.get('haFields').hidden, false);
  assert.equal(page.get('haHost').value, 'homeassistant.local');
  assert.equal(page.get('haPort').value, 1884);
  assert.equal(page.get('haUser').value, 'mqtt-user');
  assert.equal(page.get('haPassword').value, '');
  assert.match(page.get('haPassword').placeholder, /leave blank to keep/i);
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.get('haEnabled'), 'on');
  assert.equal(body.has('haTls'), false);
  assert.equal(body.get('haHost'), 'homeassistant.local');
  assert.equal(body.get('haPort'), '1884');
  assert.equal(body.get('haUser'), 'mqtt-user');
  assert.equal(body.get('haPassword'), '');
  page.requests[1].respond(200, 'Settings saved');
  page.requests[2].respond(200, JSON.stringify({ ...haConfig, haPasswordSet: false }));
  assert.equal(page.get('haPassword').placeholder, '');
});

test('older firmware leaves Home Assistant off, and turning it off disables the broker fields', async () => {
  let page = await readyPage();
  assert.equal(page.get('haEnabled').checked, false);
  assert.equal(page.get('haFields').hidden, true);
  assert.equal(page.get('haFields').disabled, true);
  assert.equal(page.get('haPort').value, 1883);
  page = await readyPage({ ...haConfig, haEnabled: false, haPort: 1883 });
  assert.equal(page.get('haHost').value, 'homeassistant.local');
  page.get('haEnabled').checked = true;
  page.trigger('haEnabled', 'change');
  assert.equal(page.get('haFields').hidden, false);
  assert.equal(page.get('haFields').disabled, false);
  assert.match(page.get('settings-status').textContent, /unsaved/i);
  page.get('haEnabled').checked = false;
  page.trigger('haEnabled', 'change');
  // A disabled fieldset is neither validated nor submitted, so the firmware keeps the saved broker.
  assert.equal(page.get('haFields').hidden, true);
  assert.equal(page.get('haFields').disabled, true);
  assert.equal(page.get('haHost').value, 'homeassistant.local');
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.get('haEnabled'), 'off');
});

test('the broker connection status updates with telemetry', async () => {
  const page = await readyPage(haConfig);
  for (const [status, state, expected] of [
    ['connected', 0, /^Connected to the broker\.$/], ['connecting', -1, /^Connecting/],
    ['disconnected', 4, /rejected the username or password/], ['disconnected', 5, /rejected the username or password/],
    ['disconnected', -2, /could not be reached/], ['disconnected', -3, /^Not connected: retrying\.$/],
    ['disabled', -1, /^$/], [undefined, undefined, /^$/],
  ]) {
    const polling = page.context.fetchSensorData();
    await page.respond('/sensorData', { temp: 20, speed: 0, speed2: 0, homeAssistant: status, homeAssistantState: state });
    await polling;
    assert.match(page.get('ha-status').textContent, expected);
  }
});

test('settings changed elsewhere reload a clean page and warn before overwriting edits', async () => {
  const sensor = revision => ({ temp: 20, speed: 0, speed2: 0, settingsRevision: revision });
  const page = await readyPage({ ...baseConfig, settingsRevision: 5 });
  let polling = page.context.fetchSensorData();
  await page.respond('/sensorData', sensor(5)); await polling;
  assert.equal(page.requests.length, 1, 'An unchanged revision does not reload');
  polling = page.context.fetchSensorData();
  await page.respond('/sensorData', sensor(6)); await polling;
  assert.equal(page.requests.length, 2);
  assert.equal(page.requests[1].url, '/getOptions');
  polling = page.context.fetchSensorData();
  await page.respond('/sensorData', sensor(6)); await polling;
  assert.equal(page.requests.length, 2, 'No second reload while one is loading');
  page.requests[1].respond(200, JSON.stringify({ ...baseConfig, staticfans: true, staticfanspeed: 64, settingsRevision: 6 }));
  await page.respond('/getFanConfig', { points: points1, points2, fansSynchronized: true });
  assert.equal(page.get('staticfan').checked, true);
  assert.equal(page.get('staticfanspeed').value, 64);
  page.get('staticfanspeed').value = '20';
  page.trigger('settings-form', 'input');
  polling = page.context.fetchSensorData();
  await page.respond('/sensorData', sensor(7)); await polling;
  assert.equal(page.requests.length, 2, 'Unsaved edits are not discarded');
  assert.match(page.get('settings-status').textContent, /changed elsewhere/i);
  assert.equal(page.get('staticfanspeed').value, '20');
  page.context.submitOptions({ preventDefault() {} });
  assert.equal(new URLSearchParams(page.requests[2].body).get('staticfanspeed'), '20');
  page.requests[2].respond(200, 'Settings saved');
  page.requests[3].respond(200, JSON.stringify({ ...baseConfig, staticfans: true, staticfanspeed: 20, settingsRevision: 8 }));
  assert.equal(page.state('settingsRevision'), 8);
  assert.equal(page.state('settingsDirty'), false);
  assert.equal(page.get('settings-status').textContent, '');
});

test('changes are sent with the header that marks the controller\'s own pages', async () => {
  const page = await readyPage();
  page.context.submitOptions({ preventDefault() {} });
  assert.equal(page.requests[1].headers['X-BLSmartFlow'], '1');
  page.requests[1].respond(200, 'Settings saved');
  page.requests[2].respond(200, JSON.stringify(baseConfig));
  await page.respond('/getFanConfig', { points: points1, points2, fansSynchronized: true });
  page.context.saveCurve(points1);
  assert.equal(page.pending('/updateFanConfig').options.headers['X-BLSmartFlow'], '1');
  for (const url of ['/getOptions', '/getFanConfig', '/sensorData']) {
    assert.ok([...page.requests, ...page.fetches].some(request => request.url === url), `Requested ${url}`);
  }
});

test('the login section warns while unprotected and never shows the saved password', async () => {
  let page = await readyPage();
  assert.match(page.get('login-status').textContent, /anyone on your network/i);
  assert.match(page.get('login-status').className, /unprotected/);
  assert.equal(page.get('webUser').value, '');
  assert.equal(page.get('webPassword').placeholder, '');
  page = await readyPage({ ...baseConfig, webUser: 'admin', webPasswordSet: true });
  assert.match(page.get('login-status').textContent, /require the login/i);
  assert.doesNotMatch(page.get('login-status').className, /unprotected/);
  assert.equal(page.get('webUser').value, 'admin');
  assert.equal(page.get('webPassword').value, '');
  assert.match(page.get('webPassword').placeholder, /leave blank to keep/i);
  page.get('webPassword').value = 'new password';
  page.trigger('settings-form', 'input');
  page.context.submitOptions({ preventDefault() {} });
  const body = new URLSearchParams(page.requests[1].body);
  assert.equal(body.get('webUser'), 'admin');
  assert.equal(body.get('webPassword'), 'new password');
  page.requests[1].respond(200, 'Settings saved');
  page.requests[2].respond(200, JSON.stringify({ ...baseConfig, webUser: 'admin', webPasswordSet: true }));
  assert.equal(page.get('webPassword').value, '');
});
