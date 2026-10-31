// Run with: node --test test/network-setup-page.test.js
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');
const { test } = require('node:test');
const vm = require('node:vm');

const html = readFileSync(join(__dirname, '../src/www/networkSetup.html'), 'utf8');
const script = [...html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)]
  .map(match => match[1]).find(script => script.includes('function loadSetup()'));
const settle = () => new Promise(resolve => setImmediate(resolve));
const saved = {
  ssid: 'home', passwordSet: true, ip: '192.0.2.83', code: '*****678', id: '************789',
  accessPoint: false, address: 'http://blflow.local/',
};

function loadPage() {
  const page = { fetches: [], timers: new Map(), confirmations: [], confirmResult: true, focused: null, now: 0 };
  const element = (tag, attributes = {}) => ({
    tagName: tag, id: attributes.id, value: attributes.value || '', hidden: Boolean(attributes.hidden),
    disabled: Boolean(attributes.disabled), textContent: '', placeholder: '', className: '', children: [], listeners: {},
    addEventListener(event, handler) { this.listeners[event] = handler; },
    append(...nodes) { this.children.push(...nodes); },
    replaceChildren(...nodes) { this.children = nodes; },
    focus() { page.focused = this.id; },
  });
  page.elements = new Map([...html.matchAll(/<([\w-]+)[^>]*\bid=['"]([^'"]+)['"][^>]*>/g)].map(([tag, name, id]) => [id,
    element(name, { id, value: /\bvalue=['"]([^'"]*)['"]/.exec(tag)?.[1], hidden: /\bhidden\b/.test(tag),
                    disabled: /\bdisabled\b/.test(tag) })]));
  let timerId = 0;
  const context = vm.createContext({
    URLSearchParams, console, location: { href: '/wifi' },
    confirm(message) { page.confirmations.push(message); return page.confirmResult; },
    setTimeout(callback, delay) { page.timers.set(++timerId, { callback, at: page.now + delay }); return timerId; },
    fetch(url, options) {
      return new Promise((resolve, reject) => page.fetches.push({ url, options, resolve, reject, done: false }));
    },
    document: {
      createElement: tag => element(tag),
      getElementById(id) { assert.ok(page.elements.has(id), `Missing element: ${id}`); return page.elements.get(id); },
    },
  });
  vm.runInContext(script, context);
  Object.assign(page, {
    context,
    get: id => page.elements.get(id),
    pending(url) {
      const request = page.fetches.find(request => request.url === url && !request.done);
      assert.ok(request, `No pending ${url}`);
      return request;
    },
    async respond(url, status, body) {
      const request = page.pending(url);
      request.done = true;
      request.resolve({ ok: status >= 200 && status < 300, status, json: async () => JSON.parse(JSON.stringify(body)),
                        text: async () => typeof body === 'string' ? body : JSON.stringify(body) });
      await settle();
    },
    async advance(milliseconds) {
      page.now += milliseconds;
      for (const [id, timer] of page.timers) {
        if (timer.at <= page.now) { page.timers.delete(id); timer.callback(); }
      }
      await settle();
    },
    async trigger(id, event) { page.get(id).listeners[event]({ preventDefault() {} }); await settle(); },
    texts: id => page.get(id).children.map(child => child.children.length
      ? child.children.map(part => part.textContent) : child.textContent),
  });
  return page;
}

async function readyPage(config = saved) {
  const page = loadPage();
  await page.respond('/getWifiSetup', 200, config);
  return page;
}

test('the saved network and printer load, and a blank password is kept only for the same network', async () => {
  const page = await readyPage();
  assert.equal(page.get('setupFields').disabled, false);
  assert.equal(page.get('setup-status').textContent, '');
  assert.equal(page.get('ssid').value, 'home');
  assert.equal(page.get('wifiPassword').value, '');
  assert.equal(page.get('wifiPassword').placeholder, 'Saved; leave blank to keep');
  assert.equal(page.get('printerip').value, '192.0.2.83');
  assert.equal(page.get('printerserial').value, '************789');
  assert.equal(page.get('printercode').value, '*****678');
  assert.equal(page.get('apIntro').hidden, true);
  assert.equal(page.get('backLink').hidden, false);
  assert.equal(page.get('restoreLink').hidden, true);
  assert.equal(page.get('findPrinters').hidden, false);
  page.get('ssid').value = 'workshop';
  await page.trigger('ssid', 'input');
  assert.equal(page.get('wifiPassword').placeholder, '');
  await page.trigger('setupForm', 'submit');
  assert.equal(page.get('setup-status').textContent, 'Enter the password for workshop.');
  assert.equal(page.fetches.length, 1);
});

test('saving asks first, sends the fields with the page header, and explains where to find the controller', async () => {
  const page = await readyPage();
  page.get('wifiPassword').value = 'new-password';
  page.confirmResult = false;
  await page.trigger('setupForm', 'submit');
  assert.deepEqual(page.confirmations, ['Save and restart the controller?']);
  assert.equal(page.fetches.length, 1);
  page.confirmResult = true;
  page.get('ssid').value = 'workshop';
  await page.trigger('setupForm', 'submit');
  assert.equal(page.confirmations[1], 'Save and restart the controller to join workshop?');
  const request = page.pending('/submitWifiSetup');
  assert.equal(request.options.method, 'POST');
  assert.equal(request.options.headers['X-BLSmartFlow'], '1');
  const body = new URLSearchParams(request.options.body);
  assert.deepEqual(Object.fromEntries(body), {
    ssid: 'workshop', password: 'new-password', ip: '192.0.2.83', serial: '************789', code: '*****678',
  });
  assert.equal(page.get('setupFields').disabled, true);
  await page.respond('/submitWifiSetup', 200, 'Saved. The controller is restarting.');
  assert.equal(page.get('setup-status').textContent, 'Saved. The controller is restarting to join workshop.');
  assert.equal(page.get('afterSave').hidden, false);
  assert.match(page.get('afterSave').textContent, /^Once it has joined, open http:\/\/blflow\.local\//);
});

test('a refused save keeps the fields editable and shows why', async () => {
  const page = await readyPage();
  page.get('wifiPassword').value = 'short';
  await page.trigger('setupForm', 'submit');
  await page.respond('/submitWifiSetup', 400, 'Wi-Fi passwords have 8 to 63 characters');
  assert.equal(page.get('setup-status').textContent, 'Not saved: Wi-Fi passwords have 8 to 63 characters');
  assert.equal(page.get('setupFields').disabled, false);
  assert.equal(page.get('afterSave').hidden, true);
});

test('a scan is polled until ready; choosing a network fills it in, and an open one saves without a password', async () => {
  const page = await readyPage();
  await page.trigger('scanNetworks', 'click');
  assert.equal(page.get('scanNetworks').disabled, true);
  assert.deepEqual(page.texts('networkList'), ['Looking for networks...']);
  await page.respond('/wifiScan?start=1', 202, { scanning: true });
  await page.advance(1000);
  await page.respond('/wifiScan', 202, { scanning: true });
  await page.advance(1000);
  await page.respond('/wifiScan', 200, { networks: [
    { ssid: 'home', rssi: -55, secure: true }, { ssid: 'Guest', rssi: -70, secure: false }] });
  assert.equal(page.get('scanNetworks').disabled, false);
  assert.deepEqual(page.texts('networkList'), [['home', '100%'], ['Guest', '60%, open']]);
  page.get('wifiPassword').value = 'typed';
  page.get('networkList').children[1].listeners.click();
  assert.equal(page.get('ssid').value, 'Guest');
  assert.equal(page.get('wifiPassword').value, '');
  assert.equal(page.get('wifiPassword').placeholder, 'Open network; no password');
  await page.trigger('setupForm', 'submit');
  const body = new URLSearchParams(page.pending('/submitWifiSetup').options.body);
  assert.equal(body.get('ssid'), 'Guest');
  assert.equal(body.get('password'), '');
  assert.equal(body.get('open'), 'on');
});

test('a failed or unfinished scan says so and can be retried', async () => {
  const page = await readyPage();
  await page.trigger('scanNetworks', 'click');
  await page.respond('/wifiScan?start=1', 503, 'Unable to scan for networks; try again');
  assert.deepEqual(page.texts('networkList'), ['Unable to scan for networks; try again']);
  assert.equal(page.get('scanNetworks').disabled, false);
  await page.trigger('scanNetworks', 'click');
  await page.respond('/wifiScan?start=1', 202, { scanning: true });
  for (let i = 0; i < 20; ++i) {
    await page.advance(1000);
    await page.respond('/wifiScan', 202, { scanning: true });
  }
  assert.deepEqual(page.texts('networkList'), ['The scan did not finish; try again.']);
  await page.trigger('scanNetworks', 'click');
  // As in a browser, fetch's error comes from the page's own realm.
  page.pending('/wifiScan?start=1').reject(vm.runInContext("new TypeError('Failed to fetch')", page.context));
  await settle();
  assert.deepEqual(page.texts('networkList'), ['Unable to reach the controller; try again.']);
});

test('finding printers lists them as they answer, and choosing one fills the IP and serial number', async () => {
  const page = await readyPage();
  await page.trigger('findPrinters', 'click');
  await page.respond('/printers', 200, { listening: true, printers: [] });
  assert.deepEqual(page.texts('printerList'), ['Looking for printers on your network...']);
  await page.advance(2000);
  await page.respond('/printers', 200, { listening: true, printers: [
    { ip: '192.0.2.50', serial: '01P00A123456789', name: 'Garage P1S', model: 'C12' },
    { ip: '192.0.2.51', serial: '03W00X987654321', name: '', model: 'C11' }] });
  assert.deepEqual(page.texts('printerList'), [['Garage P1S', '192.0.2.50'], ['C11', '192.0.2.51']]);
  page.get('printerList').children[0].listeners.click();
  assert.equal(page.get('printerip').value, '192.0.2.50');
  assert.equal(page.get('printerserial').value, '01P00A123456789');
  assert.equal(page.focused, 'printercode');
  // It keeps asking for 20 seconds in all, then stops.
  for (let i = 0; i < 8; ++i) {
    await page.advance(2000);
    await page.respond('/printers', 200, { listening: true, printers: [] });
  }
  await page.advance(2000);
  assert.equal(page.fetches.filter(request => request.url === '/printers').length, 10);
  assert.deepEqual(page.texts('printerList'), [
    'No printers answered. Check that the printer is on and on the same network, or enter it below.']);
});

test('on the setup network it explains why, scans at once, and hides printer discovery', async () => {
  let page = await readyPage({ ...saved, ssid: '', passwordSet: false, accessPoint: true });
  assert.equal(page.get('apIntro').hidden, false);
  assert.match(page.get('apIntro').textContent, /not on a Wi-Fi network yet/);
  assert.equal(page.get('backLink').hidden, true);
  assert.equal(page.get('restoreLink').hidden, false);
  assert.equal(page.get('findPrinters').hidden, true);
  assert.equal(page.get('discoveryNote').hidden, false);
  page.pending('/wifiScan?start=1');
  page.get('ssid').value = 'home';
  page.get('wifiPassword').value = 'home-password';
  await page.trigger('setupForm', 'submit');
  assert.deepEqual(page.confirmations, []);
  await page.respond('/submitWifiSetup', 200, 'Saved. The controller is restarting.');
  assert.match(page.get('afterSave').textContent, /^Reconnect this device to home, then open http:\/\/blflow\.local\//);
  page = await readyPage({ ...saved, accessPoint: true });
  assert.match(page.get('apIntro').textContent, /could not join home/);
});

test('a page that cannot load the setup stays disabled', async () => {
  const page = loadPage();
  await page.respond('/getWifiSetup', 500, 'Failed');
  assert.equal(page.get('setupFields').disabled, true);
  assert.match(page.get('setup-status').textContent, /unable to load/i);
  await page.trigger('setupForm', 'submit');
  assert.equal(page.fetches.length, 1);
});
