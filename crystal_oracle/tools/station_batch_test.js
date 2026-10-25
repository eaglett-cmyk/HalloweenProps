#!/usr/bin/env node
// station_batch_test.js - checks the Crystals tab's batch provisioning against fake_plinth.js in
// headless Chrome: start a batch with colour 2, place two blank crystals and one already-ours
// crystal, read the tally. Same plumbing as station_e2e.js.
'use strict';
const { spawn } = require('child_process');
const http = require('http');
const fs = require('fs');
const path = require('path');
const STATION = path.join(__dirname, '..', 'station');
const OUT = path.join(__dirname, '.e2e');
const CHROME = process.env.CHROME || 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
fs.mkdirSync(OUT, { recursive: true });
const log = (...a) => console.log(`${new Date().toISOString().slice(11, 23)} ${a.join(' ')}`);
const sleep = ms => new Promise(r => setTimeout(r, ms));
function start(name, args, env) {
  const p = spawn(process.execPath, args, { cwd: STATION, env: { ...process.env, ...env }, stdio: ['pipe', 'pipe', 'pipe'] });
  p.stdout.on('data', d => String(d).trimEnd().split('\n').forEach(l => log(`[${name}] ${l}`)));
  p.stderr.on('data', d => String(d).trimEnd().split('\n').forEach(l => log(`[${name}!] ${l}`)));
  return p;
}
const getJson = url => new Promise((res, rej) => http.get(url, r => { let b = ''; r.on('data', c => b += c); r.on('end', () => { try { res(JSON.parse(b)); } catch (e) { rej(e); } }); }).on('error', rej));
class CDP {
  constructor(u) { this.ws = new WebSocket(u); this.id = 0; this.pending = new Map(); this.ready = new Promise((res, rej) => { this.ws.onopen = res; this.ws.onerror = rej; });
    this.ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id && this.pending.has(m.id)) { const { res, rej } = this.pending.get(m.id); this.pending.delete(m.id); m.error ? rej(new Error(m.error.message)) : res(m.result); }
      else if (m.method === 'Runtime.exceptionThrown') { const d = m.params.exceptionDetails; log('[page.EXCEPTION]', d.text, d.exception ? d.exception.description : ''); errors.push(d.text); }
      else if (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error') { const t = m.params.args.map(a => a.value ?? a.description).join(' '); log('[page.error]', t); errors.push(t); } }; }
  send(method, params = {}) { const id = ++this.id; this.ws.send(JSON.stringify({ id, method, params })); return new Promise((res, rej) => this.pending.set(id, { res, rej })); }
  async eval(expr) { const r = await this.send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.text + ' ' + (r.exceptionDetails.exception && r.exceptionDetails.exception.description)); return r.result.value; }
  async shot(name) { const r = await this.send('Page.captureScreenshot', { format: 'png' }); fs.writeFileSync(path.join(OUT, name + '.png'), Buffer.from(r.data, 'base64')); log(`shot ${name}.png`); }
}
const errors = [];
(async () => {
  const plinth = start('plinth', ['fake_plinth.js'], { FAKE_PLINTH_PORT: '8181' });
  const server = start('server', ['server.js'], { STATION_PORT: '8460' });
  const say = cmd => { log(`> plinth: ${cmd}`); plinth.stdin.write(cmd + '\n'); };
  await sleep(1200);
  const chrome = spawn(CHROME, ['--headless=new', '--remote-debugging-port=9334', '--window-size=1100,760', '--autoplay-policy=no-user-gesture-required', '--no-first-run', '--disable-gpu', '--mute-audio', `--user-data-dir=${path.join(OUT, 'profile2')}`, 'about:blank'], { stdio: 'ignore' });
  let cdp;
  try {
    let targets; for (let i = 0; i < 40 && !targets; i++) { try { targets = await getJson('http://127.0.0.1:9334/json'); } catch { await sleep(250); } }
    cdp = new CDP(targets.find(t => t.type === 'page').webSocketDebuggerUrl); await cdp.ready;
    await cdp.send('Page.enable'); await cdp.send('Runtime.enable');
    await cdp.send('Page.navigate', { url: 'http://127.0.0.1:8460/' });
    for (let i = 0; i < 60; i++) { await sleep(250); if (await cdp.eval(`!document.getElementById('start-btn').disabled`).catch(() => false)) break; }
    if (process.env.AUDIO_MEDIA) await cdp.eval(`localStorage.setItem('station.audioMedia', 'true'); true`);
    await cdp.eval(`document.getElementById('plinth-addr').value = '127.0.0.1:8181'; document.getElementById('start-btn').click(); true`);
    for (let i = 0; i < 120; i++) { if (await cdp.eval(`document.getElementById('start').classList.contains('hidden')`)) break; await sleep(250); }
    await sleep(2500);
    log('audio engine:', await cdp.eval(`window.station.audio.engine() + ' music=' + window.station.audio.musicPlaying() + ' voiceBusy=' + window.station.audio.voiceBusy()`));
    say('fresh'); await sleep(4000); log('during awaken: voiceBusy=', await cdp.eval(`window.station.audio.voiceBusy() + ' level=' + window.station.audio.voiceLevel() + ' remaining=' + window.station.audio.voiceRemainingMs()`)); say('lift'); await sleep(22000);
    await cdp.eval(`window.station.show.requestService(); true`); await sleep(1200);
    await cdp.eval(`[...document.querySelectorAll('#service .tab')].find(b => b.textContent === 'Crystals').click(); true`); await sleep(400);
    await cdp.eval(`[...document.querySelectorAll('#service .batch .btn.colour')][1].click(); true`); await sleep(200);   // colour 2
    await cdp.eval(`[...document.querySelectorAll('#service .batch .btn')].find(b => b.textContent === 'Start batch').click(); true`); await sleep(400);
    say('fresh'); await sleep(1500); say('dump'); await sleep(300); say('lift'); await sleep(800);
    say('fresh'); await sleep(1500); say('lift'); await sleep(800);
    say('ours'); await sleep(1500); say('lift'); await sleep(800);
    await cdp.shot('60-batch');
    log('batch tally:', await cdp.eval(`document.querySelector('#service .batch .msg').textContent`));
    await cdp.eval(`[...document.querySelectorAll('#service .batch .btn')].find(b => b.textContent === 'Stop batch').click(); true`); await sleep(300);
    await cdp.eval(`[...document.querySelectorAll('#service .tab')].find(b => b.textContent === 'Close').click(); true`); await sleep(1000);
  } catch (e) { log('FAILED', e.stack || e); errors.push(String(e)); }
  finally { try { chrome.kill(); } catch {} plinth.kill(); server.kill(); log(`done: ${errors.length} error(s)`); process.exit(errors.length ? 1 : 0); }
})();
