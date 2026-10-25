// ini.js - the firmware's INI reader (settings.cpp parseLine / IniStore), in JS.
//   [section] then key = value; ';' or '#' starts a comment line; a value runs to the end of
//   the line, or is "quoted" / 'quoted' (then taken as is), or stops at a " ;" (blank + ;)
//   trailing comment. Keys are returned lower-cased as "section.key" (or the bare key before
//   any section). A repeated key: the last one wins. "[nobutton]" + "caption.charge = x" is
//   simply the key "nobutton.caption.charge", exactly what settings.text() looks up.
export function parseIni(text) {
  const out = new Map();
  let sec = '';
  for (let raw of String(text || '').split(/\r?\n/)) {
    let line = raw.trim();
    if (!line || line[0] === ';' || line[0] === '#') continue;
    if (line[0] === '[') {
      const close = line.indexOf(']');
      if (close < 0) continue;
      sec = line.slice(1, close).trim().toLowerCase();
      continue;
    }
    const eq = line.indexOf('=');
    if (eq < 0) continue;
    const key = line.slice(0, eq).trim().toLowerCase();
    if (!key) continue;
    let v = line.slice(eq + 1).replace(/^[ \t]+/, '');
    if (v[0] === '"' || v[0] === "'") {
      const q = v[0];
      const endq = v.indexOf(q, 1);
      v = endq >= 0 ? v.slice(1, endq) : v.slice(1);
    } else {
      for (let i = 0; i < v.length; i++) {
        if (v[i] === ';' && (i === 0 || v[i - 1] === ' ' || v[i - 1] === '\t')) { v = v.slice(0, i); break; }
      }
      v = v.replace(/[ \t]+$/, '');
    }
    out.set(sec ? `${sec}.${key}` : key, v);
  }
  return out;
}

// yes/no, on/off, true/false, 1/0 -> boolean (undefined if it isn't one)
export function parseBool(v) {
  const s = String(v ?? '').trim().toLowerCase();
  if (['1', 'yes', 'y', 'on', 'true'].includes(s)) return true;
  if (['0', 'no', 'n', 'off', 'false'].includes(s)) return false;
  return undefined;
}

// "#RRGGBB", "#RGB", "0xRRGGBB", "RRGGBB" -> 0xRRGGBB (undefined if not a colour)
export function parseColor(v) {
  let s = String(v ?? '').trim();
  if (s[0] === '#') s = s.slice(1);
  else if (/^0x/i.test(s)) s = s.slice(2);
  if (/^[0-9a-f]{6}$/i.test(s)) return parseInt(s, 16);
  if (/^[0-9a-f]{3}$/i.test(s)) return parseInt(s.split('').map(c => c + c).join(''), 16);
  return undefined;
}
