// riddle.js — Butterscotch's riddle book: 31 riddles, open in any order. (2026: nobody visits
// before Halloween night, so there is no October countdown.) The page opens the first riddle this
// browser hasn't solved; ?n=N opens riddle N, and each candle links to its riddle. A right answer
// lights that riddle's candle (this browser) and sends a spark to the Hoard Beacon (the server pays
// the riddle game once a day).
(function () {
  'use strict';
  // [question, answer, wrong, wrong, Butterscotch's line when it's solved]
  const RIDDLES = [
    ['I float like a balloon, I sleep on candy, and I wake up one night a year. Who am I?', 'Butterscotch!', 'A bat', 'A pumpkin', 'That\'s me! Hello, traveler!'],
    ['What has to be broken before you can use it?', 'An egg', 'A cookie', 'A crayon', 'Just like my crystal egg. Crack!'],
    ['I have a face and two hands, but no arms or legs. What am I?', 'A clock', 'A ghost', 'A teddy bear', 'Tick, tock! Right on time.'],
    ['I\'m tall when I\'m young and short when I\'m old. What am I?', 'A candle', 'A tree', 'A grown-up', 'Glow, little candle, glow!'],
    ['What gets wetter the more it dries?', 'A towel', 'A fish', 'A raincloud', 'A dry dragon is a happy dragon.'],
    ['Travelers follow my warm light to find the cave. What am I?', 'The Hoard Beacon', 'The moon', 'A flashlight', 'And you are helping to light it!'],
    ['What has a head and a tail, but no body?', 'A coin', 'A snake', 'A comet', 'Like the coins in the silver coin bowl!'],
    ['What has many teeth but never bites?', 'A comb', 'A shark', 'A zipper monster', 'No biting in my cave. Only brushing!'],
    ['I\'m tiny and green, I love the dark, and I snuff out lights. Who am I?', 'Snuffwick', 'A frog', 'A pickle', 'That sneaky little goblin!'],
    ['What building has the most stories?', 'A library', 'A castle', 'A skyscraper', 'Stories are my favourite treasure.'],
    ['What can you catch but never throw?', 'A cold', 'A ball', 'A firefly', 'Achoo! Bless you!'],
    ['What has keys but can\'t open any doors?', 'A piano', 'A treasure chest', 'A janitor', 'Plink, plonk! Play me a song.'],
    ['Where do ghosts buy their snacks?', 'The ghost-ery store', 'The boo-tique', 'The candy cave', 'Ha! Ghost-ery! I like that one.'],
    ['What kind of room has no doors and no windows?', 'A mushroom', 'A bedroom', 'A cave', 'Mushrooms grow in my cave, you know.'],
    ['Set a crystal on me and the dragon wakes up. What am I?', 'The Wakestone', 'A pillow', 'A doorbell', 'Right! One crystal at a time, please.'],
    ['What is a ghost\'s favourite dessert?', 'I-scream', 'Boo-berry pie', 'Ghost toast', 'I scream, you scream, we all scream!'],
    ['What has one eye but can\'t see a thing?', 'A needle', 'A pirate', 'A potato', 'Sharp thinking, traveler!'],
    ['What goes up but never comes back down?', 'Your age', 'A balloon', 'A kite', 'Every year you grow, and so does your rank!'],
    ['What room does a ghost never go in?', 'The living room', 'The bathroom', 'The kitchen', 'Spooky, but true!'],
    ['What can travel all around the world while staying in one corner?', 'A stamp', 'A spider', 'A snail', 'Stamps travel farther than dragons!'],
    ['I\'m the rarest treasure in the hoard, and the grown-up keeps my bowl. What am I?', 'The Dragon Gem', 'A silver coin', 'A gumdrop', 'Legendary! Only the luckiest find me.'],
    ['What has words inside but never speaks?', 'A book', 'A parrot', 'A mailbox', 'Like the storybook about me!'],
    ['What do you call a witch at the beach?', 'A sand-witch', 'A wave-witch', 'A shell-witch', 'Ha! I\'d like mine with toffee.'],
    ['What month has 28 days?', 'All of them', 'February', 'None of them', 'Clever! Every month has at least 28.'],
    ['What has a neck but no head?', 'A bottle', 'A giraffe', 'A sweater', 'A bottle of fizzy dragon soda!'],
    ['What is orange and sounds like a parrot?', 'A carrot', 'A pumpkin', 'A goldfish', 'A carrot! Even dragons eat their veggies.'],
    ['What did the dragon hatch from, long ago?', 'A crystal egg', 'A candy wrapper', 'A volcano', 'And your crystal is a piece of it!'],
    ['What runs all day but never walks?', 'A river', 'A clock', 'A cheetah', 'Splish, splash!'],
    ['What kind of tree can you hold in your hand?', 'A palm tree', 'A pine tree', 'A candy tree', 'High five! Or is it high palm?'],
    ['Why didn\'t the skeleton go to the party?', 'He had no body to go with', 'He was too tired', 'He lost his hat', 'No body! Ha! Oh, that\'s a good one.'],
    ['What lights up the cave on Halloween night, spark by spark?', 'The travelers', 'The goblins', 'The bats', 'That\'s you! Happy Halloween, traveler!'],
  ];
  const WRONG = ['Hmm, not quite. Try another!', 'Ooh, close-ish. Have another think!', 'That\'s a silly one! Try again.', 'Nope! Snuffwick guessed that too.'];

  const $ = id => document.getElementById(id);
  DG.crystalLine($('pl-crystal'));
  const q = new URLSearchParams(location.search);
  const N = RIDDLES.length;
  const isLit = n => !!DG.flag(`riddle.n${n}`);
  const firstUnsolved = from => { for (let k = 0; k < N; k++) { const m = (from + k - 1) % N + 1; if (!isLit(m)) return m; } return 0; };
  const asked = Number(q.get('n'));
  const n = asked >= 1 && asked <= N ? asked : firstUnsolved(1) || 1;
  const [question, answer, ...rest] = RIDDLES[n - 1];
  const cheer = rest.pop();
  $('rd-k').textContent = `Riddle ${n} of ${N}`;
  $('rd-q').textContent = question;

  // the choices in an order that is the same every visit
  const choices = [answer, ...rest];
  let h = (n - 1) * 2654435761 >>> 0;
  for (let i = choices.length - 1; i > 0; i--) { h = Math.imul(h ^ (h >>> 13), 0x5bd1e995) >>> 0; const j = h % (i + 1); [choices[i], choices[j]] = [choices[j], choices[i]]; }

  const dragon = $('rd-dragon'), say = $('rd-say'), box = $('rd-choices'), next = $('rd-next');
  let misses = 0;
  box.replaceChildren(...choices.map(c => {
    const b = document.createElement('button');
    b.type = 'button'; b.className = 'rd-choice'; b.textContent = c;
    b.addEventListener('click', () => pick(b, c));
    return b;
  }));
  if (isLit(n)) showSolved(false);

  function showSolved(fresh) {
    for (const b of box.children) { b.disabled = true; if (b.textContent === answer) b.classList.add('is-right'); }
    dragon.src = '/img/butterscotch_delight.webp';
    say.className = 'rd-say is-good';
    say.textContent = `“${cheer}”` + (fresh ? '' : ' You already solved this one.');
    const m = firstUnsolved(n % N + 1);
    if (m) { next.hidden = false; next.href = `?n=${m}`; next.textContent = `Next riddle (${m} of ${N}) ›`; }
    else { next.hidden = true; say.append(' Every candle is lit. You solved all ' + N + '!'); }
    candles();
  }

  async function pick(b, c) {
    if (c !== answer) {
      b.disabled = true; b.classList.add('is-wrong');
      dragon.src = '/img/butterscotch_yawn.webp';
      say.className = 'rd-say'; say.textContent = `“${WRONG[misses++ % WRONG.length]}”`;
      DG.chime(-7, 0.05);
      return;
    }
    DG.flag(`riddle.n${n}`, 1);
    showSolved(true);
    dragon.classList.remove('is-happy'); void dragon.offsetWidth; dragon.classList.add('is-happy');
    [0, 4, 7, 12].forEach((p, i) => setTimeout(() => DG.chime(p, 0.08), i * 110));
    const v = await DG.addSparks('riddle');
    const note = document.createElement('span');
    note.innerHTML = v.failed ? 'Your candle is lit!' : DG.sparkNote(v);
    say.append(document.createElement('br'), note);
  }

  function candles() {
    const list = $('rd-candles');
    list.replaceChildren(...Array.from({ length: N }, (_, i) => {
      const li = document.createElement('li');
      if (i + 1 !== n) {   // every other candle opens its riddle
        const a = document.createElement('a');
        a.href = `?n=${i + 1}`; a.textContent = String(i + 1); a.setAttribute('aria-label', `Riddle ${i + 1}`);
        li.append(a);
      } else { li.textContent = String(i + 1); li.classList.add('is-today'); }
      if (isLit(i + 1)) li.classList.add('is-lit');
      li.title = `Riddle ${i + 1}`;
      return li;
    }));
  }
  candles();
})();
