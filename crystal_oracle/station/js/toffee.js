// toffee.js - chapter 2 (2027, "The Egg in the Sock"; docs/stories/CHAPTER2-TOFFEE.md): baby Toffee naps in
// Snuffwick's sock beside Butterscotch in every scene. She is drawn live over the clips (sprites/toffee_<pose>.png,
// built by build_assets.py --ipad), so one set of clips serves all four stages. On with [station] chapter >= 2.
//
// Stage = how far a crystal's glow wakes her = how full tonight's beacon is (show.js passes it):
//   0 fast asleep (< 1/3)  1 drowsy (< 2/3)  2 nearly awake (< full)  3 wide awake (after the finale)
import { media } from './media.js';
import * as fx from './fx.js';

const POSES = ['sleep', 'drowsy', 'yawn', 'awake', 'flap'];
const path = p => `/oracle/sprites/toffee_${p}.png`;
const X = 200, BOTTOM = 356, HEIGHT = 150;   // her spot on the cave floor, lower left (clear of the beacon above)
const FADE_MS = 250, GLOW_AT_MS = 600;       // the absorb beam reaches the dragons at ~0.6 s
const HALO = 0xFFE2B8;                       // her soft warm night-light
let s_on = false, s_pose = 'sleep', s_prev = '', s_changeAt = -1e9;

function setPose(p, nowMs) { if (p !== s_pose) { s_prev = s_pose; s_pose = p; s_changeAt = nowMs; } }

// The pose for a moment of the show. scene: 'idle' | 'seq' (a crystal / blessing) | 'finale'; since = ms into it.
function poseFor(scene, stage, since, reveal) {
  if (scene === 'finale') return since < 1500 ? 'yawn' : since < 3200 ? 'awake' : 'flap';
  if (scene === 'idle') return stage >= 3 ? ((since % 5000) < 2500 ? 'awake' : 'flap') : 'sleep';
  if (since < GLOW_AT_MS) return stage >= 3 ? 'awake' : 'sleep';
  if (stage === 0) return 'sleep';
  if (stage === 1) return 'drowsy';
  if (stage === 2) return since < GLOW_AT_MS + 1800 ? 'yawn' : 'awake';
  return reveal ? 'flap' : 'awake';
}

function drawPose(g, pose, alpha, nowMs, wiggle) {
  const bm = media.sprite(path(pose));
  if (!bm || alpha <= 0) return;
  const h = HEIGHT, w = bm.width * h / bm.height;
  let dy = 0, sy = 1, rot = 0;
  if (pose === 'sleep') sy = 1 + 0.018 * Math.sin(nowMs * 2 * Math.PI / 3200);          // breathing
  else if (pose === 'drowsy') rot = 0.035 * Math.sin(nowMs * 2 * Math.PI / 2600);       // nodding off
  else if (pose === 'awake') dy = -3 * fx.wave(nowMs, 1200);
  else if (pose === 'flap') dy = -10 - 8 * fx.wave(nowMs, 500);
  rot += wiggle;
  g.save();
  g.globalAlpha = alpha;
  g.translate(X + fx.SAFE_L * 0.5, BOTTOM + dy);
  g.rotate(rot);
  g.scale(1, sy);
  g.drawImage(bm, -w / 2, -h, w, h);
  g.restore();
}

export const toffee = {
  on() { return s_on; },
  // chapter 2 and her sprites on the card
  load(chapter) {
    s_on = chapter >= 2 && media.exists(path('sleep'));
    if (s_on) for (const p of POSES) if (media.exists(path(p))) media.sprite(path(p));
    return s_on;
  },
  stageFor(points, goal, full) {
    if (full) return 3;
    const f = points / (goal || 1);
    return f >= 2 / 3 ? 2 : f >= 1 / 3 ? 1 : 0;
  },
  // Draws her for this frame. o: { scene, stage, since, reveal, hidden, rgb (the crystal's glow, 0 = none) }
  draw(g, nowMs, o) {
    if (!s_on || o.hidden) return;
    setPose(poseFor(o.scene, o.stage, o.since, o.reveal), nowMs);
    fx.addGlow(X + fx.SAFE_L * 0.5, BOTTOM - HEIGHT * 0.42, 92, HALO, 70);   // lifts her purple off the candy pile
    if (o.rgb && o.scene !== 'idle' && o.since >= GLOW_AT_MS) {
      const f = Math.min(1, (o.since - GLOW_AT_MS) / 400);
      fx.addGlow(X + fx.SAFE_L * 0.5, BOTTOM - HEIGHT * 0.45, 78, o.rgb, 120 * f);
    }
    // stage 0: the glow washes over her and she wriggles, but stays asleep
    const ws = o.since - GLOW_AT_MS;
    const wiggle = (o.scene === 'seq' && o.stage === 0 && ws >= 0 && ws < 700) ? 0.07 * Math.sin(ws / 45) * (1 - ws / 700) : 0;
    const f = Math.min(1, (nowMs - s_changeAt) / FADE_MS);
    if (f < 1 && s_prev) drawPose(g, s_prev, 1, nowMs, wiggle);
    drawPose(g, s_pose, f < 1 && s_prev ? f : 1, nowMs, wiggle);
  },
};
