// input.js - touch / mouse on the canvas as the firmware's touch::Gestures (touch.h): tap =
// down -> up < 450 ms and < 40 px of travel; longPress = held >= longPressMs without moving (once
// per press). Coordinates are in the 800x480 show space whatever the canvas's on-screen size.
let s_canvas = null;
let s_down = false, s_at = { x: 0, y: 0 }, s_downAt = { x: 0, y: 0 }, s_downMs = 0, s_moved = false, s_longFired = false;
let s_tapPending = null, s_pressStartPending = false, s_longPending = null;
let s_pointerId = null;
const TAP_MS = 450, TAP_PX = 40;

function toShow(ev) {
  const r = s_canvas.getBoundingClientRect();
  return { x: Math.round((ev.clientX - r.left) * 800 / r.width), y: Math.round((ev.clientY - r.top) * 480 / r.height) };
}

export const input = {
  attach(canvas) {
    s_canvas = canvas;
    canvas.style.touchAction = 'none';
    canvas.addEventListener('pointerdown', ev => {
      if (s_down) return;                              // one finger at a time
      s_pointerId = ev.pointerId;
      try { canvas.setPointerCapture(ev.pointerId); } catch {}
      s_down = true; s_moved = false; s_longFired = false;
      s_at = s_downAt = toShow(ev); s_downMs = performance.now();
      s_pressStartPending = true;
      ev.preventDefault();
    });
    canvas.addEventListener('pointermove', ev => {
      if (!s_down || ev.pointerId !== s_pointerId) return;
      s_at = toShow(ev);
      if (Math.hypot(s_at.x - s_downAt.x, s_at.y - s_downAt.y) >= TAP_PX) s_moved = true;
    });
    const up = ev => {
      if (!s_down || ev.pointerId !== s_pointerId) return;
      s_down = false;
      const held = performance.now() - s_downMs;
      if (!s_moved && held < TAP_MS && ev.type === 'pointerup') s_tapPending = { ...s_downAt };
      s_pointerId = null;
    };
    canvas.addEventListener('pointerup', up);
    canvas.addEventListener('pointercancel', up);
    canvas.addEventListener('contextmenu', ev => ev.preventDefault());
  },
  ok() { return !!s_canvas; },
  // Once per frame: the gestures since the last call (edge flags are one-shot).
  update(longPressMs = 2500) {
    const now = performance.now();
    const g = { down: s_down, at: { ...s_at }, tap: false, tapAt: { x: 0, y: 0 }, pressStart: s_pressStartPending,
                longPress: false, longAt: { x: 0, y: 0 }, heldMs: s_down ? now - s_downMs : 0 };
    s_pressStartPending = false;
    if (s_tapPending) { g.tap = true; g.tapAt = s_tapPending; s_tapPending = null; }
    if (s_down && !s_moved && !s_longFired && now - s_downMs >= longPressMs) { s_longFired = true; g.longPress = true; g.longAt = { ...s_downAt }; }
    return g;
  },
  hit(p, x, y, w, h) { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; },
};
