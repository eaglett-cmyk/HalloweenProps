# Page-turn update

- `book.js`: desktop turns now build a temporary two-sided half-width leaf hinged at the centre gutter. Forward turns carry the current right half onto the next left half; backward turns mirror this. The stationary half stays above the destination spread until covered. Clones are aria-hidden, inert, stripped of duplicate IDs, and use eager images. Completion, resize, and close remove the temporary elements and listeners.
- `style.css`: added a one-second eased 3D rotation, correctly oriented back faces, animated paper shading and a soft cast shadow. Phones use a clipped 350 ms slide. Reduced motion retains instant swaps. Cover mechanics and navigation handlers are preserved.
- `index.html`: only bumped CSS/JS cache versions to `g6`, required by the documented one-day asset caching policy.

Validation: JavaScript syntax check and `git diff --check` passed. The local server returned HTTP 200 for the page, updated CSS/JS, and sample artwork, then was stopped. Headless Chrome could not establish a debugging session in this environment, so visual/browser interaction checks remain unverified. No dependencies or build step were added.
