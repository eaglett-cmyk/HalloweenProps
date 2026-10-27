// hunt.js — the Goblin Hunt (a /play/ game): Snuffwick hides on five storybook pages.
// Does nothing unless the hunt is on: the games page links here with ?hunt=1, which this
// browser then remembers until all five are found. Each find sends one spark to the
// Hoard Beacon (5 in all), once a year per find. A page that hides him and has been open
// for 8 s without a find gets a gentle hint glow. Uses /play/play.js (loaded on demand).
(function () {
  'use strict';
  const on = /[?&]hunt=1\b/.test(location.search);
  let saved = null;
  try { saved = localStorage.getItem('dg.hunt'); } catch { /* private mode */ }
  if (!on && saved !== 'on') return;
  try { localStorage.setItem('dg.hunt', 'on'); } catch { /* fine */ }

  // [page (1-8), sprite, where]  (positions in % of the page art; he peeks from the edges and corners)
  const SPOTS = [
    ['p1', 1, 'snuffwick_shy', 'right:5%;top:50%;width:13%;transform:rotate(-8deg)'],
    ['p2', 2, 'snuffwick_base', 'left:7%;top:47%;width:12%;transform:rotate(10deg) scaleX(-1)'],
    ['p4', 4, 'snuffwick_shy', 'right:7%;top:60%;width:11%'],
    ['p5', 5, 'snuffwick_sock', 'left:58%;top:62%;width:12%;transform:rotate(6deg)'],
    ['p7', 7, 'snuffwick_sneeze', 'right:4%;top:6%;width:12%;transform:rotate(-14deg) scaleX(-1)'],
  ];

  const css = document.createElement('style');
  css.textContent = `
    .hunt-gob { position:absolute; z-index:4; height:auto; cursor:pointer; filter:drop-shadow(0 4px 6px rgba(0,0,0,.45)); transition:filter .3s, opacity .6s; -webkit-tap-highlight-color:transparent; }
    .hunt-gob:hover { filter:drop-shadow(0 0 10px rgba(141,191,106,.9)); }
    .hunt-gob.is-found { opacity:.45 !important; cursor:default; filter:grayscale(.3) drop-shadow(0 4px 6px rgba(0,0,0,.45)); }
    .hunt-gob.is-hop { animation:hunt-hop .5s ease 2; }
    .hunt-gob.is-hint:not(.is-found) { animation:hunt-hint 1.6s ease-in-out infinite; }
    @keyframes hunt-hint { 50% { filter:drop-shadow(0 0 14px rgba(255,200,61,.95)) drop-shadow(0 0 4px #fff); } }
    @keyframes hunt-hop { 50% { translate:0 -18%; } }
    .hunt-bar { position:fixed; left:50%; bottom:12px; transform:translateX(-50%); z-index:50;
      display:flex; align-items:center; gap:.7rem; padding:.45rem .6rem .45rem 1rem; max-width:calc(100% - 24px);
      background:rgba(15,10,31,.92); color:#f6ead2; border:1px solid rgba(141,191,106,.6); border-radius:999px;
      font:600 .95rem Fredoka, sans-serif; box-shadow:0 8px 24px rgba(0,0,0,.5); }
    .hunt-bar b { color:#8dbf6a; }
    .hunt-bar a, .hunt-bar button { color:#f6ead2; background:rgba(255,255,255,.1); border:0; border-radius:999px; padding:.3rem .8rem; font:inherit; text-decoration:none; cursor:pointer; white-space:nowrap; }
    .hunt-bar.is-done { border-color:#ffc83d; }
    .hunt-bar.is-done b { color:#ffc83d; }
    @media (prefers-reduced-motion: reduce) { .hunt-gob.is-hop, .hunt-gob.is-hint { animation:none; } .hunt-gob.is-hint:not(.is-found) { filter:drop-shadow(0 0 12px rgba(255,200,61,.95)); } }`;
  document.head.append(css);

  const found = new Set();
  const year = new Intl.DateTimeFormat('en-US', { timeZone: 'America/Chicago', year: 'numeric' }).format(new Date());
  const KEY = `dg.${year}.hunt.found`;
  try { (localStorage.getItem(KEY) || '').split(',').filter(Boolean).forEach(k => found.add(k)); } catch { /* fresh hunt */ }

  const pages = document.querySelectorAll('#story .book-stage > .page');
  for (const [id, n, sprite, style] of SPOTS) {
    const art = pages[n - 1] && pages[n - 1].querySelector('.page-art');
    if (!art) continue;
    const im = document.createElement('img');
    im.className = 'hunt-gob' + (found.has(id) ? ' is-found' : '');
    im.src = `img/${sprite}.webp`; im.alt = 'Snuffwick, hiding'; im.width = 640; im.height = 678;
    im.dataset.gob = id;
    im.style.cssText = style;
    art.append(im);
  }

  const bar = document.createElement('div');
  bar.className = 'hunt-bar';
  bar.setAttribute('role', 'status');
  document.body.append(bar);
  function renderBar(msg) {
    const n = found.size;
    bar.classList.toggle('is-done', n >= SPOTS.length);
    bar.innerHTML = `<span>${msg || (n >= SPOTS.length ? '<b>You found him all 5 times!</b>' : `Goblin Hunt: <b>${n} of ${SPOTS.length}</b> found`)}</span>` +
      `<a href="/play/">Games</a>`;
  }
  renderBar(found.size ? null : 'Goblin Hunt: Snuffwick is hiding on <b>5 pages</b>. Turn the pages and tap him!');

  let dg = null, barTimer = 0;
  function play() {   // play.js gives the chime and the beacon
    if (dg) return dg;
    dg = new Promise(res => {
      if (window.DG) { res(window.DG); return; }
      const s = document.createElement('script');
      s.src = '/play/play.js?v=3'; s.onload = () => res(window.DG); s.onerror = () => res(null);
      document.head.append(s);
    });
    return dg;
  }
  play();

  // one listener for every copy (the page turn clones the art while it animates)
  document.addEventListener('click', async ev => {
    const im = ev.target.closest && ev.target.closest('.hunt-gob');
    if (!im) return;
    ev.preventDefault(); ev.stopPropagation();
    const id = im.dataset.gob;
    if (found.has(id)) return;
    found.add(id);
    try { localStorage.setItem(KEY, [...found].join(',')); } catch { /* still counts on this page */ }
    document.querySelectorAll(`.hunt-gob[data-gob="${id}"]`).forEach(e => { e.classList.add('is-hop'); setTimeout(() => e.classList.add('is-found'), 1000); });
    const D = await play();
    if (D) [0, 5, 9].forEach((p, i) => setTimeout(() => D.chime(p + found.size, 0.07), i * 90));
    clearTimeout(barTimer);
    const v = D ? await D.addSparks('hunt', id) : { failed: true };   // one spark a find, once a year (the server keeps count)
    const plus = v.why === 'ok' ? ` +${v.added} spark${v.added === 1 ? '' : 's'}!` : '';
    if (found.size < SPOTS.length) { renderBar(`Found him! <b>${found.size} of ${SPOTS.length}</b>${plus}`); barTimer = setTimeout(() => renderBar(), 2600); return; }
    try { localStorage.removeItem('dg.hunt'); } catch { /* fine */ }   // the hunt is over: next visit is a plain book
    renderBar(`<b>You found him all 5 times!</b>${plus}` + (v.failed ? '' : ` The beacon is ${v.lit ? 'LIT!' : `${Math.floor(v.sparks / v.goal * 100)}% lit`}.`));
  }, true);

  // a hint: a page that hides him, open for 8 s with no find, makes him glow softly
  let hintPage = null, hintSince = 0;
  setInterval(() => {
    const cur = document.querySelector('#story .book-stage > .page.is-current');
    if (cur !== hintPage) { hintPage = cur; hintSince = Date.now(); return; }
    if (cur && Date.now() - hintSince > 8000) cur.querySelectorAll('.hunt-gob:not(.is-found)').forEach(e => e.classList.add('is-hint'));
  }, 1000);
})();
