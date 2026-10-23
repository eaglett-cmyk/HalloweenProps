// halloween.ciancio.cloud — the Dragonglow storybook landing page the crystals link to.
// Plain static files from this folder, no auth, no dependencies.
//
// Normally this is NOT run on its own: ..\..\VMS-Checklist\server.js requires it and
// dispatches requests whose Host is halloween.ciancio.cloud to `handler` (same pattern
// as beacon). Run directly only for local testing:  node server.js  → http://127.0.0.1:8470/
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.webp': 'image/webp',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.txt': 'text/plain; charset=utf-8',
};

function handler(req, res) {
  if (req.method !== 'GET' && req.method !== 'HEAD') {
    res.writeHead(405, { Allow: 'GET, HEAD' }); res.end(); return;
  }
  let urlPath;
  try { urlPath = decodeURIComponent(new URL(req.url, 'http://x').pathname); }
  catch { res.writeHead(400); res.end(); return; }
  if (urlPath === '/health') { res.writeHead(200, { 'Content-Type': 'text/plain' }); res.end('ok'); return; }
  if (urlPath.endsWith('/')) urlPath += 'index.html';

  // Resolve inside ROOT only; never serve this file or dotfiles.
  const file = path.normalize(path.join(ROOT, urlPath));
  const rel = path.relative(ROOT, file);
  if (!rel || rel.startsWith('..') || path.isAbsolute(rel) || rel.split(path.sep).some(s => s.startsWith('.')) || rel === 'server.js') {
    res.writeHead(404); res.end('not found'); return;
  }
  const ext = path.extname(file).toLowerCase();
  const type = TYPES[ext];
  if (!type) { res.writeHead(404); res.end('not found'); return; }

  fs.stat(file, (err, st) => {
    if (err || !st.isFile()) { res.writeHead(404); res.end('not found'); return; }
    const etag = `"${st.size.toString(16)}-${Math.floor(st.mtimeMs).toString(16)}"`;
    const headers = {
      'Content-Type': type,
      'Content-Length': st.size,
      'ETag': etag,
      // HTML is what we edit; images/css are fingerprint-free, so keep their cache modest.
      'Cache-Control': ext === '.html' ? 'no-cache' : 'public, max-age=86400',
    };
    if (req.headers['if-none-match'] === etag) { res.writeHead(304, headers); res.end(); return; }
    res.writeHead(200, headers);
    if (req.method === 'HEAD') { res.end(); return; }
    fs.createReadStream(file).pipe(res);
  });
}

module.exports = { handler };

if (require.main === module) {
  const port = Number(process.env.HALLOWEEN_PORT || 8470);
  http.createServer(handler).listen(port, '127.0.0.1', () =>
    console.log(`Dragonglow storybook: http://127.0.0.1:${port}/`));
}
