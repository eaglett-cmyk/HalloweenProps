#pragma once
// ============================================================================
//  plinth_wifi.h -- the Wi-Fi / WebSocket transport of the crystal plinth
//  (PLINTH_LINK_WIFI 1 in crystal_plinth.ino). Included by the sketch AFTER its
//  types and globals; it is not a stand-alone library.
//
//  WHY: an iPad (or any browser) cannot speak ESP-NOW. With this transport the
//  plinth joins the house Wi-Fi as a station and runs a tiny WebSocket server
//  on port 81. The station web app (crystal_oracle/station/) connects to
//  ws://<plinth>:81/ and the SAME binary packets as the ESP-NOW link travel as
//  WebSocket binary messages (crystal_link.h structs, little-endian, unchanged):
//      plinth -> browser   HELLO (1 s heartbeat), TAG_PLACED, TAG_REMOVED,
//                          WRITE_RESULT, BUTTON
//      browser -> plinth   HELLO_ACK (on connect), PING (1 s), WRITE_REQ, LED
//  LINK-PROTOCOL.md still describes the packets; only the carrier differs:
//    * no discovery / adoption: a connected socket IS the link (up to
//      WS_MAX_CLIENTS browsers at once; every plinth->screen packet goes to all
//      of them, any of them may send screen->plinth packets);
//    * TCP is reliable, so TAG_PLACED is never resent;
//    * "standalone" = no packet from any browser for SCREEN_TIMEOUT_MS (the
//      browser PINGs every second), exactly like the radio version. When a
//      browser talks again a crystal still on the reader is re-read and
//      announced again (the sketch's existing wasStandalone logic).
//  A plain HTTP GET to http://<plinth>:81/ (no Upgrade header) answers a small
//  JSON status (with CORS), handy for testing from any browser.
//
//  Wi-Fi credentials live in wifi_secrets.h next to this file (git-ignored;
//  copy wifi_secrets.example.h). The plinth announces itself over mDNS as
//  <PLINTH_HOSTNAME>.local (default dragonglow-plinth.local) and prints its IP
//  on the serial console at boot and in every status line.
//
//  The WebSocket server is written here on top of WiFiServer / WiFiClient (RFC
//  6455, server side only): handshake (SHA-1 + base64 from mbedtls), masked
//  client frames, binary / ping / pong / close opcodes. No fragmentation (a
//  browser never fragments the 16..112-byte messages this link uses) and no
//  payload over WS_RX_MAX bytes: anything else closes the socket. No library
//  to install.
// ============================================================================
#include <WiFi.h>
#include <ESPmDNS.h>
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"

#if __has_include("wifi_secrets.h")
  #include "wifi_secrets.h"
#else
  #error "PLINTH_LINK_WIFI needs wifi_secrets.h next to the sketch: copy wifi_secrets.example.h and fill in WIFI_SSID / WIFI_PASS"
#endif
#ifndef PLINTH_HOSTNAME
  #define PLINTH_HOSTNAME "dragonglow-plinth"
#endif
// Optional second network (WIFI_SSID2 / WIFI_PASS2 in wifi_secrets.h, e.g. another house):
// while not connected the plinth alternates between the two every WIFI_RETRY_MS.
#if defined(WIFI_SSID2) && defined(WIFI_PASS2)
  #define WIFI_NETS 2
#else
  #define WIFI_NETS 1
#endif
#ifndef WS_PORT
  #define WS_PORT 81
#endif
#define WS_MAX_CLIENTS   2            // browsers connected at once (the iPad + a laptop watching)
#define WS_RX_MAX        512          // largest WebSocket payload accepted (WRITE_REQ is 112 bytes)
#define WS_HS_MAX        900          // largest HTTP upgrade request accepted
#define WS_HS_TIMEOUT_MS 3000         // a socket that hasn't finished its handshake by then is dropped
#define WS_RX_TIMEOUT_MS 6000         // an open socket silent this long (no PING) is dropped (TCP half-open)
#define WIFI_RETRY_MS    15000        // WiFi.begin() again if still not connected after this long

// ---- things the sketch defines later (same translation unit) ----
struct RxPacket;
static void logLine(const char *fmt, ...);
static void handlePacket(const RxPacket &p);

namespace plinth_wifi {

struct WsClient {
  WiFiClient sock;
  bool     used = false;
  bool     open = false;             // handshake finished: WebSocket frames from here on
  uint32_t sinceMs = 0, lastRxMs = 0;
  uint16_t hsLen = 0;
  char     hs[WS_HS_MAX + 1];
  uint16_t fLen = 0;                 // bytes of the frame being assembled
  uint8_t  fbuf[WS_RX_MAX + 14];
};

static WiFiServer s_server(WS_PORT);
static WsClient   s_cl[WS_MAX_CLIENTS];
static bool       s_serverUp = false;
static bool       s_mdnsUp = false;
static bool       s_wasConnected = false;
static uint32_t   s_lastBeginMs = 0;
static uint8_t    s_net = 0;                 // which network WiFi.begin() is trying (0 = WIFI_SSID, 1 = WIFI_SSID2)
static uint32_t   s_txFrames = 0, s_rxFrames = 0, s_accepted = 0, s_dropped = 0;
static const char *(*s_statusJson)() = nullptr;   // the sketch's status text for plain HTTP GETs

// ---------------------------------------------------------------- Wi-Fi
static const char *netSsid() {
#if WIFI_NETS > 1
  if (s_net == 1) return WIFI_SSID2;
#endif
  return WIFI_SSID;
}
static const char *netPass() {
#if WIFI_NETS > 1
  if (s_net == 1) return WIFI_PASS2;
#endif
  return WIFI_PASS;
}

static void joinNet() {
  WiFi.begin(netSsid(), netPass());
  s_lastBeginMs = millis();
  logLine("wifi: joining \"%s\" as %s ...", netSsid(), PLINTH_HOSTNAME);
}

static void wifiBegin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                       // answer PINGs / writes without modem-sleep latency
  WiFi.setHostname(PLINTH_HOSTNAME);
  WiFi.setAutoReconnect(true);
  joinNet();
}

static void onConnected() {
  logLine("wifi: connected to \"%s\", IP %s (RSSI %d dBm) - WebSocket on ws://%s:%d/ and ws://%s.local:%d/",
          netSsid(), WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str(), WS_PORT,
          PLINTH_HOSTNAME, WS_PORT);
  if (!s_serverUp) {
    s_server.begin();
    s_server.setNoDelay(true);
    s_serverUp = true;
  }
  if (!s_mdnsUp) {
    if (MDNS.begin(PLINTH_HOSTNAME)) {
      MDNS.addService("crystal-plinth", "tcp", WS_PORT);
      s_mdnsUp = true;
    } else {
      logLine("wifi: mDNS failed - use the IP address");
    }
  }
}

// ---------------------------------------------------------------- WebSocket frames
// One server->client frame (unmasked). Returns false if nothing was written.
static bool wsFrame(WsClient &c, uint8_t opcode, const uint8_t *data, size_t len) {
  if (!c.open || !c.sock.connected() || len > 65535) return false;
  uint8_t buf[WS_RX_MAX + 4];
  size_t n = 0;
  buf[n++] = (uint8_t)(0x80 | (opcode & 0x0F));
  if (len < 126) {
    buf[n++] = (uint8_t)len;
  } else {
    buf[n++] = 126;
    buf[n++] = (uint8_t)(len >> 8);
    buf[n++] = (uint8_t)len;
  }
  if (len > sizeof(buf) - n) return false;
  if (len) memcpy(buf + n, data, len);
  n += len;
  const size_t w = c.sock.write(buf, n);
  if (w == n) { s_txFrames++; return true; }
  return false;
}

static void closeClient(WsClient &c, const char *why) {
  if (c.used) {
    if (c.open) {
      const uint8_t code[2] = {0x03, 0xE8};   // 1000 normal closure
      wsFrame(c, 0x8, code, 2);
    }
    logLine("ws: client %s closed (%s)", c.sock.remoteIP().toString().c_str(), why);
  }
  c.sock.stop();
  c.used = false;
  c.open = false;
  c.hsLen = 0;
  c.fLen = 0;
}

// "Sec-WebSocket-Accept" for a client key: base64(sha1(key + GUID)).
static bool acceptKey(const char *key, size_t keyLen, char *out, size_t outLen) {
  static const char GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t cat[128];
  if (keyLen + sizeof(GUID) - 1 > sizeof(cat)) return false;
  memcpy(cat, key, keyLen);
  memcpy(cat + keyLen, GUID, sizeof(GUID) - 1);
  uint8_t sha[20];
  if (mbedtls_sha1(cat, keyLen + sizeof(GUID) - 1, sha) != 0) return false;
  size_t olen = 0;
  if (mbedtls_base64_encode((unsigned char *)out, outLen, &olen, sha, sizeof(sha)) != 0) return false;
  out[olen < outLen ? olen : outLen - 1] = 0;
  return true;
}

// Finds an HTTP header value (case-insensitive name) in the request text. Returns its length.
static size_t headerValue(const char *req, const char *name, const char **val) {
  const size_t nl = strlen(name);
  const char *p = req;
  while ((p = strchr(p, '\n')) != nullptr) {
    p++;
    if (strncasecmp(p, name, nl) == 0 && p[nl] == ':') {
      const char *v = p + nl + 1;
      while (*v == ' ' || *v == '\t') v++;
      const char *e = v;
      while (*e && *e != '\r' && *e != '\n') e++;
      while (e > v && (e[-1] == ' ' || e[-1] == '\t')) e--;
      *val = v;
      return (size_t)(e - v);
    }
  }
  return 0;
}

static void finishHandshake(WsClient &c) {
  c.hs[c.hsLen] = 0;
  const char *key = nullptr, *upg = nullptr;
  const size_t keyLen = headerValue(c.hs, "Sec-WebSocket-Key", &key);
  const size_t upgLen = headerValue(c.hs, "Upgrade", &upg);
  const bool wantsWs = upgLen == 9 && strncasecmp(upg, "websocket", 9) == 0 && keyLen >= 16 && keyLen <= 64;
  if (!wantsWs) {
    // A plain HTTP request: answer a small JSON status and close (for testing / the station app).
    const char *body = s_statusJson ? s_statusJson() : "{\"ok\":true}";
    char head[200];
    const int hl = snprintf(head, sizeof(head),
                            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n"
                            "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: %u\r\n\r\n",
                            (unsigned)strlen(body));
    c.sock.write((const uint8_t *)head, (size_t)hl);
    c.sock.write((const uint8_t *)body, strlen(body));
    c.sock.flush();
    closeClient(c, "http status served");
    return;
  }
  char acc[40];
  if (!acceptKey(key, keyLen, acc, sizeof(acc))) { closeClient(c, "bad key"); return; }
  char resp[200];
  const int rl = snprintf(resp, sizeof(resp),
                          "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                          "Sec-WebSocket-Accept: %s\r\n\r\n", acc);
  if (c.sock.write((const uint8_t *)resp, (size_t)rl) != (size_t)rl) { closeClient(c, "handshake write failed"); return; }
  c.open = true;
  c.fLen = 0;
  c.lastRxMs = millis();
  s_accepted++;
  logLine("ws: client %s connected (%u so far)", c.sock.remoteIP().toString().c_str(), (unsigned)s_accepted);
}

// Parses complete frames out of c.fbuf. Returns false if the socket was closed.
static bool processFrames(WsClient &c) {
  for (;;) {
    if (c.fLen < 2) return true;
    const uint8_t b0 = c.fbuf[0], b1 = c.fbuf[1];
    const bool fin = (b0 & 0x80) != 0;
    const uint8_t opcode = b0 & 0x0F;
    const bool masked = (b1 & 0x80) != 0;
    size_t plen = b1 & 0x7F;
    size_t hdr = 2;
    if (plen == 126) {
      if (c.fLen < 4) return true;
      plen = ((size_t)c.fbuf[2] << 8) | c.fbuf[3];
      hdr = 4;
    } else if (plen == 127) {
      closeClient(c, "64-bit frame length");
      return false;
    }
    if (plen > WS_RX_MAX) { closeClient(c, "frame too large"); return false; }
    if (masked) hdr += 4;
    const size_t total = hdr + plen;
    if (c.fLen < total) return true;             // wait for the rest
    uint8_t *payload = c.fbuf + hdr;
    if (masked) {
      const uint8_t *mk = c.fbuf + hdr - 4;
      for (size_t i = 0; i < plen; i++) payload[i] ^= mk[i & 3];
    }
    if (!fin && opcode != 0) { closeClient(c, "fragmented frame"); return false; }
    s_rxFrames++;
    c.lastRxMs = millis();
    switch (opcode) {
      case 0x2: {                                // binary: one link packet
        if (plen >= sizeof(LinkHeader) && plen <= ESP_NOW_MAX_DATA_LEN) {
          RxPacket p;
          memset(p.mac, 0, sizeof(p.mac));
          p.mac[5] = (uint8_t)(&c - s_cl);       // which socket (for logs only)
          p.len = (uint8_t)plen;
          memcpy(p.data, payload, plen);
          handlePacket(p);
        }
        break;
      }
      case 0x1:                                  // text: ignored (the app only sends binary)
        break;
      case 0x8:                                  // close
        closeClient(c, "closed by the browser");
        return false;
      case 0x9:                                  // ping -> pong with the same payload
        wsFrame(c, 0xA, payload, plen);
        break;
      case 0xA:                                  // pong
        break;
      default:
        closeClient(c, "unknown opcode");
        return false;
    }
    const size_t rest = c.fLen - total;
    if (rest) memmove(c.fbuf, c.fbuf + total, rest);
    c.fLen = (uint16_t)rest;
  }
}

static void pumpClient(WsClient &c, uint32_t now) {
  (void)now;                                     // the caller's `now` predates this pass's reads: use a fresh clock
  if (!c.used) return;
  if (!c.sock.connected()) { closeClient(c, "connection lost"); return; }
  if (!c.open) {
    // handshake: collect the request until the blank line
    while (c.sock.available() > 0 && c.hsLen < WS_HS_MAX) {
      c.hs[c.hsLen++] = (char)c.sock.read();
      if (c.hsLen >= 4 && memcmp(c.hs + c.hsLen - 4, "\r\n\r\n", 4) == 0) { finishHandshake(c); return; }
    }
    if (c.hsLen >= WS_HS_MAX) { closeClient(c, "request too long"); return; }
    if ((int32_t)(millis() - c.sinceMs) > WS_HS_TIMEOUT_MS) { closeClient(c, "handshake timeout"); return; }
    return;
  }
  int budget = 2048;                             // bytes per loop pass
  while (budget-- > 0 && c.sock.available() > 0) {
    if (c.fLen >= sizeof(c.fbuf)) { closeClient(c, "frame buffer overflow"); return; }
    c.fbuf[c.fLen++] = (uint8_t)c.sock.read();
    if (!processFrames(c)) return;
  }
  // signed: lastRxMs may be a hair NEWER than any clock the caller took (the 2026-10-24 bug:
  // an unsigned `now - lastRxMs` wrapped and every browser was dropped 2 ms after connecting)
  if ((int32_t)(millis() - c.lastRxMs) > WS_RX_TIMEOUT_MS) { closeClient(c, "silent too long"); return; }
}

// ---------------------------------------------------------------- the sketch's link API
static void begin(const char *(*statusJson)()) {
  s_statusJson = statusJson;
  wifiBegin();
}

// How many browsers hold an open WebSocket right now.
static uint8_t clients() {
  uint8_t n = 0;
  for (WsClient &c : s_cl) if (c.used && c.open) n++;
  return n;
}

// Sends one link packet to every connected browser. true if at least one got it.
static bool send(const void *pkt, size_t len) {
  bool any = false;
  for (WsClient &c : s_cl)
    if (c.used && c.open && wsFrame(c, 0x2, (const uint8_t *)pkt, len)) any = true;
  return any;
}

static void service(uint32_t now) {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != s_wasConnected) {
    s_wasConnected = connected;
    if (connected) onConnected();
    else logLine("wifi: connection lost - reconnecting");
  }
  if (!connected) {
    if (now - s_lastBeginMs >= WIFI_RETRY_MS) {      // autoReconnect usually does it; this is the belt
      WiFi.disconnect();
      s_net = (uint8_t)((s_net + 1) % WIFI_NETS);    // not found here: try the other network
      joinNet();
    }
    for (WsClient &c : s_cl) if (c.used) closeClient(c, "wifi down");
    return;
  }
  if (!s_serverUp) return;
  // new connections (the oldest socket is replaced when all slots are taken)
  while (s_server.hasClient()) {
    WiFiClient nc = s_server.accept();
    if (!nc) break;
    WsClient *slot = nullptr;
    for (WsClient &c : s_cl) if (!c.used) { slot = &c; break; }
    if (!slot) {
      slot = &s_cl[0];
      for (WsClient &c : s_cl) if (c.sinceMs < slot->sinceMs) slot = &c;
      closeClient(*slot, "replaced by a new connection");
      s_dropped++;
    }
    nc.setNoDelay(true);
    slot->sock = nc;
    slot->used = true;
    slot->open = false;
    slot->sinceMs = now;
    slot->lastRxMs = now;
    slot->hsLen = 0;
    slot->fLen = 0;
  }
  for (WsClient &c : s_cl) pumpClient(c, now);
}

static const char *ip() {
  static char buf[20];
  if (WiFi.status() != WL_CONNECTED) return "-";
  strlcpy(buf, WiFi.localIP().toString().c_str(), sizeof(buf));
  return buf;
}

static bool connected() { return WiFi.status() == WL_CONNECTED; }
static uint32_t txFrames() { return s_txFrames; }
static uint32_t rxFrames() { return s_rxFrames; }

}  // namespace plinth_wifi
