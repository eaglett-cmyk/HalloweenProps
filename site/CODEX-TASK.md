# Task: make the page turn look like a real storybook page turning

You are working in `site/`, a static web page (no build step, no dependencies): `index.html`,
`style.css`, `book.js`. Open `index.html` in your head first: it is a children's storybook.

## What exists

- `main#story.book` holds `.book-stage`, a CSS grid that stacks eight `section.page` elements in the
  same grid cell (`grid-area: 1 / 1`), so the stage is as tall as the tallest page. Only the page with
  class `is-current` is visible. Each page is a two-column spread on desktop: `.page-art` (picture) and
  `.page-text` (words). Odd pages have art left / words right; pages with class `page-flip` are mirrored
  (words left / art right) via `direction: rtl`. Under 760px wide the page is one column (art above words).
- `book.js` runs `go(to)`: it adds `is-turning` + `turn-forward` (or `turn-back`) to the outgoing /
  incoming page, waits `FLIP_MS` (850 ms) and then swaps `is-current`. The CSS animates the WHOLE page
  as one sheet rotating about its LEFT edge (`page-forward` / `page-back` keyframes). That is the part
  that looks wrong: a real book does not rotate the entire spread; the RIGHT-HAND LEAF lifts at its outer
  edge, folds over the centre gutter, and lands on the left, showing its back face.
- A hardcover sits on the stage: `.hc-back` (cloth slab), `.hc-front` (front board; `.hc-face` cover,
  `.hc-inside` key art). Classes on the stage: `is-closed` (shut), `is-opening` (board swinging),
  `is-open`. Do not change how the cover opens or closes; keep every class name, every id, the nav
  buttons (`.nav-prev`, `.nav-next`, `.page-counter`, `.nav-close`), keyboard, swipe, edge-click and
  `#pN` deep links working exactly as they do now (see `book.js`).

## What to build

Replace the page-turn animation (desktop widths, the two-column spread) with a convincing paper leaf turn:

1. Treat the spread as two leaves hinged at the centre gutter (the stage's vertical middle).
2. **Forward turn:** the right half of the current page lifts from its outer (right) edge and rotates
   about the gutter through 180°, so its back face (which should show the LEFT half of the NEXT page)
   lands on the left. While it turns, the left half of the current page stays put until the leaf covers
   it, and the right half of the next page is revealed underneath from the start. At the end the next
   page is simply the current page.
3. **Back turn:** the mirror image: the left half lifts from its outer edge, rotates about the gutter
   to the right, and its back face shows the right half of the previous page.
4. Add the touches that sell it: a soft moving shadow/gradient on the turning leaf (darker near the
   gutter), a faint shadow cast onto the page beneath, a slight easing (fast in the middle of the
   rotation, gentle at the ends), `perspective` on the stage, `backface-visibility` handling, and the
   leaf drawn above everything while it moves. About 900 to 1100 ms total.
5. Implementation hint: during a turn, build a temporary "leaf" element inside `.book-stage` that
   clones the relevant halves (e.g. clone the current page and the next page into two faces, each
   clipped to one half with `clip-path: inset(0 0 0 50%)` / `inset(0 50% 0 0)` or an inner wrapper
   that is translated), position it over the stage, run the animation with the Web Animations API or
   CSS keyframes, then remove it and swap `is-current` as today. Images inside the clones must keep
   working (`loading="lazy"` on the clones should become eager). Clones must be `aria-hidden="true"`.
   The stage currently has `perspective: 2600px` and `padding: 14px`; the pages sit inside that padding.
6. Under 760px the page is a single column, so a half-leaf turn does not apply: keep a simple,
   clean turn there (the current whole-page rotation is acceptable, or a horizontal slide), as long
   as nothing overflows the viewport.
7. Respect `prefers-reduced-motion: reduce`: no animation, instant swap (already wired via `reduced`).
8. Keep it dependency-free, vanilla JS and CSS, and keep the file structure. Do not add a build step.

## How to verify

Run `node server.js` in this folder (serves http://127.0.0.1:8470/), open the page, click the cover
to open the book, then use the arrows. There is Chromium at
`C:\Program Files\Google\Chrome\Application\chrome.exe` if you want headless screenshots
(`--headless=new --screenshot=... --window-size=1200,900 http://127.0.0.1:8470/#p2`), but a careful
implementation plus a syntax check (`node -e "new (require('vm').Script)(require('fs').readFileSync('book.js','utf8'))"`)
is acceptable. Do not leave the server running when you finish.

When done, write a short summary of what you changed (files + approach) to `CODEX-RESULT.md` in this folder.
