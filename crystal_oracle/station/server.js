// Crystal Oracle STATION server - serves the station web app (the show, for an iPad or any browser)
// and the TF-card assets it plays, plus a little state/log API. Dependency-free (node >= 18).
//
//   node server.js                      -> http://<this PC>:8460/   (all interfaces, so the iPad can reach it)
//   STATION_PORT=8461 node server.js    -> another port
//   STATION_ASSETS=D:\oracle node ...   -> serve the assets from a TF card instead of ../sd_card/oracle
//
// Routes
//   /                      the app (index.html, style.css, js/*)
//   /oracle/<...>          the card: clips/*.omj|.jpg, voice/*.wav, sfx/*.wav, music/*.wav, config.ini, captions.ini
//   /api/index             JSON listing of the card's clips / voice / sfx / music (the app's "name cache")
//   /api/state  GET/PUT    the station's counters (state/state.json), so they survive a cleared browser
//   /api/log    POST       {file, line} appended under logs/ (visits_<year>.csv, events.log)
//   /health                "ok"
//
// The app must be loaded over plain http (not https): Safari refuses a ws:// plinth from an https page.
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
// The card it serves: STATION_ASSETS, else the iPad build (tools/build_assets.py --ipad -> sd_card_ipad:
// full-colour, high-quality clips) when it exists, else the Waveshare TF card build (sd_card).
const IPAD_ASSETS = path.join(ROOT, '..', 'sd_card_ipad', 'oracle');
const ASSETS = path.resolve(process.env.STATION_ASSETS ||
  (fs.existsSync(path.join(IPAD_ASSETS, 'clips')) ? IPAD_ASSETS : path.join(ROOT, '..', 'sd_card', 'oracle')));
const STATE_DIR = path.join(ROOT, 'state');
const SITE_BEACON = path.resolve(process.env.SITE_BEACON || path.join(ROOT, '..', '..', 'site', 'data', 'beacon.json'));
const WEB_MAX = 100;   // the site's cap on web sparks (site/server.js WEB_MAX)
const LOG_DIR = path.join(ROOT, 'logs');
const PORT = Number(process.env.STATION_PORT || 8460);

const TYPES = {
  '.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8', '.ini': 'text/plain; charset=utf-8', '.txt': 'text/plain; charset=utf-8',
  '.wav': 'audio/wav', '.omj': 'application/octet-stream', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg',
  '.png': 'image/png', '.webp': 'image/webp', '.svg': 'image/svg+xml', '.ico': 'image/x-icon', '.woff2': 'font/woff2',
};

function send(res, code, body, type = 'text/plain; charset=utf-8', extra = {}) {
  const buf = Buffer.isBuffer(body) ? body : Buffer.from(String(body));
  res.writeHead(code, { 'Content-Type': type, 'Content-Length': buf.length, 'Cache-Control': 'no-store', ...extra });
  res.end(buf);
}

// A file inside `root` only (no dotfiles, no escaping), with ETag / 304 and byte ranges (Safari asks for them on media).
function serveFile(req, res, root, rel, cache) {
  const file = path.normalize(path.join(root, rel));
  const r = path.relative(root, file);
  if (!r || r.startsWith('..') || path.isAbsolute(r) || r.split(path.sep).some(s => s.startsWith('.'))) { send(res, 404, 'not found'); return; }
  const type = TYPES[path.extname(file).toLowerCase()];
  if (!type) { send(res, 404, 'not found'); return; }
  fs.stat(file, (err, st) => {
    if (err || !st.isFile()) { send(res, 404, 'not found'); return; }
    const etag = `"${st.size.toString(16)}-${Math.floor(st.mtimeMs).toString(16)}"`;
    const headers = { 'Content-Type': type, 'ETag': etag, 'Accept-Ranges': 'bytes', 'Cache-Control': cache };
    if (req.headers['if-none-match'] === etag) { res.writeHead(304, headers); res.end(); return; }
    let start = 0, end = st.size - 1, code = 200;
    const m = /^bytes=(\d*)-(\d*)$/.exec(req.headers.range || '');
    if (m && (m[1] || m[2])) {
      start = m[1] ? Number(m[1]) : Math.max(0, st.size - Number(m[2]));
      end = m[1] && m[2] ? Math.min(Number(m[2]), st.size - 1) : (m[1] ? st.size - 1 : st.size - 1);
      if (start > end || start >= st.size) { res.writeHead(416, { 'Content-Range': `bytes */${st.size}` }); res.end(); return; }
      code = 206;
      headers['Content-Range'] = `bytes ${start}-${end}/${st.size}`;
    }
    headers['Content-Length'] = end - start + 1;
    res.writeHead(code, headers);
    if (req.method === 'HEAD') { res.end(); return; }
    fs.createReadStream(file, { start, end }).pipe(res);
  });
}

function listDir(sub, exts) {
  try {
    return fs.readdirSync(path.join(ASSETS, sub)).filter(f => exts.includes(path.extname(f).toLowerCase())).sort();
  } catch { return []; }
}

function readBody(req, max = 256 * 1024) {
  return new Promise((resolve, reject) => {
    const chunks = []; let n = 0;
    req.on('data', c => { n += c.length; if (n > max) { reject(new Error('too large')); req.destroy(); } else chunks.push(c); });
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

async function handler(req, res) {
  let url;
  try { url = new URL(req.url, 'http://x'); } catch { send(res, 400, 'bad url'); return; }
  const p = decodeURIComponent(url.pathname);

  if (p === '/health') { send(res, 200, 'ok'); return; }
  if (p === '/favicon.ico' || p === '/favicon.svg') {
    send(res, 200, '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><path d="M32 4 52 24 32 60 12 24Z" fill="#a78bfa"/><path d="M32 4 52 24H12Z" fill="#ddd6fe"/></svg>',
         'image/svg+xml', { 'Cache-Control': 'public, max-age=86400' });
    return;
  }

  if (p === '/api/index') {
    const assetsOk = fs.existsSync(ASSETS);
    send(res, 200, JSON.stringify({
      assets: ASSETS, assetsOk,
      clips: listDir('clips', ['.omj', '.jpg', '.jpeg']),
      voice: listDir('voice', ['.wav']), sfx: listDir('sfx', ['.wav']), music: listDir('music', ['.wav']),
      hasConfig: fs.existsSync(path.join(ASSETS, 'config.ini')), hasCaptions: fs.existsSync(path.join(ASSETS, 'captions.ini')),
      serverTime: Date.now(),
    }), 'application/json; charset=utf-8');
    return;
  }

  if (p === '/api/state') {
    const file = path.join(STATE_DIR, 'state.json');
    if (req.method === 'GET') {
      fs.readFile(file, 'utf8', (err, txt) => send(res, 200, err ? '{}' : txt, 'application/json; charset=utf-8'));
      return;
    }
    if (req.method === 'PUT' || req.method === 'POST') {
      try {
        const body = await readBody(req);
        JSON.parse(body);                               // must be JSON
        fs.mkdirSync(STATE_DIR, { recursive: true });
        fs.writeFileSync(file + '.tmp', body);
        fs.renameSync(file + '.tmp', file);
        send(res, 200, '{"ok":true}', 'application/json; charset=utf-8');
      } catch (e) { send(res, 400, JSON.stringify({ error: String(e.message || e) }), 'application/json; charset=utf-8'); }
      return;
    }
    res.writeHead(405, { Allow: 'GET, PUT' }); res.end(); return;
  }

  // [station] web_beacon: the website games' light for the meter. The site (..\..\site, served
  // by the VMS server on this same PC) keeps it in data/beacon.json; only {year, web} leaves here.
  if (p === '/api/web_beacon') {
    let out = { year: 0, web: 0 };
    try {
      const b = JSON.parse(fs.readFileSync(SITE_BEACON, 'utf8'));
      out = { year: b.year | 0, web: Math.max(0, Math.min(WEB_MAX, b.sparks | 0)) };
    } catch { /* no site data: nothing from the web */ }
    send(res, 200, JSON.stringify(out), 'application/json; charset=utf-8');
    return;
  }

  if (p === '/api/log' && req.method === 'POST') {
    try {
      const { file, line } = JSON.parse(await readBody(req));
      if (!/^[a-z0-9_]+\.(csv|log)$/i.test(file || '') || typeof line !== 'string') throw new Error('bad request');
      fs.mkdirSync(LOG_DIR, { recursive: true });
      fs.appendFileSync(path.join(LOG_DIR, file), line.replace(/\r?\n?$/, '\n'));
      send(res, 200, '{"ok":true}', 'application/json; charset=utf-8');
    } catch (e) { send(res, 400, JSON.stringify({ error: String(e.message || e) }), 'application/json; charset=utf-8'); }
    return;
  }

  if (req.method !== 'GET' && req.method !== 'HEAD') { res.writeHead(405, { Allow: 'GET, HEAD' }); res.end(); return; }

  if (p.startsWith('/oracle/')) {           // the card: big immutable-ish media, cache a day (ETag revalidates)
    serveFile(req, res, ASSETS, p.slice('/oracle/'.length), p.endsWith('.ini') ? 'no-cache' : 'public, max-age=86400');
    return;
  }
  let rel = p === '/' ? 'index.html' : p.slice(1);
  if (rel === 'server.js' || rel === 'fake_plinth.js' || rel.startsWith('state/') || rel.startsWith('logs/')) { send(res, 404, 'not found'); return; }
  serveFile(req, res, ROOT, rel, 'no-cache');
}

module.exports = { handler };

if (require.main === module) {
  http.createServer((req, res) => { handler(req, res).catch(e => { console.error(e); try { send(res, 500, 'error'); } catch {} }); })
    .listen(PORT, '0.0.0.0', () => {
      const os = require('os');
      const ips = Object.values(os.networkInterfaces()).flat().filter(i => i && i.family === 'IPv4' && !i.internal).map(i => i.address);
      console.log(`Crystal Oracle station: http://127.0.0.1:${PORT}/  ${ips.map(ip => `http://${ip}:${PORT}/`).join('  ')}`);
      console.log(`assets: ${ASSETS} ${fs.existsSync(ASSETS) ? '' : '(MISSING - run tools/build_assets.py or set STATION_ASSETS)'}`);
    });
}
