#!/usr/bin/env node
// station_e2e.js - drives the station web app end to end in headless Chrome without hardware:
// starts station/fake_plinth.js (:8181) and station/server.js (:8460), opens the app over CDP,
// taps "begin", places crystals of every kind through the fake plinth, screenshots along the
// way, and reports console errors. Dependency-free (node >= 22 for the global WebSocket).
//
//   node tools/station_e2e.js [--keep]      screenshots -> tools/.e2e/*.png, log -> tools/.e2e/log.txt
'use strict';
const { spawn } = require('child_process');
const http = require('http');
const fs = require('fs');
const path = require('path');

const STATION = path.join(__dirname, '..', 'station');
const OUT = path.join(__dirname, '.e2e');
const CHROME = process.env.CHROME || 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const KEEP = process.argv.includes('--keep');
fs.mkdirSync(OUT, { recursive: true });
const logLines = [];
const log = (...a) => { const s = `${new Date().toISOString().slice(11, 23)} ${a.join(' ')}`; console.log(s); logLines.push(s); };
const sleep = ms => new Promise(r => setTimeout(r, ms));

function start(name, args, env) {
  const p = spawn(process.execPath, args, { cwd: STATION, env: { ...process.env, ...env }, stdio: ['pipe', 'pipe', 'pipe'] });
  p.stdout.on('data', d => String(d).trimEnd().split('\n').forEach(l => log(`[${name}] ${l}`)));
  p.stderr.on('data', d => String(d).trimEnd().split('\n').forEach(l => log(`[${name}!] ${l}`)));
  return p;
}
function getJson(url) {
  return new Promise((res, rej) => http.get(url, r => { let b = ''; r.on('data', c => b += c); r.on('end', () => { try { res(JSON.parse(b)); } catch (e) { rej(e); } }); }).on('error', rej));
}

class CDP {
  constructor(wsUrl) { this.ws = new WebSocket(wsUrl); this.id = 0; this.pending = new Map(); this.events = []; this.handlers = [];
    this.ready = new Promise((res, rej) => { this.ws.onopen = res; this.ws.onerror = rej; });
    this.ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id && this.pending.has(m.id)) { const { res, rej } = this.pending.get(m.id); this.pending.delete(m.id); m.error ? rej(new Error(m.error.message)) : res(m.result); }
      else if (m.method) { this.events.push(m); for (const h of this.handlers) h(m); } }; }
  send(method, params = {}) { const id = ++this.id; this.ws.send(JSON.stringify({ id, method, params })); return new Promise((res, rej) => this.pending.set(id, { res, rej })); }
  on(h) { this.handlers.push(h); }
  async eval(expr) { const r = await this.send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.text + ' ' + (r.exceptionDetails.exception && r.exceptionDetails.exception.description)); return r.result.value; }
  async shot(name) { const r = await this.send('Page.captureScreenshot', { format: 'png' }); fs.writeFileSync(path.join(OUT, name + '.png'), Buffer.from(r.data, 'base64')); log(`shot ${name}.png`); }
}

(async () => {
  const errors = [];
  const plinth = start('plinth', ['fake_plinth.js'], { FAKE_PLINTH_PORT: '8181' });
  const server = start('server', ['server.js'], { STATION_PORT: '8460' });
  const say = cmd => { log(`> plinth: ${cmd}`); plinth.stdin.write(cmd + '\n'); };
  await sleep(1200);
  const chrome = spawn(CHROME, ['--headless=new', '--remote-debugging-port=9333', '--window-size=1100,760', '--autoplay-policy=no-user-gesture-required',
    '--no-first-run', '--no-default-browser-check', '--disable-gpu', '--mute-audio', `--user-data-dir=${path.join(OUT, 'profile')}`, 'about:blank'], { stdio: 'ignore' });
  let cdp;
  try {
    let targets;
    for (let i = 0; i < 40 && !targets; i++) { try { targets = await getJson('http://127.0.0.1:9333/json'); } catch { await sleep(250); } }
    const page = targets.find(t => t.type === 'page');
    cdp = new CDP(page.webSocketDebuggerUrl);
    await cdp.ready;
    await cdp.send('Page.enable'); await cdp.send('Runtime.enable'); await cdp.send('Log.enable');
    cdp.on(m => {
      if (m.method === 'Runtime.consoleAPICalled') { const txt = m.params.args.map(a => a.value !== undefined ? String(a.value) : (a.description || a.type)).join(' '); log(`[page.${m.params.type}] ${txt}`); if (m.params.type === 'error') errors.push(txt); }
      if (m.method === 'Runtime.exceptionThrown') { const d = m.params.exceptionDetails; const txt = `${d.text} ${d.exception ? d.exception.description : ''} @${d.url}:${d.lineNumber}`; log(`[page.EXCEPTION] ${txt}`); errors.push(txt); }
      if (m.method === 'Log.entryAdded' && m.params.entry.level === 'error') { log(`[page.log] ${m.params.entry.text} ${m.params.entry.url || ''}`); errors.push(m.params.entry.text); }
    });
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1100, height: 760, deviceScaleFactor: 1, mobile: false });
    await cdp.send('Page.navigate', { url: 'http://127.0.0.1:8460/' });
    await sleep(1500);
    for (let i = 0; i < 40; i++) { if (await cdp.eval(`!document.getElementById('start-btn').disabled`)) break; await sleep(250); }
    log('status:', await cdp.eval(`document.getElementById('start-status').textContent`));
    await cdp.shot('00-boot');
    await cdp.eval(`document.getElementById('plinth-addr').value = '127.0.0.1:8181'; document.getElementById('start-btn').click(); true`);
    for (let i = 0; i < 120; i++) { if (await cdp.eval(`document.getElementById('start').classList.contains('hidden')`)) break; await sleep(250); }
    log('started; status:', await cdp.eval(`document.getElementById('start-status').textContent`));
    await sleep(2500);
    log('state:', JSON.stringify(await cdp.eval(`window.station.show.state()`)), 'link:', await cdp.eval(`window.station.link.state() + ' online=' + window.station.link.online()`));
    await cdp.shot('01-attract');
    await sleep(3000);
    await cdp.shot('02-attract-later');

    // a fresh crystal: absorb -> awaken (3 lines) -> hold -> reveal + bowl -> goodbye
    say('fresh');
    const shots = [[1500, '10-absorb'], [4000, '11-awaken'], [9000, '12-awaken-later'], [21000, '13-reveal'], [26000, '14-after']];
    let t = 0;
    for (const [at, name] of shots) { await sleep(at - t); t = at; log('state:', JSON.stringify(await cdp.eval(`window.station.show.state()`))); await cdp.shot(name); }
    say('dump');
    await sleep(500);
    say('lift');
    await sleep(2500);

    // the same crystal again = already glowed
    say('again');
    await sleep(3500); await cdp.shot('20-replay');
    await sleep(6000); say('lift'); await sleep(2000);

    // a returning crystal with rush forced OFF: greet -> recall -> charge (taps) -> reveal
    await cdp.eval(`window.station.settings.cfg.rush = 2; true`);
    say('ours');
    await sleep(2500); await cdp.shot('30-greet');
    await sleep(7000); await cdp.shot('31-recall-or-charge');
    for (let i = 0; i < 6; i++) { await cdp.eval(`(() => { const c = document.getElementById('show'); const r = c.getBoundingClientRect(); const o = { clientX: r.left + r.width/2, clientY: r.top + r.height/2, pointerId: 1, pointerType: 'touch', bubbles: true, isPrimary: true }; c.dispatchEvent(new PointerEvent('pointerdown', o)); c.dispatchEvent(new PointerEvent('pointerup', o)); return true; })()`); await sleep(350); }
    await cdp.shot('32-charge-taps');
    await sleep(4000); await cdp.shot('33-reveal');
    await sleep(6000); await cdp.shot('34-goodbye');
    await sleep(6000); say('lift'); await sleep(2000);
    await cdp.eval(`window.station.settings.cfg.rush = 0; true`);

    // the finale: one point short of the goal, then a button blessing fills the meter
    await cdp.eval(`window.station.settings.counters.moonPoints = window.station.settings.cfg.moonGoal - 1; true`);
    say('button');
    await sleep(6000); await cdp.shot('35-blessing-before-finale');
    await sleep(7000); await cdp.shot('36-finale');
    await sleep(12000); await cdp.shot('37-attract-full');

    // foreign + unreadable + a tap blessing
    say('foreign'); await sleep(4000); await cdp.shot('40-foreign'); await sleep(8000); say('lift'); await sleep(1500);
    say('bad'); await sleep(2500); await cdp.shot('41-sleepy'); await sleep(7000); say('lift'); await sleep(7000);
    await cdp.eval(`(() => { const c = document.getElementById('show'); const r = c.getBoundingClientRect(); const o = { clientX: r.left + r.width*0.6, clientY: r.top + r.height*0.5, pointerId: 1, pointerType: 'touch', bubbles: true, isPrimary: true }; c.dispatchEvent(new PointerEvent('pointerdown', o)); c.dispatchEvent(new PointerEvent('pointerup', o)); return true; })()`);
    await sleep(3000); await cdp.shot('42-tap-blessing'); await sleep(9000);

    // host menu
    await cdp.eval(`window.station.show.requestService(); true`);
    await sleep(1500); await cdp.shot('50-menu-tonight');
    await cdp.eval(`[...document.querySelectorAll('#service .tab')].find(b => b.textContent === 'Crystals').click(); true`);
    say('ours'); await sleep(1200); await cdp.shot('51-menu-crystals');
    await cdp.eval(`[...document.querySelectorAll('#service .tab')].find(b => b.textContent === 'Reader').click(); true`);
    await sleep(600); await cdp.shot('52-menu-reader');
    say('lift');
    await cdp.eval(`[...document.querySelectorAll('#service .tab')].find(b => b.textContent === 'Close').click(); true`);
    await sleep(2500); await cdp.shot('53-after-menu');
    log('counters:', JSON.stringify(await cdp.eval(`JSON.stringify(window.station.settings.counters)`)));
    log('recent events:', JSON.stringify(await cdp.eval(`window.station.vlog.recent().slice(-12)`), null, 0));
  } catch (e) {
    log('E2E FAILED:', e.stack || e);
    errors.push(String(e));
  } finally {
    if (!KEEP) { try { chrome.kill(); } catch {} plinth.kill(); server.kill(); }
    fs.writeFileSync(path.join(OUT, 'log.txt'), logLines.join('\n'));
    log(`done: ${errors.length} page error(s)`);
    errors.forEach(e => log('  ERROR:', e));
    if (!KEEP) process.exit(errors.length ? 1 : 0);
  }
})();
