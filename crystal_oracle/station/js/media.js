// media.js - the "TF card" over HTTP: the asset index (the firmware's name cache), OMJ clips
// decoded frame by frame in the browser, stills, and a background prefetch into memory.
//   /oracle/clips/<id>[_<n>].omj | .jpg     /oracle/voice/<group>_<n>.wav
//   /oracle/sfx/<id>.wav                    /oracle/music/<id>.wav
// OMJ1 (media.h): "OMJ1" u16 w, u16 h, u16 fps*100, u16 0, u32 frames, u32 maxFrameBytes, then per
// frame u32 len + JPEG + pad to 4. Frames are independent JPEGs: late frames are skipped.
const s_index = { ok: false, assetsOk: false, dirs: { clips: new Set(), voice: new Set(), sfx: new Set(), music: new Set() }, hasConfig: false, hasCaptions: false };
const s_buffers = new Map();    // path -> ArrayBuffer | Promise<ArrayBuffer>
const s_stills = new Map();     // path -> ImageBitmap | 'loading' | 'failed'
let s_bytesLoaded = 0;

async function fetchBuffer(path) {
  // 'no-cache' = always revalidate (ETag -> 304 when unchanged), so a rebuilt card shows up on the next load
  const r = await fetch(path, { cache: 'no-cache' });
  if (!r.ok) throw new Error(`${path}: HTTP ${r.status}`);
  const b = await r.arrayBuffer();
  s_bytesLoaded += b.byteLength;
  return b;
}

export const media = {
  async load() {
    try {
      const r = await fetch('/api/index', { cache: 'no-store' });
      const j = await r.json();
      s_index.ok = true;
      s_index.assetsOk = !!j.assetsOk;
      s_index.hasConfig = !!j.hasConfig; s_index.hasCaptions = !!j.hasCaptions;
      for (const d of ['clips', 'voice', 'sfx', 'music']) s_index.dirs[d] = new Set((j[d] || []).map(s => s.toLowerCase()));
    } catch (e) {
      console.warn('media: no asset index', e);
      s_index.ok = false; s_index.assetsOk = false;
    }
    return s_index.assetsOk;
  },
  sdOk() { return s_index.assetsOk; },
  hasConfig() { return s_index.hasConfig; },
  hasCaptions() { return s_index.hasCaptions; },
  bytesLoaded() { return s_bytesLoaded; },
  // "/oracle/voice/x.wav" -> is it on the card?
  exists(path) {
    const m = /^\/oracle\/(clips|voice|sfx|music)\/([^/]+)$/i.exec(path);
    return !!m && s_index.dirs[m[1].toLowerCase()].has(m[2].toLowerCase());
  },
  // Under /oracle/clips, in order: <id>_<variant>.omj, <id>_<variant>.jpg, <id>.omj, <id>.jpg
  findVisual(id, variant = 0) {
    const clips = s_index.dirs.clips;
    const tries = [];
    if (variant) tries.push([`${id}_${variant}.omj`, 'clip'], [`${id}_${variant}.jpg`, 'still']);
    tries.push([`${id}.omj`, 'clip'], [`${id}.jpg`, 'still']);
    for (const [f, kind] of tries) if (clips.has(f.toLowerCase())) return { kind, path: `/oracle/clips/${f}`, stem: f.replace(/\.[^.]+$/, '') };
    return { kind: 'none', path: '', stem: '' };
  },
  // consecutive <id>_1.<ext> .. _9.<ext> in <dirRel>
  countVariants(dirRel, id, ext) {
    const d = s_index.dirs[dirRel];
    if (!d) return 0;
    let n = 0;
    while (n < 9 && d.has(`${id}_${n + 1}.${ext}`.toLowerCase())) n++;
    return n;
  },
  assetCount() { return Object.values(s_index.dirs).reduce((a, s) => a + s.size, 0); },
  listDir(dirRel) { return Array.from(s_index.dirs[dirRel] || []); },

  // Loads (and keeps) a file's bytes. Repeated calls share one fetch.
  getBuffer(path) {
    let b = s_buffers.get(path);
    if (b) return b instanceof Promise ? b : Promise.resolve(b);
    const p = fetchBuffer(path).then(buf => { s_buffers.set(path, buf); return buf; }).catch(e => { s_buffers.delete(path); throw e; });
    s_buffers.set(path, p);
    return p;
  },
  isLoaded(path) { const b = s_buffers.get(path); return !!b && !(b instanceof Promise); },
  // Fetches a list of paths one after another in the background (idle time); never throws.
  async prefetch(paths, onEach) {
    for (const p of paths) {
      if (!p || s_buffers.has(p)) continue;
      try { await this.getBuffer(p); if (onEach) onEach(p); } catch (e) { console.warn('prefetch', p, e.message); }
    }
  },

  // Draws an 800x480 still if it is decoded; starts decoding otherwise. true = drawn.
  drawStill(path, ctx) {
    let s = s_stills.get(path);
    if (s instanceof ImageBitmap) { ctx.drawImage(s, 0, 0, 800, 480); return true; }
    if (s === 'failed') return false;
    if (!s) {
      s_stills.set(path, 'loading');
      this.getBuffer(path).then(buf => createImageBitmap(new Blob([buf], { type: 'image/jpeg' })))
        .then(bm => s_stills.set(path, bm)).catch(e => { console.warn('still', path, e.message); s_stills.set(path, 'failed'); });
    }
    return false;
  },
  stillFailed(path) { return s_stills.get(path) === 'failed'; },
  Clip: null,   // set below
};

// ---------------------------------------------------------------- OMJ clips
export class Clip {
  constructor() { this._open = false; this._buf = null; this._frames = 0; this._fps100 = 0; this._w = 0; this._h = 0; this._offs = null; this._lens = null;
                  this._bitmap = null; this._shownIdx = -1; this._decoding = false; this._wantIdx = 0; this._fails = 0; this._path = ''; this._gen = 0; }
  async open(path) {
    this.close();
    this._gen++;
    const gen = this._gen;
    let buf;
    try { buf = await media.getBuffer(path); } catch (e) { console.warn('clip', path, e.message); return false; }
    if (gen !== this._gen) return false;                  // closed / reopened meanwhile
    const v = new DataView(buf);
    if (buf.byteLength < 20 || v.getUint8(0) !== 0x4F || v.getUint8(1) !== 0x4D || v.getUint8(2) !== 0x4A || v.getUint8(3) !== 0x31) { console.warn('clip', path, 'not OMJ1'); return false; }
    this._w = v.getUint16(4, true); this._h = v.getUint16(6, true); this._fps100 = v.getUint16(8, true);
    const frames = v.getUint32(12, true);
    const offs = new Uint32Array(frames), lens = new Uint32Array(frames);
    let o = 20, n = 0;
    while (n < frames && o + 4 <= buf.byteLength) {
      const len = v.getUint32(o, true);
      if (o + 4 + len > buf.byteLength) break;
      offs[n] = o + 4; lens[n] = len; n++;
      o += 4 + len; o = (o + 3) & ~3;
    }
    if (!n) return false;
    this._buf = buf; this._frames = n; this._offs = offs; this._lens = lens; this._path = path;
    this._open = true; this._shownIdx = -1; this._wantIdx = 0; this._fails = 0;
    this._decode(0, gen);
    return true;
  }
  close() {
    this._open = false; this._gen++;
    if (this._bitmap) { try { this._bitmap.close(); } catch {} this._bitmap = null; }
    this._buf = null; this._shownIdx = -1; this._decoding = false;
  }
  isOpen() { return this._open; }
  frames() { return this._frames; }
  fps() { return this._fps100 / 100; }
  frameMs() { return this._fps100 ? Math.round(100000 / this._fps100) : 77; }
  durationMs() { return this._fps100 ? Math.round(this._frames * 100000 / this._fps100) : 0; }
  _decode(idx, gen) {
    if (this._decoding || !this._buf) return;
    this._decoding = true;
    const slice = new Uint8Array(this._buf, this._offs[idx], this._lens[idx]);
    createImageBitmap(new Blob([slice], { type: 'image/jpeg' })).then(bm => {
      if (gen !== this._gen) { bm.close(); return; }
      if (this._bitmap) { try { this._bitmap.close(); } catch {} }
      this._bitmap = bm; this._shownIdx = idx; this._decoding = false; this._fails = 0;
      if (this._wantIdx !== idx) this._decode(this._wantIdx, gen);     // already late: keep going
    }).catch(e => {
      if (gen !== this._gen) return;
      this._decoding = false; this._fails++;
      if (this._fails <= 3) console.warn('clip frame', this._path, idx, e.message);
    });
  }
  // Draws the frame due at elapsedMs (loop wraps, else the last frame holds). false only after repeated decode failures.
  render(elapsedMs, loop, ctx) {
    if (!this._open) return false;
    const dur = this.durationMs();
    let idx = Math.floor(elapsedMs / this.frameMs());
    if (loop && dur) idx %= this._frames; else if (idx >= this._frames) idx = this._frames - 1;
    if (idx < 0) idx = 0;
    this._wantIdx = idx;
    if (idx !== this._shownIdx && !this._decoding) this._decode(idx, this._gen);
    if (this._bitmap) { ctx.drawImage(this._bitmap, 0, 0, 800, 480); return true; }
    return this._fails < 3;
  }
}
media.Clip = Clip;
