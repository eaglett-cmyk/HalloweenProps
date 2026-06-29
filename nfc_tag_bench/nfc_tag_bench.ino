/*
 * ============================================================================
 *  NFC TAG BENCH  —  Skull Oracle test rig  (v2: writes the prop's real format)
 *  Board : ESP32 (AITRIP 30-pin WROOM-32, Amazon B0CR5Y2JVD)
 *          Arduino IDE board = "ESP32 Dev Module"  (Espressif esp32 core)
 *  NFC   : PN532 (HiLetgo V3, Amazon B01I1J17LC) in I2C mode
 *          -> set the module's DIP switches: SW1 ON, SW2 OFF
 * ----------------------------------------------------------------------------
 *  WHAT IT DOES
 *    The ESP32 joins your WiFi and serves a web page. Hold an NTAG token on the
 *    PN532 and you can READ it and WRITE it *exactly the way the prop does* --
 *    NDEF link, "SK" marker, per-year visit slots -- PLUS the new "charge"
 *    value. Charge is a per-token modifier the prop ADDS to the drawn number.
 *
 *    Use it to set a charge on a token, then tap that token on the prop and
 *    watch the roll come out higher: that is the feature under test.
 *
 *    >>> The TAG LAYOUT below is kept byte-for-byte identical to
 *        ../skull_oracle/skull_oracle.ino  (schema 0x03). Change both together.
 *
 *  WIRING   (PN532 in I2C mode  ->  ESP32 30-pin)
 *  --------------------------------------------------------------------------
 *    All six wires land on the 3V3 (right) side of the 30-pin board:
 *    PN532 VCC  -> 3V3          (top-right; keeps the I2C lines at 3.3 V)
 *    PN532 GND  -> GND          (right side, next to 3V3)
 *    PN532 SDA  -> GPIO21  (SDA)
 *    PN532 SCL  -> GPIO22  (SCL)
 *    PN532 RSTO -> GPIO17        (reset)
 *    PN532 IRQ  -> GPIO16        (interrupt)
 *    DIP switches on the PN532:  SW1 ON, SW2 OFF  = I2C
 *
 *  LIBRARIES (Arduino Library Manager): "Adafruit PN532" (pulls in
 *    "Adafruit BusIO"). WiFi / WebServer / ESPmDNS / Wire ship with the core.
 *
 *  WIFI: credentials live in secrets.h (gitignored). Copy secrets.h.example to
 *    secrets.h and fill in WIFI_SSID / WIFI_PASS. The board joins that network;
 *    open the Serial Monitor @115200 for its IP, or browse skullbench.local.
 *    If it can't join in 15 s it falls back to an access point "SkullBench".
 *  ============================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <SPI.h>
#include <string.h>
#include <Adafruit_PN532.h>
#include "secrets.h"            // defines WIFI_SSID and WIFI_PASS

// ------------------------------- config (TUNE) -----------------------------
#define PIN_SDA   21      // all PN532 pins are on the 3V3 (right) side of the board
#define PIN_SCL   22
#define PIN_IRQ   16      // (avoids GPIO18/19 = the thermocouple board's SPI pins)
#define PIN_RST   17

#define HOSTNAME  "skullbench"  // -> http://skullbench.local
#define AP_SSID   "SkullBench"  // fallback access point if WiFi won't join
#define AP_PASS   "oracle1234"  // >= 8 chars

// --- tag layout: MUST MATCH ../skull_oracle/skull_oracle.ino (schema 0x03) ---
#define REPO_PATH       "github.com/eaglett-cmyk/HalloweenProps"
#define NDEF_START_PAGE 4       // pages 4..15 hold the link
#define MARKER_PAGE     16      // 'S','K',schema,visits
#define CHARGE_PAGE     17      // [charge,0,0,0]  per-token roll modifier
#define YEAR_BASE_PAGE  18      // slot for BASE_YEAR; +1 per year after
#define MAX_YEARS       22      // pages 18..39
#define MAGIC0          0x53    // 'S'
#define MAGIC1          0x4B    // 'K'
#define SCHEMA_VER      0x03
#define BASE_YEAR       2024
#define NUMBER_MAX      100

Adafruit_PN532 nfc(PIN_IRQ, PIN_RST);   // I2C mode (uses Wire)
WebServer server(80);

// ------------------------------- the web UI --------------------------------
static const char PAGE_HTML[] PROGMEM = R"HTMLPAGE(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Skull Oracle - NFC Bench</title>
<style>
 :root{--bg:#0f172a;--card:#1e293b;--ink:#e2e8f0;--mut:#94a3b8;--acc:#f97316;--ok:#22c55e;--bad:#ef4444;--line:#334155}
 *{box-sizing:border-box}
 body{margin:0;font:15px/1.5 system-ui,Arial,sans-serif;background:var(--bg);color:var(--ink)}
 .wrap{max-width:680px;margin:0 auto;padding:16px}
 h1{font-size:20px;margin:.2em 0}
 .sub{color:var(--mut);font-size:13px;margin-bottom:14px}
 .card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin:12px 0}
 .row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
 label{font-size:13px;color:var(--mut);display:block;margin-bottom:4px}
 input,select{background:#0b1220;color:var(--ink);border:1px solid var(--line);border-radius:8px;padding:8px;font-size:15px;width:100%}
 .f{flex:1;min-width:92px}
 button{background:var(--acc);color:#1a1206;border:0;border-radius:8px;padding:9px 14px;font-size:14px;font-weight:600;cursor:pointer}
 button.ghost{background:#0b1220;color:var(--ink);border:1px solid var(--line)}
 button.danger{background:var(--bad);color:#fff}
 .pill{display:inline-block;padding:2px 10px;border-radius:999px;font-size:12px;font-weight:600;background:#334155}
 .big{font-size:26px;font-weight:700}
 .acc{color:var(--acc)}
 .kv{display:flex;justify-content:space-between;border-bottom:1px dashed var(--line);padding:6px 0}
 .kv:last-child{border:0}
 table{width:100%;border-collapse:collapse;font-size:14px}
 td,th{text-align:left;padding:4px 6px;border-bottom:1px solid var(--line)}
 .muted{color:var(--mut)}
 #msg{min-height:18px;font-size:13px}
</style></head>
<body><div class="wrap">
 <h1>&#127875; Skull Oracle &mdash; NFC Bench</h1>
 <div class="sub">writes the prop's real tag format + the charge modifier &middot; hold a tag on the reader</div>

 <div class="card">
   <div class="row" style="justify-content:space-between">
     <span id="dot" class="pill">no tag</span>
     <span class="muted" id="uid">&mdash;</span>
   </div>
   <div style="margin-top:10px">
     <div class="kv"><span>Charge (roll bonus)</span><span class="big acc" id="charge">&mdash;</span></div>
     <div class="kv"><span>Visits</span><span id="visits">&mdash;</span></div>
     <div class="kv"><span>Phone link (NDEF)</span><span id="link" class="muted">&mdash;</span></div>
     <div class="kv"><span>Schema</span><span id="schema" class="muted">&mdash;</span></div>
   </div>
   <div style="margin-top:10px">
     <div class="muted" style="font-size:12px;margin-bottom:4px">Visit history</div>
     <table id="hist"><tbody><tr><td class="muted">&mdash;</td></tr></tbody></table>
   </div>
 </div>

 <div class="card">
   <label>Charge &mdash; added to the prop's drawn number (0&ndash;255)</label>
   <div class="row">
     <input class="f" type="number" id="chargeIn" min="0" max="255" value="20">
     <button onclick="setCharge()">Set charge</button>
     <button class="ghost" onclick="bump(10)">+10</button>
     <button class="ghost" onclick="bump(-10)">&minus;10</button>
   </div>
 </div>

 <div class="card">
   <label>Log a visit &mdash; seed a PAST year (leave the prop's EVENT_YEAR unplayed to test the roll)</label>
   <div class="row">
     <div class="f"><label>Year</label><input type="number" id="year" min="2024" max="2045" value="2025"></div>
     <div class="f"><label>Number (1&ndash;100)</label><input type="number" id="num" min="1" max="100" value="42"></div>
     <div class="f"><label>Prize</label><select id="prize">
       <option value="0">0 &mdash; trick</option><option value="1">1 &mdash; treat</option>
       <option value="2">2 &mdash; big win</option><option value="3">3 &mdash; jackpot</option></select></div>
   </div>
   <div class="row" style="margin-top:10px"><button onclick="logVisit()">Log visit</button></div>
 </div>

 <div class="card">
   <div class="row" style="justify-content:space-between">
     <button class="ghost" onclick="provision()">Provision fresh tag</button>
     <button class="danger" onclick="fmt()">Erase / format</button>
   </div>
   <div class="muted" style="font-size:12px;margin-top:8px">
     Provision = write the NDEF link + a blank "SK" marker (a brand-new prop tag).
   </div>
 </div>

 <div id="msg" class="muted"></div>
</div>
<script>
 const $=id=>document.getElementById(id), D="—";
 function pname(p){return ['trick','treat','big win','jackpot'][p]||'?';}
 async function poll(){
   try{
     const d=await (await fetch('/api/read')).json();
     if(!d.present){ set('no tag','#334155'); $('uid').textContent=D;
       blank(); return; }
     $('uid').textContent=d.uid;
     $('link').textContent = d.link ? 'yes' : 'no';
     if(d.ours){ set('prop tag','#15803d');
       $('charge').textContent=d.charge; $('visits').textContent=d.visits;
       $('schema').textContent='0x'+(d.schema||0).toString(16);
       let h='<tbody>';
       if(d.history&&d.history.length){ for(const e of d.history)
         h+=`<tr><td>${e.year}</td><td>No. ${e.number}</td><td class="muted">${pname(e.prize)}</td></tr>`; }
       else h+='<tr><td class="muted">no visits yet</td></tr>';
       $('hist').innerHTML=h+'</tbody>';
     } else { set('blank / foreign','#b45309'); blank();
       $('hist').innerHTML='<tbody><tr><td class="muted">not a prop tag &mdash; Provision, Set charge, or Log visit to initialize</td></tr></tbody>'; }
   }catch(e){}
 }
 function blank(){ $('charge').textContent=D; $('visits').textContent=D; $('schema').textContent=D;
   $('hist').innerHTML='<tbody><tr><td class="muted">'+D+'</td></tr></tbody>'; }
 function set(t,c){ $('dot').textContent=t; $('dot').style.background=c; }
 async function call(url){ $('msg').textContent='…';
   try{ const d=await (await fetch(url)).json();
     $('msg').textContent = d.ok ? '✓ written' : ('✗ '+(d.err||'failed')); }
   catch(e){ $('msg').textContent='✗ network'; }
   await poll(); }
 function setCharge(){ call('/api/charge?value='+(+$('chargeIn').value||0)); }
 function bump(n){ let v=(parseInt($('charge').textContent)||0)+n; if(v<0)v=0; if(v>255)v=255;
   $('chargeIn').value=v; call('/api/charge?value='+v); }
 function logVisit(){ call(`/api/log?year=${+$('year').value}&number=${+$('num').value}&prize=${+$('prize').value}`); }
 function provision(){ call('/api/provision'); }
 function fmt(){ if(confirm('Erase this tag (clears user pages 4-39)?')) call('/api/format'); }
 setInterval(poll,1500); poll();
</script></body></html>
)HTMLPAGE";

// ------------------------------- NFC helpers -------------------------------
bool rdPage(uint8_t p, uint8_t* b){ return nfc.mifareultralight_ReadPage(p, b); }
bool wrPage(uint8_t p, uint8_t* b){ return nfc.mifareultralight_WritePage(p, b); }

bool selectTag(uint16_t timeout) {
  uint8_t uid[7], len;
  return nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len, timeout);
}

// Writes the CC + a single NDEF URI record for REPO_PATH into pages 3..15,
// byte-for-byte the same as the prop's writeNdefUrl().
void writeNdefUrl() {
  uint8_t cc[4] = { 0xE1, 0x10, 0x12, 0x00 };   // NTAG203 CC: NDEF, 144 bytes
  wrPage(3, cc);

  uint8_t buf[48];
  memset(buf, 0, sizeof(buf));
  uint8_t urlLen     = strlen(REPO_PATH);
  uint8_t payloadLen = urlLen + 1;               // + URI prefix byte
  uint8_t i = 0;
  buf[i++] = 0x03;                 // NDEF Message TLV
  buf[i++] = payloadLen + 4;       // TLV length (record header 4 + payload)
  buf[i++] = 0xD1;                 // record header: MB|ME|SR, TNF=well-known
  buf[i++] = 0x01;                 // type length
  buf[i++] = payloadLen;           // payload length
  buf[i++] = 0x55;                 // type 'U' (URI)
  buf[i++] = 0x04;                 // URI prefix code: https://
  memcpy(&buf[i], REPO_PATH, urlLen);
  i += urlLen;
  buf[i++] = 0xFE;                 // Terminator TLV
  for (uint8_t p = 0; p < 12; p++) wrPage(NDEF_START_PAGE + p, &buf[p * 4]);
}

// Make sure the tag is a provisioned prop tag (marker present). Mirrors the
// prop: a fresh tag gets the NDEF link + a blank marker + charge 0.
bool ensureProvisioned(uint8_t* visitsOut) {
  uint8_t m[4];
  if (rdPage(MARKER_PAGE, m) && m[0] == MAGIC0 && m[1] == MAGIC1) {
    if (visitsOut) *visitsOut = m[3];
    return true;
  }
  writeNdefUrl();
  uint8_t zero[4] = {0,0,0,0};
  wrPage(CHARGE_PAGE, zero);
  uint8_t fresh[4] = { MAGIC0, MAGIC1, SCHEMA_VER, 0 };
  bool ok = wrPage(MARKER_PAGE, fresh);
  if (visitsOut) *visitsOut = 0;
  return ok;
}

// ------------------------------- HTTP handlers -----------------------------
void sendJson(const String& s) { server.send(200, "application/json", s); }
void handleRoot() { server.send_P(200, "text/html", PAGE_HTML); }

void handleRead() {
  uint8_t uid[7], ulen;
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &ulen, 150)) {
    sendJson("{\"present\":false}");
    return;
  }
  String j = "{\"present\":true,\"uid\":\"";
  for (uint8_t i = 0; i < ulen; i++) {
    char t[3]; sprintf(t, "%02X", uid[i]); j += t;
    if (i + 1 < ulen) j += ":";
  }
  j += "\"";

  uint8_t p4[4];
  bool link = rdPage(NDEF_START_PAGE, p4) && p4[0] == 0x03;   // NDEF TLV present?
  j += ",\"link\":"; j += link ? "true" : "false";

  uint8_t m[4];
  bool ours = rdPage(MARKER_PAGE, m) && m[0] == MAGIC0 && m[1] == MAGIC1;
  j += ",\"ours\":"; j += ours ? "true" : "false";

  if (ours) {
    uint8_t visits = m[3];
    uint8_t c[4] = {0,0,0,0}; rdPage(CHARGE_PAGE, c);
    j += ",\"schema\":" + String(m[2]);
    j += ",\"visits\":" + String(visits);
    j += ",\"charge\":" + String(c[0]);
    j += ",\"history\":[";
    bool first = true;
    for (uint8_t y = 0; y < MAX_YEARS; y++) {
      uint8_t s[4];
      if (rdPage(YEAR_BASE_PAGE + y, s) && s[0] != 0) {
        if (!first) j += ",";
        first = false;
        j += "{\"year\":"    + String(BASE_YEAR + y) +
             ",\"number\":"  + String(s[0]) +
             ",\"prize\":"   + String(s[1]) + "}";
      }
    }
    j += "]";
  }
  j += "}";
  sendJson(j);
}

void handleProvision() {
  if (!selectTag(300)) { sendJson("{\"ok\":false,\"err\":\"no tag\"}"); return; }
  writeNdefUrl();
  uint8_t zero[4] = {0,0,0,0};
  wrPage(CHARGE_PAGE, zero);
  uint8_t m[4] = { MAGIC0, MAGIC1, SCHEMA_VER, 0 };
  bool ok = wrPage(MARKER_PAGE, m);
  sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"write\"}");
}

void handleCharge() {
  long v = server.arg("value").toInt();
  if (v < 0) v = 0; if (v > 255) v = 255;
  if (!selectTag(300)) { sendJson("{\"ok\":false,\"err\":\"no tag\"}"); return; }
  ensureProvisioned(nullptr);
  uint8_t c[4] = { (uint8_t)v, 0, 0, 0 };
  bool ok = wrPage(CHARGE_PAGE, c);
  sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"write\"}");
}

void handleLog() {
  int year   = server.arg("year").toInt();
  int number = server.arg("number").toInt();
  int prize  = server.arg("prize").toInt();
  int idx = year - BASE_YEAR;
  if (idx < 0 || idx >= MAX_YEARS) { sendJson("{\"ok\":false,\"err\":\"year out of range\"}"); return; }
  if (number < 1) number = 1; if (number > NUMBER_MAX) number = NUMBER_MAX;
  if (prize  < 0) prize  = 0; if (prize  > 3) prize = 3;

  if (!selectTag(300)) { sendJson("{\"ok\":false,\"err\":\"no tag\"}"); return; }
  ensureProvisioned(nullptr);

  uint8_t slotPage = YEAR_BASE_PAGE + idx;
  uint8_t old[4] = {0,0,0,0}; rdPage(slotPage, old);
  bool wasEmpty = (old[0] == 0);

  uint8_t s[4] = { (uint8_t)number, (uint8_t)prize, 0, 0 };
  bool ok = wrPage(slotPage, s);

  if (ok && wasEmpty) {                         // a new year -> bump the count
    uint8_t m[4];
    if (!(rdPage(MARKER_PAGE, m) && m[0] == MAGIC0 && m[1] == MAGIC1)) {
      m[0] = MAGIC0; m[1] = MAGIC1; m[2] = SCHEMA_VER; m[3] = 0;
    }
    if (m[3] < 255) m[3]++;
    ok = wrPage(MARKER_PAGE, m);                // commit count last
  }
  sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"write\"}");
}

void handleFormat() {
  if (!selectTag(300)) { sendJson("{\"ok\":false,\"err\":\"no tag\"}"); return; }
  uint8_t zero[4] = {0,0,0,0};
  bool ok = true;
  for (uint8_t p = NDEF_START_PAGE; p <= YEAR_BASE_PAGE + MAX_YEARS - 1; p++) ok &= wrPage(p, zero);
  sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"write\"}");
}

// ----------------------------------- setup ---------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\nNFC Tag Bench v2"));

  Wire.begin(PIN_SDA, PIN_SCL);
  nfc.begin();
  uint32_t ver = nfc.getFirmwareVersion();
  if (!ver) Serial.println(F("PN532 NOT found - check I2C wiring + DIP switches (SW1 ON / SW2 OFF)"));
  else { nfc.SAMConfig(); Serial.print(F("PN532 ready, firmware 0x")); Serial.println(ver, HEX); }

  // Join WiFi (station). Fall back to an access point if it won't connect.
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print(F("Joining WiFi \"")); Serial.print(WIFI_SSID); Serial.print(F("\" "));
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(250); Serial.print('.'); }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Connected. IP: http://")); Serial.println(WiFi.localIP());
    if (MDNS.begin(HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.println(F("Also reachable at: http://" HOSTNAME ".local"));
    }
  } else {
    Serial.println(F("WiFi join failed -> starting access point fallback."));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print(F("AP \"")); Serial.print(AP_SSID);
    Serial.print(F("\" -> http://")); Serial.println(WiFi.softAPIP());
  }

  server.on("/",              handleRoot);
  server.on("/api/read",      handleRead);
  server.on("/api/provision", handleProvision);
  server.on("/api/charge",    handleCharge);
  server.on("/api/log",       handleLog);
  server.on("/api/format",    handleFormat);
  server.begin();
  Serial.println(F("Web server up."));
}

void loop() {
  server.handleClient();
}
