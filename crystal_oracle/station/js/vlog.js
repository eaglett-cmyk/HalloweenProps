// vlog.js - the station's logs (vlog.h): visits_<year>.csv and events.log, echoed to the console
// and appended on the station server (/api/log -> station/logs/). Never names anyone: the crystal
// column is a salted hash of the UID.
import { kindName } from './token.js';

let s_year = 0;
let s_t0 = performance.now();
const s_queue = [];
let s_flushing = false;
const s_recent = [];              // last lines for the host menu
const MAX_RECENT = 60;

async function flush() {
  if (s_flushing || !s_queue.length) return;
  s_flushing = true;
  while (s_queue.length) {
    const item = s_queue[0];
    try {
      const r = await fetch('/api/log', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(item) });
      if (!r.ok) throw new Error('HTTP ' + r.status);
      s_queue.shift();
    } catch { break; }           // keep it, try again on the next line / later
  }
  s_flushing = false;
}
setInterval(flush, 5000);

function enqueue(file, line) {
  s_queue.push({ file, line });
  if (s_queue.length > 500) s_queue.shift();
  flush();
}
const ms = () => Math.round(performance.now() - s_t0);
const csvSafe = s => String(s ?? '').replace(/[",\r\n]/g, ' ');

export const vlog = {
  begin(eventYear) { s_year = eventYear; this.event(`station app started (${navigator.userAgent})`); },
  visit(path, c, o, writeResult) {
    const id = c.uidLen ? c.anonId.toString(16).padStart(8, '0') : '';
    const line = o && o.number
      ? `${ms()},${id},${csvSafe(path)},${kindName(c.kind)},${o.visitsAfter},${o.number},${o.tier},${o.flags},${o.rank},${csvSafe(writeResult)}`
      : `${ms()},${id},${csvSafe(path)},${kindName(c.kind)},${c.visits},,,,,${csvSafe(writeResult)}`;
    console.log(`[${String(ms()).padStart(7)}] visit ${line}`);
    enqueue(`visits_${s_year}.csv`, line);
  },
  event(...parts) {
    const line = `[${String(ms()).padStart(7)}] ${parts.join(' ')}`;
    console.log(line);
    s_recent.push(line); if (s_recent.length > MAX_RECENT) s_recent.shift();
    enqueue('events.log', `${new Date().toISOString()} ${line}`);
  },
  recent() { return s_recent.slice(); },
  pending() { return s_queue.length; },
};
