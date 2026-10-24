// Dragonglow storybook: turn the story pages like a book instead of scrolling.
// Progressive enhancement: without this script the pages simply stack and scroll.
(function () {
  'use strict';
  var book = document.getElementById('story');
  if (!book) return;
  var stage = book.querySelector('.book-stage');
  var pages = Array.prototype.slice.call(stage.querySelectorAll('.page'));
  if (pages.length < 2) return;

  var reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  var FLIP_MS = 850;
  var current = 0;
  var turning = false;
  var opening = false;

  document.body.classList.add('book-mode');
  var navs = Array.prototype.slice.call(book.querySelectorAll('.book-nav'));
  navs.forEach(function (n) { n.hidden = false; });
  var counters = Array.prototype.slice.call(book.querySelectorAll('.page-counter'));
  var prevBtns = Array.prototype.slice.call(book.querySelectorAll('.nav-prev'));
  var nextBtns = Array.prototype.slice.call(book.querySelectorAll('.nav-next'));
  var theEnd = book.querySelector('.the-end');

  // Deep link: #p3 opens on page 3.
  var m = /^#p(\d+)$/.exec(location.hash);
  if (m) current = Math.min(pages.length - 1, Math.max(0, parseInt(m[1], 10) - 1));

  pages.forEach(function (p, i) {
    p.classList.toggle('is-current', i === current);
    p.setAttribute('aria-hidden', i === current ? 'false' : 'true');
  });

  function render() {
    counters.forEach(function (c) { c.textContent = 'Page ' + (current + 1) + ' of ' + pages.length; });
    prevBtns.forEach(function (b) { b.disabled = current === 0; });
    var last = current === pages.length - 1;
    nextBtns.forEach(function (b) {
      var bottom = b.closest('.book-nav-bottom');
      if (bottom) {
        // On the last page the big button carries on to the grown-ups section.
        b.disabled = false;
        b.innerHTML = last ? 'For grown-ups &#x203A;' : 'Turn the page &#x203A;';
      } else {
        b.disabled = last;
      }
    });
    if (theEnd) theEnd.classList.toggle('is-visible', last);
    // Lazy images on the next page: start loading them now so the turn reveals a finished page.
    var nxt = pages[current + 1];
    if (nxt) Array.prototype.forEach.call(nxt.querySelectorAll('img[loading="lazy"]'), function (img) {
      img.loading = 'eager';
    });
    if (history.replaceState) history.replaceState(null, '', '#p' + (current + 1));
  }

  function go(to) {
    if (stage.classList.contains('is-closed') || opening || turning || to < 0 || to >= pages.length || to === current) return;
    var from = current;
    var forward = to > from;
    var out = pages[from], inn = pages[to];
    current = to;

    if (reduced) {
      out.classList.remove('is-current'); out.setAttribute('aria-hidden', 'true');
      inn.classList.add('is-current'); inn.setAttribute('aria-hidden', 'false');
      render();
      return;
    }

    turning = true;
    inn.classList.add('is-current');
    inn.setAttribute('aria-hidden', 'false');
    if (forward) {
      // The old page lifts at its right edge and folds over the spine, uncovering the new page beneath.
      out.classList.add('is-turning', 'turn-forward');
    } else {
      // Turning back: the new page comes in folded and lays flat over the old one.
      inn.classList.add('is-turning', 'turn-back');
      out.classList.add('is-under');
    }
    window.setTimeout(function () {
      out.classList.remove('is-current', 'is-turning', 'turn-forward', 'is-under');
      out.setAttribute('aria-hidden', 'true');
      inn.classList.remove('is-turning', 'turn-back');
      turning = false;
      render();
    }, FLIP_MS);
  }

  prevBtns.forEach(function (b) { b.addEventListener('click', function () { go(current - 1); }); });
  nextBtns.forEach(function (b) { b.addEventListener('click', function () {
    if (current === pages.length - 1) {
      var gu = document.getElementById('grownups');
      if (gu) gu.scrollIntoView({ behavior: reduced ? 'auto' : 'smooth' });
      return;
    }
    go(current + 1);
  }); });

  // Keyboard: arrows and space/backspace, only while the book is on screen.
  document.addEventListener('keydown', function (e) {
    if (e.target && /^(INPUT|TEXTAREA|SELECT|BUTTON)$/.test(e.target.tagName)) return;
    var r = stage.getBoundingClientRect();
    if (r.bottom < 0 || r.top > window.innerHeight || stage.classList.contains('is-closed')) return;
    if (e.key === 'ArrowRight' || e.key === 'PageDown' || e.key === ' ') { e.preventDefault(); go(current + 1); }
    else if (e.key === 'ArrowLeft' || e.key === 'PageUp' || e.key === 'Backspace') { e.preventDefault(); go(current - 1); }
  });

  // Swipe on the stage.
  var sx = null, sy = null;
  stage.addEventListener('touchstart', function (e) {
    if (e.touches.length !== 1) return;
    sx = e.touches[0].clientX; sy = e.touches[0].clientY;
  }, { passive: true });
  stage.addEventListener('touchend', function (e) {
    if (sx === null) return;
    var dx = e.changedTouches[0].clientX - sx, dy = e.changedTouches[0].clientY - sy;
    sx = sy = null;
    if (Math.abs(dx) > 50 && Math.abs(dx) > Math.abs(dy) * 1.5) go(dx < 0 ? current + 1 : current - 1);
  }, { passive: true });

  // Clicking the outer edge of a page turns it, like lifting a corner.
  stage.addEventListener('click', function (e) {
    if (stage.classList.contains('is-closed') || opening) return;
    if (window.getSelection && String(window.getSelection())) return;
    var page = pages[current];
    var r = page.getBoundingClientRect();
    var x = e.clientX - r.left;
    if (x > r.width * 0.88) go(current + 1);
    else if (x < r.width * 0.12) go(current - 1);
  });

  // The hardcover. The shut book is the stage's right half with the front board on top; opening
  // swings the board across the spine (it becomes the left board, key art inside), the cloth slab
  // grows to the whole spread, and page 1 appears between the boards.
  var front = stage.querySelector('.hc-front');
  var OPEN_MS = reduced ? 0 : 1250;
  function setShut(shut) {
    navs.forEach(function (n) { n.classList.toggle('is-shut', shut); });
    if (front) front.setAttribute('aria-expanded', shut ? 'false' : 'true');
  }
  function openBook(scroll, instant) {
    if (opening) return;
    if (!stage.classList.contains('is-closed')) {
      if (scroll) stage.scrollIntoView({ behavior: reduced ? 'auto' : 'smooth', block: 'center' });
      return;
    }
    if (current !== 0) go(0);
    stage.classList.remove('is-closed');
    if (instant || reduced) {
      stage.classList.add('is-open'); setShut(false);
      if (scroll) stage.scrollIntoView({ block: 'center' });
      return;
    }
    opening = true;
    stage.classList.add('is-opening');
    if (scroll) stage.scrollIntoView({ behavior: 'smooth', block: 'center' });
    window.setTimeout(function () {
      stage.classList.remove('is-opening');
      stage.classList.add('is-open');
      setShut(false);
      opening = false;
    }, OPEN_MS);
  }
  function closeBook() {
    if (opening) return;
    stage.classList.remove('is-open');
    stage.classList.add('is-closed');
    setShut(true);
    stage.scrollIntoView({ behavior: reduced ? 'auto' : 'smooth', block: 'center' });
  }
  if (front) {
    front.addEventListener('click', function (e) {
      if (stage.classList.contains('is-closed')) { e.stopPropagation(); openBook(true); }
    });
    front.addEventListener('keydown', function (e) {
      if ((e.key === 'Enter' || e.key === ' ') && stage.classList.contains('is-closed')) { e.preventDefault(); openBook(true); }
    });
  }
  var readBtn = document.querySelector('a[href="#story"]');
  if (readBtn) readBtn.addEventListener('click', function (e) { e.preventDefault(); openBook(true); });
  Array.prototype.forEach.call(book.querySelectorAll('.nav-close'), function (b) {
    b.addEventListener('click', closeBook);
  });
  setShut(true);
  // Arriving on a deep link (#story or #pN) means the book is already open.
  if (/^#(story|p\d+)$/.test(location.hash)) openBook(false, true);

  render();
})();
