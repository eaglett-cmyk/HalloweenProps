// crystal.js — one crystal's page (/c/<id>): reads /api/crystal/<id> and fills crystal.html.
// Names match the storybook (pages 6 and 7) and the station's tier / rank numbers.
(function () {
  'use strict';
  const RANKS = ['', 'Sparkfinder', 'Apprentice', 'Glowcaster', 'Hoardguard', 'Worldwalker', 'Dragon Rider', 'Keeper of the Hoard'];
  const TIERS = [
    { name: 'Goblin Trick', bowl: 'bowl-green', note: 'Snuffwick got him again. Candy anyway!' },
    { name: 'Hoard Coin', bowl: 'bowl-silver', note: 'From the silver coin bowl.' },
    { name: 'Golden Star', bowl: 'bowl-gold', note: 'A rare one, from the gold star bowl!' },
    { name: 'Dragon Gem', bowl: 'bowl-red', note: 'Legendary! The rarest treasure of all.' },
  ];
  const F_FIRST = 0x01;

  const m = location.pathname.match(/\/c\/([0-9a-fA-F]{8})\/?$/);
  const id = m ? m[1].toLowerCase() : '';
  const $ = s => document.getElementById(s);
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };

  $('cp-id').textContent = id ? id.toUpperCase() : '?';
  const summary = $('cp-summary');

  function say(lines) {
    summary.replaceChildren(...lines.map(([cls, t]) => el('p', cls, t)));
  }

  if (!id) { say([['cp-big', 'This crystal link looks smudged.'], ['', 'Tap the crystal with your phone again.']]); return; }

  fetch(`/api/crystal/${id}`, { cache: 'no-cache' })
    .then(r => { if (!r.ok) throw new Error(r.status); return r.json(); })
    .then(show)
    .catch(() => say([['cp-big', 'The dragon is napping.'], ['', 'Its memory could not be reached just now. Try again in a little while.']]));

  // ---- nickname: shown as the title; anyone with the crystal's link can set or change it
  const title = $('cp-title'), nameBtn = $('cp-name-btn'), form = $('cp-name-form'), input = $('cp-name-in'), msg = $('cp-name-msg');
  const HELP = msg.textContent;
  let nickname = null;
  function showName() {
    title.textContent = nickname || 'Your Crystal';
    document.title = `${nickname || 'Your Crystal'} — Dragonglow`;
    nameBtn.textContent = nickname ? 'Rename it' : 'Give it a name';
    nameBtn.hidden = false;
    form.hidden = true;
  }
  nameBtn.addEventListener('click', () => {
    nameBtn.hidden = true; form.hidden = false; msg.textContent = HELP; msg.className = '';
    input.value = nickname || ''; input.focus();
  });
  $('cp-name-cancel').addEventListener('click', showName);
  form.addEventListener('submit', e => {
    e.preventDefault();
    const btn = form.querySelector('button[type=submit]');
    btn.disabled = true;
    fetch(`/api/crystal/${id}/nickname`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ nickname: input.value }) })
      .then(r => r.json().then(j => ({ ok: r.ok, j })))
      .then(({ ok, j }) => {
        if (!ok) throw new Error(j.error || 'Could not save that name.');
        nickname = j.nickname;
        showName();
      })
      .catch(err => { msg.textContent = err.message || 'Could not save that name.'; msg.className = 'cp-name-err'; })
      .finally(() => { btn.disabled = false; });
  });

  function show(d) {
    nickname = d.nickname || null;
    showName();
    if (!d.known) {
      say([
        ['cp-big', 'This crystal is waiting to glow.'],
        ['', 'Butterscotch hasn\'t written its story here yet. If it woke him this Halloween, its page fills in a day or two after the night. Keep it safe and bring it back next year!'],
      ]);
      return;
    }
    const rank = RANKS[Math.min(Math.max(d.rank | 0, 1), RANKS.length - 1)];
    const n = d.visits;
    const first = d.years[0].year;
    say([
      ['cp-rank-label', 'Rank'],
      ['cp-big cp-rank', rank],
      ['', `It has woken Butterscotch on ${n} Halloween${n === 1 ? '' : 's'}, since ${first}.`],
      ['cp-next', d.rank >= 7 ? 'The highest honour. Only Keepers hear the secret.' : 'Bring it back next Halloween to climb a rank.'],
    ]);
    const list = $('cp-yearlist');
    list.replaceChildren(...d.years.slice().reverse().map(y => {
      const t = TIERS[y.tier] || TIERS[1];
      const li = el('li', 'cp-year');
      li.append(el('span', 'cp-y', String(y.year)));
      const body = el('div', 'cp-ybody');
      const res = el('p', 'cp-res');
      res.append(el('span', `bowl ${t.bowl}`), el('strong', '', t.name));
      body.append(res, el('p', 'cp-glow', `Glow ${y.number}${y.flags & F_FIRST ? ' · the night it first awoke' : ''}`), el('p', 'cp-tnote', t.note));
      li.append(body);
      return li;
    }));
    $('cp-years').hidden = false;
  }
})();
