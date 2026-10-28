// nick_filter.js — keeps crystal nicknames kind (server only: server.js refuses to serve it).
// A name is refused when, after undoing the usual tricks (sh!t, f.u.c.k, fuuuck, 4ss, a s s,
// Cyrillic look-alikes), it contains an ANYWHERE term, or one of its words is a WORD term
// (whole words only, plus plurals, so "Peacock", "Grape", "Classy" and "Butterscotch" pass).
// The built-in terms are base64 so this file doesn't read as a list of slurs. The owner can add
// more in data/nick_block.txt (git-ignored, re-read when it changes), one per line:
//   word      that whole word        word*    any word starting with it
//   *word*    anywhere in the name   # ...    a comment
// Saved names are checked again whenever they are shown, so adding a term hides old names too.
'use strict';
const fs = require('fs');
const path = require('path');

const unpack = b => Buffer.from(b, 'base64').toString('utf8').split(',');
const BUILTIN_ANYWHERE = unpack('ZnVjayxmdWssZnVxLHBodWNrLHNoaXQsY3VudCxiaXRjaCxiaWF0Y2gsYmFzdGFyZCx3aG9yZSxzbHV0LHBvcm4scHVzc2llcyxwZW5pcyx2YWdpbmEsZGlsZG8sb3JnYXNtLG1hc3R1cmJhdCxqaXp6LGJvb2JpZXMsdGl0dGllcyx0aXR0eSxjb2Nrc3VjayxkaWNraGVhZCxhc3Nob2xlLGFzc2hhdCxhc3N3aXBlLGR1bWJhc3MsamFja2FzcyxmYXRhc3MsYmFkYXNzLGFyc2Vob2xlLGJvbGxvY2ssdHdhdCx3YW5rZXIsbW90aGVyZixwaXNzLG5pZ2csbmlnZ2EsZmFnZ290LGZhZ290LHJldGFyZCx0cmFubnksd2V0YmFjayxuYXppLGhpdGxlcixra2ssc3VpY2lkZSxraWxseW91cnNlbGYscGVkb3BoaWwscGFlZG9waGlsLHNleCxzdWNrbXksbGlja215LGJsb3dqb2IsaGFuZGpvYixlcmVjdGlvbixob3JuaWVzdCxnZW5pdGFsLHRlc3RpY2xlLHNjcm90dW0sbmlwcGxlLGNsaXRvcixoZW50YWkseHh4LG9ubHlmYW5zLG1pbGYsY29jYWluZQ==');
const BUILTIN_WORD = unpack('YXNzLGFyc2UsYnV0dCxkaWNrLGNvY2sscHVzc3ksdGl0LHRpdHMsY3VtLGFuYWwsYW51cyxyYXBlLHJhcGVkLGJvb2IsYm9vYnMsaG9lLHRob3QsY3JhcCxkYW1uLGhlbGwscHJpY2ssYnVnZ2VyLHdhbmssaG9tbyxmYWcsc3BpYyxjaGluayxraWtlLGR5a2UsZ29vayxjb29uLHBlZG8scGFlZG8sbnVkZSxuYWtlZCxob3JueSxzZW1lbixjb25kb20sd2VlZCxtZXRoLGt5cyxzdGZ1LHd0ZixvbWZnLG5pZyxuZWdybyxzbGF2ZSxpc2lzLGhlcm9pbixyYXBpc3Q=');

// digits and symbols people swap for letters, and Cyrillic/Greek letters that look Latin
const LOOK = {
  '0': 'o', '1': 'i', '!': 'i', '|': 'i', '3': 'e', '4': 'a', '@': 'a', '5': 's', '$': 's', '7': 't', '8': 'b', '9': 'g',
  'а': 'a', 'в': 'b', 'е': 'e', 'ё': 'e', 'к': 'k', 'м': 'm', 'н': 'h', 'о': 'o', 'р': 'p', 'с': 'c', 'т': 't', 'у': 'y', 'х': 'x', 'і': 'i', 'ј': 'j', 'ѕ': 's',
  'α': 'a', 'β': 'b', 'ε': 'e', 'ι': 'i', 'κ': 'k', 'ν': 'v', 'ο': 'o', 'ρ': 'p', 'τ': 't', 'υ': 'u', 'χ': 'x',
  'ц': 'u', 'µ': 'u', 'ʋ': 'u', 'ս': 'u',
};

function fold(s) {
  return [...s.normalize('NFKD').replace(/\p{M}/gu, '').toLowerCase()].map(c => LOOK[c] || c).join('');
}
// each letter may repeat ("fuuuck"), so "ass" needs two s's but "asssss" still counts
const pat = term => [...term].map(c => `${c}+`).join('');

let extraMtime = -1, extra = { anywhere: [], word: [], prefix: [] };
function loadExtra() {
  const file = path.join(__dirname, 'data', 'nick_block.txt');
  let st = null;
  try { st = fs.statSync(file); } catch { /* no extras */ }
  const m = st ? st.mtimeMs : 0;
  if (m === extraMtime) return;
  extraMtime = m; extra = { anywhere: [], word: [], prefix: [] };
  if (!st) return;
  for (const raw of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
    if (raw.trim().startsWith('#')) continue;
    const line = fold(raw.trim()).replace(/[^a-z*]/g, '');
    if (!line) continue;
    if (line.startsWith('*') && line.endsWith('*') && line.length > 2) extra.anywhere.push(line.slice(1, -1));
    else if (line.endsWith('*') && line.length > 1) extra.prefix.push(line.slice(0, -1));
    else if (!line.includes('*')) extra.word.push(line);
  }
}

let built = null, builtFor = -2;
function rules() {
  loadExtra();
  if (built && builtFor === extraMtime) return built;
  const any = [...BUILTIN_ANYWHERE, ...extra.anywhere].filter(Boolean).map(pat);
  const word = [...BUILTIN_WORD, ...extra.word].filter(Boolean).map(pat);
  const prefix = extra.prefix.filter(Boolean).map(p => `${pat(p)}[a-z]*`);
  built = {
    anywhere: new RegExp(any.join('|')),
    word: new RegExp(`^(?:${[...word, ...prefix].join('|')})(?:e?s|z)?$`),
  };
  builtFor = extraMtime;
  return built;
}

function hits(r, f) {
  const joined = f.replace(/[^a-z]/g, '');                         // "f.u.c.k" -> "fuck"
  const words = f.split(/[^a-z]+/).filter(Boolean);
  return r.anywhere.test(joined) || words.some(w => r.word.test(w)) || r.word.test(joined);   // last: "a s s" -> "ass"
}

// true when the name is fine to save and show. Two readings: digits and symbols as the letters
// they stand for ("sh!t"), and with them dropped ("Sparkles!" must not read as "sparklesi").
function nicknameOk(name) {
  if (typeof name !== 'string' || !name) return true;
  if (/(^|\D)(69|420)(\D|$)/.test(name)) return false;
  const r = rules();
  return !hits(r, fold(name)) && !hits(r, fold(name.replace(/[0-9!|@$]/g, '')));
}

module.exports = { nicknameOk };
