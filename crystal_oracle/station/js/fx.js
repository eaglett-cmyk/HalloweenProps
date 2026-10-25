// fx.js - everything the show draws LIVE on top of (or instead of) the clips, on an 800x480
// canvas (a port of fx.h / display.h to Canvas 2D): procedural backgrounds, additive glows,
// sparkles, the gem meter, the big number, titles, bowl icons, the caption band, the Moon /
// lantern meter, poof / smudge / beam / check mark. Colours are 0xRRGGBB like the firmware.
export const W = 800, H = 480;
const FONT = '"Arial Black", "Arial Rounded MT Bold", Impact, "Trebuchet MS", Arial, Helvetica, sans-serif';
const WHITE = 0xFFFFFF;
let g = null;                 // the 2D context of the frame being drawn
let s_vignette = null;        // cached vignette canvas

export const css = (rgb, a = 1) => `rgba(${(rgb >> 16) & 255},${(rgb >> 8) & 255},${rgb & 255},${a})`;
export const clamp8 = v => (v < 0 ? 0 : v > 255 ? 255 : Math.round(v));
export const clamp01 = v => (v < 0 ? 0 : v > 1 ? 1 : v);
export function mixRgb(a, b, t) {   // t 0..255 towards b
  const k = t / 255;
  const ch = s => Math.round(((a >> s) & 255) + ((((b >> s) & 255) - ((a >> s) & 255)) * k));
  return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}
export function scaleRgb(c, k255) { const k = k255 / 255; return (Math.round(((c >> 16) & 255) * k) << 16) | (Math.round(((c >> 8) & 255) * k) << 8) | Math.round((c & 255) * k); }
export const wave = (tMs, periodMs, phase = 0) => 0.5 + 0.5 * Math.sin(2 * Math.PI * ((tMs % periodMs) / periodMs) + phase);
const TAU = Math.PI * 2;
let s_rng = 0x9E3779B9;
export function rnd() { s_rng ^= s_rng << 13; s_rng >>>= 0; s_rng ^= s_rng >>> 17; s_rng ^= s_rng << 5; s_rng >>>= 0; return s_rng; }
const rndf = () => (rnd() >>> 8) / 16777216;
function hash32(x) { x = Math.imul(x ^ (x >>> 16), 0x7FEB352D) >>> 0; x = Math.imul(x ^ (x >>> 15), 0x846CA68B) >>> 0; return (x ^ (x >>> 16)) >>> 0; }

export const Shape = { Circle: 0, Triangle: 1, Star: 2 };
export function shapeFromName(n) { n = String(n || '').trim().toLowerCase(); return n === 'triangle' ? Shape.Triangle : n === 'star' ? Shape.Star : Shape.Circle; }

// ---------------------------------------------------------------- frame + primitives (display.h)
export function bind(ctx) { g = ctx; }
export function ctx() { return g; }
export function fill(rgb) { g.globalCompositeOperation = 'source-over'; g.fillStyle = css(rgb); g.fillRect(0, 0, W, H); }
export function darkenRect(x, y, w, h, level) { g.globalCompositeOperation = 'source-over'; g.fillStyle = `rgba(0,0,0,${level / 255})`; g.fillRect(x, y, w, h); }
// Additive soft glow: brightest at the centre, gone at r.
export function addGlow(cx, cy, r, rgb, level) {
  if (r <= 0 || level <= 0) return;
  const a = clamp01(level / 255);
  const grad = g.createRadialGradient(cx, cy, 0, cx, cy, r);
  grad.addColorStop(0, css(rgb, a)); grad.addColorStop(0.45, css(rgb, a * 0.38)); grad.addColorStop(1, css(rgb, 0));
  g.globalCompositeOperation = 'lighter';
  g.fillStyle = grad;
  g.fillRect(cx - r, cy - r, r * 2, r * 2);
  g.globalCompositeOperation = 'source-over';
}
function softDisc(cx, cy, r, vScale, rgb, alpha255) {
  g.save(); g.translate(cx, cy); g.scale(1, vScale);
  const grad = g.createRadialGradient(0, 0, 0, 0, 0, r);
  const a = alpha255 / 255;
  grad.addColorStop(0, css(rgb, a)); grad.addColorStop(0.6, css(rgb, a * 0.9)); grad.addColorStop(1, css(rgb, 0));
  g.fillStyle = grad; g.globalCompositeOperation = 'source-over';
  g.beginPath(); g.arc(0, 0, r, 0, TAU); g.fill();
  g.restore();
}

// ---- text ----
function setFont(px) { g.font = `bold ${px}px ${FONT}`; g.textAlign = 'center'; g.textBaseline = 'alphabetic'; }
export function textWidth(s, px) { setFont(px); return g.measureText(s).width; }
export function textCentered(s, cx, baseline, px, rgb, outline = 2, alpha = 1) {
  if (!s) return;
  setFont(px);
  g.globalCompositeOperation = 'source-over';
  g.lineJoin = 'round'; g.miterLimit = 2;
  if (outline > 0) { g.lineWidth = outline * 2; g.strokeStyle = `rgba(0,0,0,${0.9 * alpha})`; g.strokeText(s, cx, baseline); }
  g.fillStyle = css(rgb, alpha); g.fillText(s, cx, baseline);
}
function fittedText(s, cx, baseline, rgb, sizes, outlines, maxW) {
  if (!s) return;
  for (let i = 0; i < sizes.length; i++) {
    if (i === sizes.length - 1 || textWidth(s, sizes[i]) <= maxW) { textCentered(s, cx, baseline, sizes[i], rgb, outlines[i]); return; }
  }
}
export function bigNumber(value, cx, baseline, rgb) {
  addGlow(cx, baseline - 46, 125, rgb, 64);
  textCentered(String(value), cx, baseline, 104, rgb, 4);
}
export function title(s, cx, baseline, rgb) { fittedText(s, cx, baseline, rgb, [42, 36, 30, 22], [3, 3, 2, 2], W - 24); }
export function subtitle(s, cx, baseline, rgb) { fittedText(s, cx, baseline, rgb, [28, 24, 20, 16], [2, 2, 2, 1], W - 24); }
// Caption band at the bottom: a soft top edge, the band darkened, the text centred.
export function caption(text, rgb = WHITE) {
  const grad = g.createLinearGradient(0, 388, 0, 400);
  grad.addColorStop(0, 'rgba(0,0,0,0)'); grad.addColorStop(1, 'rgba(0,0,0,0.59)');
  g.globalCompositeOperation = 'source-over'; g.fillStyle = grad; g.fillRect(0, 388, W, 12);
  darkenRect(0, 400, W, H - 400, 150);
  if (text) fittedText(text, W / 2, 453, rgb, [36, 29, 22], [3, 2, 2], W - 24);
}
export function hostBadge(text, rgb) {
  if (!text) return;
  const w = textWidth(text, 15);
  darkenRect(4, 4, w + 18, 24, 60);
  textCentered(text, 13 + w / 2, 22, 15, rgb, 1);
}
function counterText(count, label) { return `${Number(count).toLocaleString('en-US')} ${label}`; }
export function counterBadge(count, label) {
  const line = counterText(count, label), w = textWidth(line, 19), right = W - 12;
  darkenRect(right - w - 10, 6, w + 18, 30, 80);
  textCentered(line, right - w / 2, 29, 19, 0xD2BEFF, 2);
}
export function counterBadgeAt(cx, baseline, count, label) {
  const line = counterText(count, label), w = textWidth(line, 19);
  let x = cx - w / 2;
  if (x + w > W - 8) x = W - 8 - w;
  if (x < 8) x = 8;
  darkenRect(x - 9, baseline - 23, w + 18, 30, 80);
  textCentered(line, x + w / 2, baseline, 19, 0xD2BEFF, 2);
}

// ---------------------------------------------------------------- procedural backgrounds
function vignette() {
  if (s_vignette) return s_vignette;
  const c = document.createElement('canvas'); c.width = W; c.height = H;
  const x = c.getContext('2d');
  x.fillStyle = 'rgb(10,6,26)'; x.fillRect(0, 0, W, H);
  const grad = x.createRadialGradient(W / 2, 220, 60, W / 2, 220, 560);
  grad.addColorStop(0, 'rgba(34,22,66,1)'); grad.addColorStop(1, 'rgba(6,3,16,1)');
  x.fillStyle = grad; x.fillRect(0, 0, W, H);
  s_vignette = c;
  return c;
}
export function orbBackground(tMs, rgb, energy, auraRgb = 0) {
  const ox = 400, oy = 220;
  g.globalCompositeOperation = 'source-over';
  g.drawImage(vignette(), 0, 0);
  const aura = auraRgb || rgb;
  const es = (energy >> 5) / 7;
  addGlow(ox, oy, 300 + 50 * es, aura, 42 + 34 * es);
  const e = energy / 255;
  const breath = wave(tMs, Math.round(4200 - 2000 * e));
  const R = 95 + 35 * e + 16 * breath;
  addGlow(ox, oy, R, rgb, 100 + 70 * e + 50 * breath);
  addGlow(ox, oy, R * 0.45, mixRgb(rgb, WHITE, 150), 60 + 60 * e + 70 * breath);
  const motes = 10 + Math.floor(10 * e);
  const moteCol = mixRgb(rgb, WHITE, 120);
  g.globalCompositeOperation = 'lighter';
  for (let i = 0; i < motes; i++) {
    const hsh = hash32(Math.imul(0x9E3779B9, i + 1) >>> 0);
    const speed = 9 + hsh % 15 + 14 * e;
    const start = (hsh >>> 5) % 560;
    const travel = ((tMs * speed + start * 1000) % 560000) / 1000;
    const y = 500 - travel;
    const amp = 8 + ((hsh >>> 9) % 24);
    const swayMs = 2600 + ((hsh >>> 14) % 3200);
    const x = (hsh % W) + amp * Math.sin(TAU * ((tMs % swayMs) / swayMs) + i);
    const tw = wave(tMs, 900 + ((hsh >>> 20) % 900), i);
    const lvl = (60 + 90 * tw) * (0.7 + 0.3 * e);
    addGlow(x, y, 5 + ((hsh >>> 24) % 5), moteCol, lvl);
    g.globalCompositeOperation = 'lighter';
    g.fillStyle = css(WHITE, lvl / 255); g.fillRect(x, y, 1.5, 1.5);
  }
  g.globalCompositeOperation = 'source-over';
}
export function fallbackCue(titleText, tMs, rgb, energy, auraRgb = 0) {
  orbBackground(tMs, rgb, energy, auraRgb);
  if (titleText) title(titleText, W / 2, 236, mixRgb(rgb, WHITE, 200));
}

// ---------------------------------------------------------------- overlays
export function glowPulse(cx, cy, r, rgb, tMs, periodMs = 1600) {
  const w = wave(tMs, periodMs || 1600);
  addGlow(cx, cy, r * (0.88 + 0.12 * w), rgb, 70 + 150 * w);
}
export function ring(cx, cy, r, thick, rgb, level) {
  if (r <= 0 || level <= 0) return;
  g.globalCompositeOperation = 'lighter';
  g.strokeStyle = css(rgb, clamp01(level / 255)); g.lineWidth = thick;
  g.beginPath(); g.arc(cx, cy, r, 0, TAU); g.stroke();
  g.globalCompositeOperation = 'source-over';
}
export function burstRing(cx, cy, rgb, sinceMs, durMs = 700) {
  if (durMs <= 0 || sinceMs >= durMs || sinceMs < 0) return;
  const p = sinceMs / durMs, q = 1 - p, ease = 1 - q * q * q;
  const r = 24 + 300 * ease, thick = 6 + 28 * q, lvl = 255 * Math.pow(q, 1.3);
  if (p < 0.3) addGlow(cx, cy, 90 + 140 * p, mixRgb(rgb, WHITE, 140), 200 * (1 - p / 0.3));
  ring(cx, cy, r, thick, rgb, lvl);
  if (p < 0.65) ring(cx, cy, r * 0.62, thick * 0.5, mixRgb(rgb, WHITE, 150), lvl * 0.6);
}

// ---- gems ----
function drawGem(gx, gy, w, h, rgb, lit) {
  const hw = w / 2;
  const tx = gx, ty = gy - h * 0.5, sy = gy - h * 0.18, ly = gy + h * 0.2, by = gy + h * 0.5, lx = gx - hw, rx = gx + hw, cy = gy - h * 0.04;
  const crownL = lit ? mixRgb(rgb, WHITE, 165) : scaleRgb(rgb, 46), crownR = lit ? mixRgb(rgb, WHITE, 80) : scaleRgb(rgb, 34);
  const faceL = lit ? rgb : scaleRgb(rgb, 26), faceR = lit ? scaleRgb(rgb, 150) : scaleRgb(rgb, 18), edge = lit ? mixRgb(rgb, WHITE, 210) : scaleRgb(rgb, 105);
  g.globalCompositeOperation = 'source-over';
  const tri = (a, b, c, col) => { g.fillStyle = css(col); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.lineTo(c[0], c[1]); g.closePath(); g.fill(); };
  tri([lx, sy], [lx, ly], [tx, cy], faceL); tri([lx, ly], [tx, by], [tx, cy], faceL);
  tri([rx, sy], [rx, ly], [tx, cy], faceR); tri([rx, ly], [tx, by], [tx, cy], faceR);
  tri([tx, ty], [lx, sy], [tx, cy], crownL); tri([tx, ty], [rx, sy], [tx, cy], crownR);
  g.strokeStyle = css(edge); g.lineWidth = 1.2; g.lineJoin = 'round';
  g.beginPath(); g.moveTo(tx, ty); g.lineTo(rx, sy); g.lineTo(rx, ly); g.lineTo(tx, by); g.lineTo(lx, ly); g.lineTo(lx, sy); g.closePath(); g.stroke();
  if (lit && w >= 18) {
    g.strokeStyle = css(mixRgb(rgb, WHITE, 120)); g.lineWidth = 1;
    g.beginPath(); g.moveTo(lx, sy); g.lineTo(tx, cy); g.lineTo(rx, sy); g.moveTo(tx, cy); g.lineTo(tx, by); g.stroke();
    g.fillStyle = 'rgba(255,255,255,0.9)'; g.fillRect(gx - w * 0.22 - 1, gy - h * 0.22 - 1, 2.5, 2.5);
  }
}
export function gem(cx, cy, w, h, rgb, lit) { if (w > 0 && h > 0) drawGem(cx, cy, w, h, rgb, lit); }
export function crystalMeter(cx, cy, filled, total, rgb, tMs, pulseNewest) {
  if (!total) return;
  total = Math.min(total, 16); filled = Math.min(filled, total);
  let spacing = 52; if (total * spacing > 720) spacing = 720 / total;
  const gw = spacing * 0.5, gh = gw * 1.55, mid = (total - 1) / 2, pulse = wave(tMs, 900);
  const gx = [], gy = [];
  for (let i = 0; i < total; i++) { const u = mid > 0 ? (i - mid) / mid : 0; gx.push(cx + (i - mid) * spacing); gy.push(cy - 12 * u * u); }
  for (let i = 0; i < filled; i++) {
    const newest = pulseNewest && i === filled - 1, k = newest ? 0.8 + 0.6 * pulse : 1;
    addGlow(gx[i], gy[i], gh * 0.95 * (newest ? 1.25 : 1), rgb, 70 * k);
  }
  for (let i = 0; i < total; i++) {
    const lit = i < filled, newest = lit && pulseNewest && i === filled - 1, s = newest ? 1 + 0.14 * pulse : 1;
    drawGem(gx[i], gy[i], gw * s, gh * s, rgb, lit);
  }
}
export function placeHint(tMs, rgb) { placeHintAt(W / 2, tMs, rgb); }
export function placeHintAt(cx, tMs, rgb) {
  const bob = 7 * Math.sin(TAU * ((tMs % 1600) / 1600)), gy = 392 + bob, w = wave(tMs, 1600);
  addGlow(cx, gy, 78, rgb, 80 + 60 * w);
  drawGem(cx, gy, 46, 70, rgb, true);
  const ph = (tMs % 1100) / 1100, slide = 7 * ph, lvl = Math.sin(Math.PI * ph);
  const col = scaleRgb(mixRgb(rgb, WHITE, 110), clamp8(110 + 145 * lvl));
  const top = 436 + slide;
  g.globalCompositeOperation = 'lighter'; g.fillStyle = css(col);
  g.fillRect(cx - 7, top, 14, 12);
  g.beginPath(); g.moveTo(cx - 21, top + 12); g.lineTo(cx + 21, top + 12); g.lineTo(cx, top + 32); g.closePath(); g.fill();
  g.globalCompositeOperation = 'source-over';
}

// ---- bowl shapes ----
function starPath(cx, cy, ro, ri) {
  g.beginPath();
  for (let i = 0; i < 10; i++) {
    const a = -Math.PI / 2 + i * Math.PI / 5, r = i % 2 ? ri : ro;
    const x = cx + r * Math.cos(a), y = cy + r * Math.sin(a);
    if (i) g.lineTo(x, y); else g.moveTo(x, y);
  }
  g.closePath();
}
function triPath(cx, cy, circR) {
  g.beginPath();
  for (let i = 0; i < 3; i++) { const a = -Math.PI / 2 + i * TAU / 3; const x = cx + circR * Math.cos(a), y = cy + circR * Math.sin(a); if (i) g.lineTo(x, y); else g.moveTo(x, y); }
  g.closePath();
}
export function bowlIcon(cx, cy, size, shape, rgb, tMs, level = 255) {
  if (size <= 0 || level <= 0) return;
  const w = wave(tMs, 1400), R = size * 0.5 * (1 + 0.05 * w);
  addGlow(cx, cy, R * 1.6, rgb, (70 + 70 * w) * level / 255);
  const white = css(scaleRgb(WHITE, level)), fillc = css(scaleRgb(rgb, level));
  const OUT = 3;
  g.globalCompositeOperation = 'source-over';
  switch (shape) {
    case Shape.Triangle: { const rt = R * 1.18; g.fillStyle = white; triPath(cx, cy + rt * 0.12, rt + 2 * OUT); g.fill(); g.fillStyle = fillc; triPath(cx, cy + rt * 0.12, rt); g.fill(); break; }
    case Shape.Star: { const rs = R * 1.08, ri = rs * 0.47; g.fillStyle = white; starPath(cx, cy, rs + OUT + 1, ri + OUT); g.fill(); g.fillStyle = fillc; starPath(cx, cy, rs, ri); g.fill(); break; }
    default: {
      g.fillStyle = white; g.beginPath(); g.arc(cx, cy, R + OUT, 0, TAU); g.fill();
      g.fillStyle = fillc; g.beginPath(); g.arc(cx, cy, R, 0, TAU); g.fill();
      g.fillStyle = css(scaleRgb(mixRgb(rgb, WHITE, 170), level)); g.beginPath(); g.arc(cx - R * 0.38, cy - R * 0.38, R * 0.16 + 1, 0, TAU); g.fill();
    }
  }
}

// ---------------------------------------------------------------- story overlays
export function moonMeter(cx, cy, r, phase, glowLevel, rgb, tMs) {
  if (r < 4) return;
  r = Math.min(r, 200); phase = Math.min(8, Math.max(0, phase || 0));
  const SILVER = rgb & 0xFFFFFF, DARK = 0x283050;
  const lit = 0.1 + 0.9 * (phase / 8), gl = glowLevel / 255, breath = wave(tMs, 4000);
  const halo = (24 + 96 * lit) * (0.5 + 0.5 * gl) * (0.92 + 0.08 * breath);
  addGlow(cx, cy, r * (1.75 + 0.45 * gl), mixRgb(SILVER, 0xBFD4FF, 90), halo);
  if (gl > 0.55) addGlow(cx, cy, r * 1.25, SILVER, (gl - 0.55) * 160);
  g.globalCompositeOperation = 'source-over';
  g.fillStyle = css(DARK, 0.55); g.beginPath(); g.arc(cx, cy, r, 0, TAU); g.fill();
  // the lit part: the disc clipped right of an elliptical terminator
  g.save(); g.beginPath(); g.arc(cx, cy, r, 0, TAU); g.clip();
  const xt = r * (1 - 2 * lit);           // terminator half-width (signed): lit to its right
  g.beginPath();
  g.moveTo(cx, cy - r); g.arc(cx, cy, r, -Math.PI / 2, Math.PI / 2, false);   // right limb
  g.ellipse(cx, cy, Math.abs(xt), r, 0, Math.PI / 2, -Math.PI / 2, xt > 0);     // terminator
  g.closePath();
  const limb = g.createRadialGradient(cx - r * 0.2, cy - r * 0.2, r * 0.2, cx, cy, r);
  limb.addColorStop(0, css(mixRgb(SILVER, WHITE, 30))); limb.addColorStop(1, css(scaleRgb(SILVER, 190)));
  g.fillStyle = limb; g.fill();
  const CR = [[0.3, -0.28, 0.17, 0.8], [-0.22, 0.3, 0.13, 0.84], [0.46, 0.32, 0.1, 0.82], [-0.36, -0.3, 0.09, 0.86], [0.06, 0.06, 0.07, 0.88]];
  for (const [u, v, rad, k] of CR) { g.fillStyle = `rgba(40,50,90,${1 - k})`; g.beginPath(); g.arc(cx + u * r, cy + v * r, rad * r, 0, TAU); g.fill(); }
  g.restore();
}
export function lanternMeter(cx, cy, r, phase, glowLevel, rgb, tMs) {
  if (r < 6) return;
  r = Math.min(r, 220); phase = Math.min(8, Math.max(0, phase || 0));
  rgb = (rgb & 0xFFFFFF) || 0xFFB347;
  const f = phase / 8, gl = glowLevel / 255;
  const ts = (tMs % 600000) * 0.001;
  const flick = 0.5 * Math.sin(TAU * 1.3 * ts) + 0.3 * Math.sin(TAU * 1.9 * ts + 1.7) + 0.2 * Math.sin(TAU * 0.7 * ts + 0.4);
  const fl = 1 + 0.06 * flick, fr = 1 + 0.025 * flick, lvl = 0.55 + 0.6 * gl, rad = (0.9 + 0.25 * gl) * fr;
  addGlow(cx, cy, r * (0.55 + 0.85 * f) * rad, rgb, (40 + 150 * f) * lvl * fl);
  const warm = mixRgb(rgb, 0xFFF1D6, 40 + 90 * f);
  addGlow(cx, cy, r * (0.28 + 0.32 * f) * rad, warm, (55 + 140 * f) * lvl * fl);
  if (f > 0.35) addGlow(cx, cy, r * (0.1 + 0.08 * f), mixRgb(rgb, WHITE, 200), (f - 0.35) / 0.65 * 170 * fl);
  if (gl > 0.55) addGlow(cx, cy, r * 1.25, mixRgb(rgb, WHITE, 90), (gl - 0.55) * 170);
  const n = f < 0.3 ? 0 : 1 + Math.floor((f - 0.3) * 6);
  const sparkCol = mixRgb(rgb, WHITE, 120);
  for (let i = 0; i < n && i < 5; i++) {
    const h = hash32(Math.imul(0xA511E9B3, i + 1) >>> 0);
    const period = 2600 + h % 1700;
    const u = ((tMs + (h >>> 8) % period) % period) / period;
    const rise = r * (1 + 0.35 * ((h >>> 16) % 100) / 100);
    const x = cx + (((h >>> 20) % 41) - 20) * r * (0.6 / 70) + 6 * Math.sin(TAU * 1.5 * u + i);
    const y = cy - r * 0.15 - u * rise;
    const b = Math.sin(Math.PI * u) * (110 + 100 * f);
    addGlow(x, y, 5 + (h >>> 28) % 3, sparkCol, b * 0.6);
    g.globalCompositeOperation = 'lighter'; g.fillStyle = css(0xFFFFC0, clamp01(b / 255)); g.fillRect(x, y, 1.5, 1.5); g.globalCompositeOperation = 'source-over';
  }
}
export function poof(cx, cy, sinceMs, rgb = WHITE, durMs = 500) {
  if (durMs <= 0 || sinceMs >= durMs || sinceMs < 0) return;
  rgb &= 0xFFFFFF;
  const p = sinceMs / durMs, q = 1 - p, grow = 1 - q * q, R = 22 + 50 * grow, a = clamp8(235 * Math.pow(q, 1.5));
  if (p < 0.35) addGlow(cx, cy, 50 + 90 * p, rgb, 210 * (1 - p / 0.35));
  const shade = mixRgb(rgb, 0xC8CCDA, 90);
  for (let i = 0; i < 6; i++) {
    const ang = i * (TAU / 6) + 0.5;
    softDisc(cx + R * 0.58 * Math.cos(ang), cy + R * 0.42 * Math.sin(ang), R * 0.4, 0.85, (i & 1) ? shade : rgb, a);
  }
  softDisc(cx, cy - R * 0.05, R * 0.55, 0.85, rgb, a);
}
export function smudgeCloud(cx, cy, r, tMs, level = 255, rgb = 0xB9A8D6) {
  if (r < 6 || level <= 0) return;
  const LAV = (rgb & 0xFFFFFF) || 0xB9A8D6, t = tMs % 600000;
  for (let i = 0; i < 7; i++) {
    const a = i * (TAU / 7) + 0.25 * Math.sin(t / 1700 + i);
    const dist = r * (0.55 + 0.05 * Math.sin(t / 1300 + i * 2.1)), pr = r * (0.42 + 0.05 * Math.sin(t / 900 + i * 1.7));
    softDisc(cx + dist * Math.cos(a), cy + dist * 0.62 * Math.sin(a), pr, 0.85, mixRgb(LAV, WHITE, 20 + (i % 3) * 18), level);
  }
  softDisc(cx, cy, r * 0.66, 0.8, LAV, level);
  softDisc(cx - r * 0.22, cy - r * 0.2, r * 0.3, 0.8, mixRgb(LAV, WHITE, 120), level / 2);
  softDisc(cx + r * 0.1, cy + r * 0.12, r * 0.28, 0.8, scaleRgb(LAV, 190), level / 3);
}
// A soft column of light from (x0,y0) (base) to (x1,y1) (tip), halfWidth at the base.
export function beam(x0, y0, x1, y1, halfWidth, rgb, level, tMs) {
  if (halfWidth <= 0 || level <= 0) return;
  halfWidth = Math.min(halfWidth, 200);
  const lv = clamp01(level / 255);
  g.save();
  g.globalCompositeOperation = 'lighter';
  const hwTip = halfWidth * 0.45;
  g.beginPath(); g.moveTo(x0 - halfWidth, y0); g.lineTo(x1 - hwTip, y1); g.lineTo(x1 + hwTip, y1); g.lineTo(x0 + halfWidth, y0); g.closePath();
  g.clip();
  // cross-section: tinted body + whiter core (a horizontal gradient about the axis, drawn in skewed space)
  const core = mixRgb(rgb, WHITE, 170);
  const steps = 10;
  const yTop = Math.min(y0, y1), yBot = Math.max(y0, y1);
  const shimmer = (tMs % 100000) * 0.25;
  for (let s = 0; s < steps; s++) {
    const ya = yTop + (yBot - yTop) * s / steps, yb = yTop + (yBot - yTop) * (s + 1) / steps;
    const v = (y0 > y1) ? (yb - yTop) / (yBot - yTop || 1) : 1 - (ya - yTop) / (yBot - yTop || 1);   // 0 tip .. 1 base
    const xc = x1 + (x0 - x1) * v, hw = halfWidth * (0.45 + 0.55 * v);
    let sh = 0.86 + 0.14 * Math.sin(0.06 * (ya + shimmer));
    if (v < 0.06) sh *= v / 0.06; if (v > 0.92) sh *= (1 - v) / 0.08;
    const grad = g.createLinearGradient(xc - hw, 0, xc + hw, 0);
    grad.addColorStop(0, css(rgb, 0)); grad.addColorStop(0.3, css(rgb, 0.45 * lv * sh)); grad.addColorStop(0.5, css(core, 0.75 * lv * sh));
    grad.addColorStop(0.7, css(rgb, 0.45 * lv * sh)); grad.addColorStop(1, css(rgb, 0));
    g.fillStyle = grad; g.fillRect(xc - hw - 1, ya - 0.5, hw * 2 + 2, yb - ya + 1);
  }
  g.restore();
  addGlow(x1, y1, halfWidth * 1.6, mixRgb(rgb, WHITE, 120), 150 * lv);
}
export function checkMark(cx, cy, size, rgb) {
  if (size <= 4) return;
  const s = size;
  const ax = cx - 0.5 * s, ay = cy + 0.02 * s, bx = cx - 0.15 * s, by = cy + 0.36 * s, ex = cx + 0.52 * s, ey = cy - 0.4 * s, w = s * 0.17 + 2;
  g.globalCompositeOperation = 'source-over'; g.lineCap = 'round'; g.lineJoin = 'round';
  for (const [col, lw] of [['rgba(0,0,0,0.9)', w + 6], [css(rgb), w]]) {
    g.strokeStyle = col; g.lineWidth = lw; g.beginPath(); g.moveTo(ax, ay); g.lineTo(bx, by); g.lineTo(ex, ey); g.stroke();
  }
}

// ---------------------------------------------------------------- sparkles (one shared particle field)
const MAX_SPARKS = 96;
const s_sparks = [];
let s_ambientAcc = 0, s_ambientLastMs = 0;
function allocSpark() {
  if (s_sparks.length < MAX_SPARKS) { const s = {}; s_sparks.push(s); return s; }
  let best = s_sparks[0];
  for (const s of s_sparks) { if (!s.active) return s; if (s.life - s.age < best.life - best.age) best = s; }
  return best;
}
function drawSparkle(x, y, size, rgb, level) {
  if (level <= 0) return;
  if (x < -8 || x > W + 8 || y < -8 || y > H + 8) return;
  const a = clamp01(level / 255);
  if (size >= 2) addGlow(x, y, size * 3, rgb, level / 3);
  g.globalCompositeOperation = 'lighter';
  g.strokeStyle = css(rgb, a); g.lineWidth = 1.2; g.lineCap = 'round';
  g.beginPath(); g.moveTo(x - size, y); g.lineTo(x + size, y); g.moveTo(x, y - size); g.lineTo(x, y + size); g.stroke();
  g.fillStyle = `rgba(255,255,255,${a})`; g.fillRect(x - 0.75, y - 0.75, 1.5, 1.5);
  g.globalCompositeOperation = 'source-over';
}
export function sparklesClear() { for (const s of s_sparks) s.active = false; s_ambientAcc = 0; s_ambientLastMs = 0; }
export function sparklesBurst(cx, cy, n, rgb) {
  for (let i = 0; i < n; i++) {
    const s = allocSpark();
    const a = rndf() * TAU, sp = 0.06 + rndf() * 0.3;
    s.x = cx + 4 * Math.cos(a); s.y = cy + 4 * Math.sin(a); s.vx = sp * Math.cos(a); s.vy = sp * Math.sin(a) - 0.04;
    s.age = 0; s.life = 450 + rnd() % 700; s.size = 1 + rnd() % 3; s.ambient = false; s.phase = rnd() & 255;
    s.rgb = (rnd() % 3 === 0) ? mixRgb(rgb, WHITE, 210) : mixRgb(rgb, WHITE, 50 + rnd() % 90);
    s.active = true;
  }
}
export function sparklesAmbient(rgb, perSecond, nowMs) {
  let dt = s_ambientLastMs === 0 ? 33 : nowMs - s_ambientLastMs;
  if (dt > 250) dt = 250;
  s_ambientLastMs = nowMs;
  if (!perSecond) return;
  s_ambientAcc = Math.min(8, s_ambientAcc + perSecond * dt / 1000);
  while (s_ambientAcc >= 1) {
    s_ambientAcc -= 1;
    const s = allocSpark();
    s.x = 20 + rnd() % (W - 40); s.y = 20 + rnd() % 360; s.vx = (rndf() - 0.5) * 0.012; s.vy = -(0.006 + rndf() * 0.018);
    s.age = 0; s.life = 1400 + rnd() % 1600; s.size = 1 + rnd() % 3; s.ambient = true; s.phase = rnd() & 255;
    s.rgb = mixRgb(rgb, WHITE, 80 + rnd() % 120); s.active = true;
  }
}
export function sparklesStep(dtMs) {
  if (dtMs > 100) dtMs = 100;
  const dt = dtMs;
  for (const s of s_sparks) {
    if (!s.active) continue;
    s.age += dtMs;
    if (s.age >= s.life) { s.active = false; continue; }
    if (!s.ambient) { let drag = 1 - 0.0022 * dt; if (drag < 0) drag = 0; s.vx *= drag; s.vy = s.vy * drag + 0.00012 * dt; }
    s.x += s.vx * dt; s.y += s.vy * dt;
    if (s.x < -12 || s.x > W + 12 || s.y < -12 || s.y > H + 12) { s.active = false; continue; }
    const f = s.age / s.life;
    let b = f < 0.15 ? f / 0.15 : (1 - f) / 0.85;
    if (s.ambient) b *= 0.55 + 0.45 * Math.sin(s.age * 0.012 + s.phase * (TAU / 256));
    drawSparkle(s.x, s.y, s.size, s.rgb, 255 * b);
  }
}
