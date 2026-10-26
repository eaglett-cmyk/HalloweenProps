// import_visits.js — copy the station's visit logs into data/ so each crystal's page
// (/c/<id>) can show its history. Run after Halloween night (or any time; re-running
// just replaces the copies):
//
//   node import_visits.js <folder or file> [source]
//
//   <folder>  e.g. the TF card's E:\oracle\logs, or ..\crystal_oracle\station\logs
//   [source]  a label kept in the copy's name so two stations don't overwrite each
//             other (default: screen) -> data\visits_2026_screen.csv
//
// The site re-reads data\ on the next request; no restart. data\ is git-ignored and
// never served as files.
'use strict';
const fs = require('fs');
const path = require('path');

const [src, label = 'screen'] = process.argv.slice(2);
if (!src) {
  console.error('usage: node import_visits.js <folder or visits_<year>.csv> [source]');
  process.exit(1);
}
if (!/^[a-z0-9-]+$/i.test(label)) {
  console.error('source label: letters, digits and dashes only');
  process.exit(1);
}
const dataDir = path.join(__dirname, 'data');
fs.mkdirSync(dataDir, { recursive: true });

const files = fs.statSync(src).isDirectory()
  ? fs.readdirSync(src).filter(n => /^visits_\d{4}\.csv$/i.test(n)).map(n => path.join(src, n))
  : [src];
if (!files.length) {
  console.error(`no visits_<year>.csv in ${src}`);
  process.exit(1);
}
for (const f of files) {
  const m = path.basename(f).match(/^visits_(\d{4})\.csv$/i);
  if (!m) { console.error(`skipped ${f} (not visits_<year>.csv)`); continue; }
  const dest = path.join(dataDir, `visits_${m[1]}_${label}.csv`);
  fs.copyFileSync(f, dest);
  const rows = fs.readFileSync(dest, 'utf8').split(/\r?\n/).filter(l => /,ok\s*$/.test(l)).length;
  console.log(`${f} -> ${path.relative(__dirname, dest)} (${rows} crystal visits written)`);
}
