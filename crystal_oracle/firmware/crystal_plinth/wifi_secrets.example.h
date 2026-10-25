// Wi-Fi credentials for the plinth's Wi-Fi / WebSocket mode (PLINTH_LINK_WIFI 1 in crystal_plinth.ino).
// Copy this file to wifi_secrets.h (git-ignored) and fill it in. The ESP-NOW build never reads it.
#pragma once
#define WIFI_SSID       "your-wifi-name"
#define WIFI_PASS       "your-wifi-password"
#define PLINTH_HOSTNAME "dragonglow-plinth"   // -> ws://dragonglow-plinth.local:81/ (mDNS); the station app's default address
