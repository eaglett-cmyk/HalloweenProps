"""
============================================================================
 co_firmware.py  -  text files shared with the firmware (Crystal Oracle)
----------------------------------------------------------------------------
   * config.ini template  <- the firmware's own sources, so it can't drift:
       - KEYS, sections, order and notes from the FIELDS table in
         firmware/crystal_oracle/settings.cpp ({"section", "key", FT::type,
         &cfg.field, lo, hi, "note"}) - exactly what settings::load() reads;
       - DEFAULT VALUES from struct Settings in settings.h ("// [section]"
         comment lines, one "type name = default;  // comment" per line).
     Lines are written the way settings::save() writes them
     ("%-17s = %-8s ; note"), bools as yes/no, enums as plinth/auto/...
     Without settings.cpp the keys fall back to snake_case field names.
   * captions.ini         <- manifest "words" ("section.key": "text"), grouped
     by section = the text before the FIRST dot, so the no-button overrides
     "nobutton.caption.charge" land in [nobutton] as "caption.charge = ..."
     (the firmware joins them back to "nobutton.caption.charge"); top-level
     keys (title, character) come first. Values the
     firmware's INI reader would trim or cut (leading/trailing blanks, " ;")
     are written in quotes, which that reader strips. An [anchor] section
     (clip id = x,y,r: the lantern glow per built clip, co_layers.anchor)
     follows the words.
   * story_defaults.h     <- the same words as C++ (--gen-defaults), keeping the
     existing file's comment header and its line layout.
   * README.txt for the card.

 Python 3 standard library only.
============================================================================
"""
import re

# ---------------------------------------------------------------------------
# settings.h -> config.ini
# ---------------------------------------------------------------------------
_FIELD_RE = re.compile(r"^\s*(bool|char|u?int(?:8|16|32)_t|int|float)\s+(\w+)\s*(\[\s*\d+\s*\])?\s*=\s*([^;]+?)\s*;\s*(?://\s*(.*))?$")
_SECTION_RE = re.compile(r"^\s*//\s*\[(\w+)\]\s*(.*)$")
_ENUM_RE = re.compile(r"enum\s+(\w+)\s*:\s*\w+\s*\{([^}]*)\}")


class Field:
    def __init__(self, section, name, ctype, raw, comment, is_array):
        self.section, self.name, self.ctype, self.raw, self.comment, self.is_array = section, name, ctype, raw, comment, is_array

    @property
    def key(self):
        return "%s.%s" % (self.section, self.name)


def parse_settings_h(path):
    """Returns (fields, enums, section_notes). enums = {PREFIX_: [names...]}."""
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    enums = {}
    for _, body in _ENUM_RE.findall(text):
        names = [n.split("=")[0].strip() for n in body.split(",") if n.strip()]
        if names:
            prefix = names[0].split("_")[0] + "_"
            enums[prefix] = names
    m = re.search(r"struct\s+Settings\s*\{(.*?)\n\};", text, re.S)
    fields, notes = [], {}
    if not m:
        return fields, enums, notes
    section = None
    for line in m.group(1).splitlines():
        s = _SECTION_RE.match(line)
        if s:
            section = s.group(1)
            notes[section] = s.group(2).strip()
            continue
        f = _FIELD_RE.match(line)
        if f and section:
            fields.append(Field(section, f.group(2), f.group(1), f.group(4).strip(), (f.group(5) or "").strip(),
                                bool(f.group(3))))
    return fields, enums, notes


def ini_value(field):
    """settings.h default -> the text settings::save() would write for it."""
    raw = field.raw
    if field.ctype == "bool":
        return "yes" if raw == "true" else "no"
    if field.is_array and raw.startswith('"'):
        return raw[1:-1]
    em = re.match(r"^([A-Z]+_)([A-Z0-9_]+)$", raw)
    if em:
        return em.group(2).lower()
    return raw


def enum_choices(field, enums):
    em = re.match(r"^([A-Z]+_)[A-Z0-9_]+$", field.raw)
    if em and em.group(1) in enums:
        return [n[len(em.group(1)):].lower() for n in enums[em.group(1)]]
    return None


def snake(name):
    return re.sub(r"(?<=[a-z0-9])([A-Z])", r"_\1", name).lower()


_ROW_RE = re.compile(r'\{\s*"(\w+)"\s*,\s*"(\w+)"\s*,\s*FT::(\w+)\s*,\s*&?\s*cfg\.(\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*'
                     r'"((?:[^"\\]|\\.)*)"\s*\}')


def parse_settings_table(cpp_path):
    """The FIELDS table of settings.cpp: [(section, key, ftype, cfg_field, note)], or None if
    settings.cpp is missing / has no recognisable table."""
    try:
        with open(cpp_path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return None
    m = re.search(r"\bFIELDS\s*\[\s*\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        return None
    rows = [(s, k, t, fld, c_unescape(note)) for s, k, t, fld, _lo, _hi, note in _ROW_RE.findall(m.group(1))]
    return rows or None


def check_settings_table(fields, table):
    """Cross-check settings.h <-> settings.cpp. Returns warning strings."""
    names = {f.name for f in fields}
    used = {r[3] for r in table}
    out = ["settings.cpp FIELDS row [%s] %s points at cfg.%s, which settings.h does not define" % (s, k, fld)
           for s, k, _t, fld, _n in table if fld not in names]
    out += ["settings.h field %s has no config.ini key in settings.cpp (it can't be set from the card)" % f.name
            for f in fields if f.name not in used]
    return out


def config_template(fields, enums, table, sources_rel):
    """The /oracle/config.ini the card starts with. table = parse_settings_table() or None."""
    by_name = {f.name: f for f in fields}
    rows = []
    if table:
        for sec, key, _t, fld, note in table:
            f = by_name.get(fld)
            if f:
                rows.append((sec, key, ini_value(f), note))
    else:
        for f in fields:
            ch = enum_choices(f, enums)
            note = f.comment + ("  (%s)" % " | ".join(ch) if ch else "") + ("  (yes | no)" if f.ctype == "bool" else "")
            rows.append((f.section, snake(f.name), ini_value(f), note.strip()))
    out = [
        "; ==========================================================================",
        ";  Crystal Oracle - station settings  (/oracle/config.ini on the TF card)",
        ";  key = value, one per line. ';' starts a comment. Unknown keys are ignored",
        ";  and missing keys use the firmware defaults. yes/no, on/off, 1/0 all work.",
        ";  The service menu rewrites this file; the previous one is kept as .bak",
        ";",
        ";  Written by tools/build_assets.py ONLY when the card has no config.ini.",
        ";  Keys + notes: %s (FIELDS table)" % sources_rel[0],
        ";  Values: the firmware defaults from %s" % sources_rel[1],
        ";  so this template and the firmware stay in sync by construction -",
        ";  change defaults in the firmware, not here.",
        "; ==========================================================================",
    ]
    section = None
    for sec, key, val, note in rows:
        if sec != section:
            section = sec
            out.append("")
            out.append("[%s]" % sec)
        out.append(("%-17s = %-8s ; %s" % (key, ini_quote(val), note)).rstrip() if note else "%s = %s" % (key, ini_quote(val)))
    out.append("")
    return "\n".join(out)


def ini_quote(v):
    """Quote a value when the firmware's INI reader would otherwise trim it or cut it at ' ;'.
    Returns None if it can't be represented (contains both quote characters and needs quoting)."""
    needs = v != v.strip() or bool(re.search(r"[ \t];", v)) or v[:1] in ("\"", "'")
    if not needs:
        return v
    for q in ('"', "'"):
        if q not in v:
            return q + v + q
    return None


# ---------------------------------------------------------------------------
# captions.ini
# ---------------------------------------------------------------------------
def group_words(words):
    """[(section or '', [(key, value)])] in first-appearance order, top level first. The section is the text
    before the first dot: "nobutton.caption.charge" -> ("nobutton", "caption.charge")."""
    top, secs = [], {}
    order = []
    for k, v in words.items():
        if "." in k:
            sec, key = k.split(".", 1)
            if sec not in secs:
                secs[sec] = []
                order.append(sec)
            secs[sec].append((key, v))
        else:
            top.append((k, v))
    return ([("", top)] if top else []) + [(s, secs[s]) for s in order]


def captions_ini(words, title, manifest_rel, anchors=None):
    """anchors: [(clip id, (x, y, r))] -> an [anchor] section (settings::text("anchor.<clip>"))."""
    out = [
        "; /oracle/captions.ini - every word the Crystal Oracle shows on screen",
        "; GENERATED by tools/build_assets.py from %s (\"words\")." % manifest_rel,
        "; Quick fixes on the card are fine, but the next build overwrites this file:",
        "; make lasting changes in the manifest. A value runs to the end of the line",
        "; (or is \"quoted\" when it has edge blanks or a ' ;' that must be kept).",
        "; Colours are #RRGGBB; bowl shapes are circle | triangle | star. A crystal colour",
        "; ([color] N_hex) may also be 2-4 stops \"#RRGGBB,#RRGGBB\" (cycled, ~4 s) or rainbow;",
        "; the colours in use are 1..N up to the first empty N_hex.",
    ]
    if title:
        out.append("; Show: %s" % title)
    for sec, items in group_words(words):
        out.append("")
        if sec:
            out.append("[%s]" % sec)
        for k, v in items:
            q = ini_quote(v)
            out.append(("%s = %s" % (k, q if q is not None else v)).rstrip())
    if anchors:
        out += ["",
                "[anchor]",
                "; Narrator glow per clip: <clip id> = x,y,r in screen pixels (GENERATED: the clip's",
                "; anchor layer at its first visible moment + that still's \"anchor\" in the manifest;",
                "; only clips built from real art are listed)."]
        for cid, (x, y, r) in anchors:
            out.append("%s = %d,%d,%d" % (cid, x, y, r))
    out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# story_defaults.h
# ---------------------------------------------------------------------------
ARRAY_LINE = "static const StoryDefault STORY_DEFAULTS[] = {"
PACK = {"rank": 4}                       # sections whose single entries share lines
ATTACH = ("_color", "_shape", "_hex")    # keys that sit on the line of their base key

DEFAULT_HEADER = """#pragma once
// ============================================================================
//  story_defaults.h — built-in words for the show, used when /oracle/captions.ini
//  is missing a key. GENERATED by tools/build_assets.py --gen-defaults from
//  assets/manifest.json (edit the manifest, not this file).
// ============================================================================

struct StoryDefault { const char *key; const char *value; };

""" + ARRAY_LINE + "\n"

_PAIR_RE = re.compile(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}')


def c_unescape(s):
    out = bytearray()
    i = 0
    b = s.encode("utf-8")
    simple = {ord("n"): 10, ord("t"): 9, ord("r"): 13, ord("\\"): 92, ord('"'): 34, ord("'"): 39, ord("?"): 63}
    while i < len(b):
        if b[i] == 92 and i + 1 < len(b):
            c = b[i + 1]
            if c in simple:
                out.append(simple[c])
                i += 2
                continue
            if 48 <= c <= 55:
                j = i + 1
                while j < len(b) and j < i + 4 and 48 <= b[j] <= 55:
                    j += 1
                out.append(int(b[i + 1:j], 8))
                i = j
                continue
            if c == ord("x"):
                j = i + 2
                while j < len(b) and chr(b[j]) in "0123456789abcdefABCDEF":
                    j += 1
                out.append(int(b[i + 2:j], 16) & 0xFF)
                i = j
                continue
        out.append(b[i])
        i += 1
    return out.decode("utf-8", "replace")


def c_escape(s):
    """C++ string literal body. Non-ASCII -> 3-digit octal escapes (can't swallow the next char)."""
    out = []
    prev_q = False
    for ch in s:
        o = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "?" and prev_q:
            out.append("\\?")              # never form a trigraph
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif o < 32 or o == 127:
            out.append("\\%03o" % o)
        elif o > 126:
            out.extend("\\%03o" % b for b in ch.encode("utf-8"))
        else:
            out.append(ch)
        prev_q = ch == "?"
    return "".join(out)


def parse_story_defaults(path):
    """Returns (header text up to and including the array line, [(key, value)]) or (None, [])."""
    try:
        with open(path, "r", encoding="utf-8", newline="") as f:
            text = f.read()
    except OSError:
        return None, []
    i = text.find(ARRAY_LINE)
    if i < 0:
        return None, []
    j = text.find("\n", i)
    header = text[:j + 1]
    body = text[j + 1:]
    end = body.find("};")
    pairs = [(c_unescape(k), c_unescape(v)) for k, v in _PAIR_RE.findall(body[:end] if end >= 0 else body)]
    return header, pairs


def story_defaults_body(words):
    lines = []
    first = True
    for sec, items in group_words(words):
        if not first:
            lines.append("")
        first = False
        groups = []                          # [[(full key, value), ...]] = one logical item
        base = {}
        for k, v in items:
            full = "%s.%s" % (sec, k) if sec else k
            att = next((a for a in ATTACH if k.endswith(a) and k[:-len(a)] in base), None)
            if att:
                groups[base[k[:-len(att)]]].append((full, v))
            else:
                base[k] = len(groups)
                groups.append([(full, v)])
        per = PACK.get(sec, 1)
        row = []
        for g in groups:
            row.append(g)
            if len(row) >= per or len(g) > 1:
                lines.append("  " + ", ".join('{"%s", "%s"}' % (c_escape(k), c_escape(v)) for gg in row for k, v in gg) + ",")
                row = []
        if row:
            lines.append("  " + ", ".join('{"%s", "%s"}' % (c_escape(k), c_escape(v)) for gg in row for k, v in gg) + ",")
    return "\n".join(lines) + "\n"


# Header comment lines refreshed on every --gen-defaults (old text -> new text); the rest of an existing
# header is kept as it is.
HEADER_REFRESH = [
    ("//    ui.*                                   fixed interface words\n// ====",
     "//    ui.*                                   fixed interface words\n"
     "//    meter.*, fx.*, glow.idle_color         story features (manifest.schema.md)\n"
     "//    nobutton.<key>                         used instead of <key> when [station] button = no\n"
     "//    nobutton.voice                         \"from=to;...\" voice group / line swaps (no button)\n"
     "// ===="),
    ("//    color.1 .. color.6 (+ color.N_hex)     crystal colour names / colours\n",
     "//    color.1 .. color.6 (+ color.N_hex)     crystal colour names / colour specs: \"#RRGGBB\",\n"
     "//                                           2-4 stops \"#RRGGBB,#RRGGBB\" (cycled) or \"rainbow\";\n"
     "//                                           in use = 1..N up to the first empty N_hex\n"),
]


def gen_story_defaults(words, existing_header):
    header = existing_header or DEFAULT_HEADER
    nl = "\r\n" if "\r\n" in header else "\n"
    header = header.replace("\r\n", "\n")
    for old, new in HEADER_REFRESH:
        header = header.replace(old, new)
    text = header + story_defaults_body(words) + "};\n"
    return text.replace("\r\n", "\n").replace("\n", nl)


# ---------------------------------------------------------------------------
# README.txt on the card
# ---------------------------------------------------------------------------
def card_readme(title, when, counts):
    return """CRYSTAL ORACLE - TF card contents - {title}
Built {when} by tools/build_assets.py (Crystal Oracle project).

Copy this "oracle" folder to the ROOT of the TF card (FAT32), so the card has
/oracle/clips, /oracle/voice ...

Updating a card that is already in use:
  * Copy over the top (merge). The station's counters and logs live in
    /oracle/state and /oracle/logs on the card; keep them.
  * When Windows asks about config.ini, choose SKIP: the card's copy may hold
    settings changed in the service menu. (Only a fresh card needs this one.)
  * Files removed from the build stay on the card. To drop old clips or
    sounds, delete /oracle/clips, voice, sfx and music on the card first.

  config.ini     station settings (year, volume, odds, prize stock, timing).
                 Edit with any text editor. The build creates it once and
                 never overwrites it; the service menu saves changes into it.
  captions.ini   every word on screen (rank / tier / bowl names, colours,
                 captions), the story features ([meter] the communal meter,
                 [fx] effects per clip) and, under [anchor], where the
                 narrator's glow sits in each clip. Generated from the
                 manifest - edit the manifest.
  clips/         visuals: <cue>.omj (animated, OMJ1 = MJPEG 800x480) or
                 <cue>.jpg (still). <cue>_<n> goes with voice variant n.
  voice/         voice lines <group>_<n>.wav  (WAV 48 kHz mono 16-bit)
  sfx/           sound effects  (same format)
  music/         music beds, looped  (same format)

Anything missing is fine: the station draws a built-in animation for any cue
without a clip and plays built-in chimes for missing sounds.

This build: {clips} clip(s), {voice} voice line(s), {sfx} sound effect(s), {music} music bed(s)
({placeholders} of them placeholders - test cards / synth sounds / computer voice).
""".format(title=title or "untitled", when=when, **counts).replace("\n", "\r\n")
