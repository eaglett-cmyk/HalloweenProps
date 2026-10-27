// main.js - boots the station: canvas + input, the card's config / words / asset index, the
// counters, then (after the host's first tap, which Safari needs for audio) the plinth link and
// the show. Keyboard for testing on a PC: m = host menu, f = fresh crystal, r = returning,
// p = already played, x = foreign, u = unreadable, b = plinth button, l = lift the sim crystal.
import * as S from './settings.js';
import { cfg } from './settings.js';
import { media } from './media.js';
import { audio } from './audio.js';
import { link } from './link.js';
import { input } from './input.js';
import { vlog } from './vlog.js';
import { show } from './show.js';
import * as fx from './fx.js';
import { service, sim } from './service.js';

const BUILD = '2026-10-26d';
// page errors go to the station's events.log too (the iPad has no console in Guided Access)
window.addEventListener('error', e => { try { vlog.event(`page error: ${e.message} @ ${(e.filename || '').split('/').pop()}:${e.lineno}`); } catch {} });
window.addEventListener('unhandledrejection', e => { try { vlog.event(`page error (promise): ${e.reason && e.reason.message || e.reason}`); } catch {} });
const canvas = document.getElementById('show');
const overlay = document.getElementById('start');
const startBtn = document.getElementById('start-btn');
const statusEl = document.getElementById('start-status');
const addrEl = document.getElementById('plinth-addr');
const hud = document.getElementById('hud');
let s_wakeLock = null;

// cfg.fit 'fill' (default): scale the 800x480 frame up until it fills the screen and crop
// what spills over (an iPad's 4:3-ish screen loses ~7% of each side, no black bars), but
// never crop more than 14% of either axis. 'fit': the whole frame, letterboxed.
// The visible part is handed to fx (setSafeX) so badges and captions stay on screen.
function fitCanvas() {
  const vw = window.innerWidth, vh = window.innerHeight;
  const contain = Math.min(vw / 800, vh / 480), cover = Math.max(vw / 800, vh / 480);
  const scale = cfg.fit === 'fit' ? contain : Math.min(cover, contain / 0.86);
  canvas.style.width = `${Math.floor(800 * scale)}px`;
  canvas.style.height = `${Math.floor(480 * scale)}px`;
  const cropX = Math.max(0, (800 * scale - vw) / 2 / scale);
  fx.setSafeX(Math.ceil(cropX), 800 - Math.ceil(cropX));
}
window.addEventListener('resize', fitCanvas);
fitCanvas();

async function fetchText(path) {
  try { const r = await fetch(path, { cache: 'no-store' }); return r.ok ? await r.text() : ''; } catch { return ''; }
}
function setStatus(s) { statusEl.textContent = s; }

async function keepAwake() {
  try { if ('wakeLock' in navigator && !s_wakeLock) { s_wakeLock = await navigator.wakeLock.request('screen'); s_wakeLock.addEventListener('release', () => { s_wakeLock = null; }); } } catch {}
}
document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'visible') { keepAwake(); audio.resume(); } });

async function fullscreen() {
  // iOS Safari's fullscreen mode puts an "X" exit button over the top-left corner, right where
  // the host menu long-press lives, and taps there leave fullscreen. On an iPad the real full
  // screen is "Add to Home Screen" (standalone), so never ask for it there.
  if (/iPad|iPhone|iPod/.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1)) return;
  const e = document.documentElement;
  try { if (e.requestFullscreen) await e.requestFullscreen({ navigationUI: 'hide' }); else if (e.webkitRequestFullscreen) e.webkitRequestFullscreen(); } catch {}
}

// Background: pull the whole card into memory, the attract loop first (clips, then voice, music).
function prefetchAll() {
  const clips = media.listDir('clips').map(f => `/oracle/clips/${f}`);
  const first = ['attract', 'absorb', 'greet', 'awaken_1', 'awaken_2', 'awaken_3', 'awaken', 'charge', 'reveal_t1', 'reveal_t0_1', 'reveal_t2', 'reveal_t3', 'goodbye', 'nocrystal', 'replay', 'sleepy']
    .map(id => media.findVisual(id).path).filter(Boolean);
  const rest = clips.filter(p => !first.includes(p));
  const voice = media.listDir('voice').map(f => `/oracle/voice/${f}`);
  media.prefetch([...first, ...rest]).then(() => audio.preload(voice)).then(() => { hud.textContent = ''; });
}

async function boot() {
  setStatus('Loading the card...');
  await media.load();
  const [configTxt, captionsTxt] = await Promise.all([
    media.hasConfig() ? fetchText('/oracle/config.ini') : '',
    media.hasCaptions() ? fetchText('/oracle/captions.ini') : '',
  ]);
  S.loadConfig(configTxt);
  fitCanvas();   // config.ini may set [station] fit
  S.loadWords(captionsTxt);
  await S.loadCounters(cfg.eventYear);
  vlog.begin(cfg.eventYear);
  input.attach(canvas);
  service.mount();
  addrEl.value = cfg.plinth;
  const lines = [
    media.sdOk() ? `card: ${media.assetCount()} assets` : 'card: NOT FOUND (run tools/build_assets.py)',
    `words: ${S.text('title', '?')} / ${S.meterName()} ${S.counters.moonPoints}/${cfg.moonGoal}`,
    `plinth: ${cfg.plinth}`,
  ];
  // a boot screen on the canvas behind the overlay
  const g = canvas.getContext('2d');
  g.fillStyle = '#06030f'; g.fillRect(0, 0, 800, 480);
  g.fillStyle = '#e9d5ff'; g.font = 'bold 42px "Arial Black", Impact, Arial, sans-serif'; g.textAlign = 'center'; g.fillText(S.text('title', 'The Crystal Oracle'), 400, 120);
  g.fillStyle = '#fff'; g.font = 'bold 20px Arial, sans-serif'; lines.forEach((l, i) => g.fillText(l, 400, 196 + i * 34));
  setStatus((media.sdOk() ? 'Ready. Tap to begin.' : 'No assets found - the show will run with built-in visuals and chimes.') + ` (build ${BUILD})`);
  startBtn.disabled = false;
}

async function start() {
  startBtn.disabled = true;
  setStatus('Starting...');
  const addr = addrEl.value.trim();
  if (addr && addr !== cfg.plinth) S.setConfig('station.plinth', addr);
  setStatus('Starting audio...');
  // the music bed starts inside this tap (see audio.init): the full-beacon bed if the beacon is already lit
  const bed = S.counters.moonFull && media.exists('/oracle/music/idle_full.wav') ? '/oracle/music/idle_full.wav' : '/oracle/music/idle.wav';
  await audio.init(bed);
  audio.setVolume(cfg.volume);
  setStatus(`Sound: ${audio.engine()} (${audio.state()}). Connecting to the plinth...`);
  vlog.event(`sound at start: ${audio.engine()}, context ${audio.state()}, rate ${audio.sampleRate ? audio.sampleRate() : '?'}, volume ${cfg.volume}`);
  await fullscreen();
  keepAwake();
  link.begin(cfg.plinth, cfg.group);
  // the attract loop + the sfx + the music beds first, so the first seconds are complete
  const core = [media.findVisual('attract').path, media.findVisual('absorb').path].filter(Boolean);
  setStatus('Loading the attract loop...');
  await media.prefetch(core, p => setStatus(`Loaded ${p.split('/').pop()}`));
  await audio.preload(media.listDir('music').map(f => `/oracle/music/${f}`).concat(media.listDir('sfx').map(f => `/oracle/sfx/${f}`)));
  overlay.classList.add('hidden');
  show.begin(canvas);
  hud.textContent = `loading the rest of the card in the background... (sound: ${audio.engine()}, build ${BUILD})`;
  setTimeout(() => vlog.event(`sound 5 s in: ${audio.engine()}, context ${audio.state()}, unmute ${audio.unmuteState()}; ${audio.mediaStatus()}`), 5000);
  prefetchAll();
  show.start();
}
startBtn.addEventListener('click', () => start().catch(e => { console.error(e); setStatus(`Start failed: ${e.message}`); startBtn.disabled = false; }));

// keyboard shortcuts for testing on a PC
window.addEventListener('keydown', ev => {
  if (overlay && !overlay.classList.contains('hidden')) return;
  if (ev.target && (ev.target.tagName === 'INPUT' || ev.target.tagName === 'SELECT')) return;
  const inMenu = service.isOpen();
  const place = img => { if (inMenu) service.queueSim(img); else link.simulatePlaced(img); };
  switch (ev.key) {
    case 'm': if (!inMenu) show.requestService(); break;
    case 'f': place(sim.blankImage()); break;
    case 'r': place(sim.oursImage({ visits: 2, colour: 1 + Math.floor(Math.random() * Math.max(1, S.colorCount())), lastTier: 1 })); break;
    case 'p': place(sim.oursImage({ visits: 1, playedThisYear: true })); break;
    case 'x': place(sim.foreignImage()); break;
    case 'u': place(sim.badImage()); break;
    case 'b': link.simulateButton(); break;
    case 'l': link.simulateLifted(true); break;
    default: return;
  }
});

window.station = { show, link, media, audio, settings: S, sim, service, vlog };
boot().catch(e => { console.error(e); setStatus(`Boot failed: ${e.message}`); startBtn.disabled = false; });
