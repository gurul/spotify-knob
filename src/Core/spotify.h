/*-------------------------------------------------------------------------------------------------
**
** spotify.h
**
**    Utility declarations for Spotify integration. This includes
**    OAuth authentication, certificate setup, and serving a local web
**    interface to acquire an auth code via redirect URI.
**
** SPDX-FileCopyrightText: 2025 ThingPulse Ltd., https://thingpulse.com
** SPDX-License-Identifier: MIT
**
** ------------------------------------------------------------------------------------------------
** Change Log:
**    2024-12-27 - Electric Diversions - Copied and renamed to tpSpotify.h from spotify.h
**    2025-05-04 - Electric Diversions - Renamed back to spotify.h and moved to the Core folder
**    2025-06-07 - Electric Diversions - Refactor to scope SpotifyArduino instance to SpotifyPlayer
** ------------------------------------------------------------------------------------------------
*/

#pragma once

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <SpotifyArduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "Vault.h"

extern const char *spotify_server_cert;
extern const char *spotify_image_server_cert;

/*
** ===================================================================
** Root CAs for the shared `client` below. It talks to TWO hosts:
**
**   api.spotify.com       -> DigiCert Global Root G2
**   accounts.spotify.com  -> Certainly Intermediate R1
**                            -> Starfield Root Certificate Authority - G2
**
** Starfield Root G2 — ADDED 2026-09-23. accounts.spotify.com moved to the
** "Certainly" chain. With G2 alone, every access-token refresh failed:
**   (-9984) X509 - Certificate verification failed
** so the knob sat on "Waiting for music" with the API itself healthy.
** Source: macOS SystemRootCertificates keychain.
**   SHA256 2C:E1:CB:0B:F9:D2:F9:E1:02:99:3F:BE:21:51:52:C3:
**          B2:DD:0C:AB:DE:1C:68:E5:31:9B:83:91:54:DB:B7:F5
**   Valid until 2037-12-31.
**
** Verified 2026-09-23 with the system store switched off
** (openssl s_client -no-CApath -no-CAstore -CAfile <file>):
**   G2 alone        api: 0 (ok)   accounts: 20 (unable to get issuer)
**   this bundle     api: 0 (ok)   accounts: 0 (ok)
**
** Keep BOTH. Spotify rotates CAs per host; a bundle is the point. The
** album-art CDN has its own bundle (gCombinedCerts, SpotifyArtMgr.cpp).
** ===================================================================
*/
const char *spotify_api_root_certs = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIID3TCCAsWgAwIBAgIBADANBgkqhkiG9w0BAQsFADCBjzELMAkGA1UEBhMCVVMx
EDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNjb3R0c2RhbGUxJTAjBgNVBAoT
HFN0YXJmaWVsZCBUZWNobm9sb2dpZXMsIEluYy4xMjAwBgNVBAMTKVN0YXJmaWVs
ZCBSb290IENlcnRpZmljYXRlIEF1dGhvcml0eSAtIEcyMB4XDTA5MDkwMTAwMDAw
MFoXDTM3MTIzMTIzNTk1OVowgY8xCzAJBgNVBAYTAlVTMRAwDgYDVQQIEwdBcml6
b25hMRMwEQYDVQQHEwpTY290dHNkYWxlMSUwIwYDVQQKExxTdGFyZmllbGQgVGVj
aG5vbG9naWVzLCBJbmMuMTIwMAYDVQQDEylTdGFyZmllbGQgUm9vdCBDZXJ0aWZp
Y2F0ZSBBdXRob3JpdHkgLSBHMjCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoC
ggEBAL3twQP89o/8ArFvW59I2Z154qK3A2FWGMNHttfKPTUuiUP3oWmb3ooa/RMg
nLRJdzIpVv257IzdIvpy3Cdhl+72WoTsbhm5iSzchFvVdPtrX8WJpRBSiUZV9Lh1
HOZ/5FSuS/hVclcCGfgXcVnrHigHdMWdSL5stPSksPNkN3mSwOxGXn/hbVNMYq/N
Hwtjuzqd+/x5AJhhdM8mgkBj87JyahkNmcrUDnXMN/uLicFZ8WJ/X7NfZTD4p7dN
dloedl40wOiWVpmKs/B/pM293DIxfJHP4F8R+GuqSVzRmZTRouNjWwl2tVZi4Ut0
HZbUJtQIBFnQmA4O5t78w+wfkPECAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAO
BgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYEFHwMMh+n2TB/xH1oo2Kooc6rB1snMA0G
CSqGSIb3DQEBCwUAA4IBAQARWfolTwNvlJk7mh+ChTnUdgWUXuEok21iXQnCoKjU
sHU48TRqneSfioYmUeYs0cYtbpUgSpIB7LiKZ3sx4mcujJUDJi5DnUox9g61DLu3
4jd/IroAow57UvtruzvE03lRTs2Q9GcHGcg8RnoNAX3FWOdt5oUwF5okxBDgBPfg
8n/Uqgr/Qh037ZTlZFkSIHc40zI+OIF1lnP6aI+xy84fxez6nH7PfrHxBy22/L/K
pL/QlwVKvOoYKAKQvVR4CSFx09F9HdkWsKlhPdAKACL8x3vLCWRFCztAgfd9fDL1
mMpYjn0q7pBZc2T5NnReJaH1ZgUufzkVqSr7UIuOhWn0
-----END CERTIFICATE-----
)EOF";

// Use http://<value-configured-here>.local/callback/ as the redirect URI for the app on Spotify.
// Hence, the default URI is http://tp-spotify.local/callback/.
// If you change the value here, you need to modify the redirect URI on Spotify as well.
#define SPOTIFY_ESPOTIFIER_NODE_NAME "tp-spotify"

#define SPOTIFY_REFRESH_TOKEN_FILE_NAME "/refresh-token.txt"
// the '/callback/' path is essential as spotify.h#fetchSpotifyAuthCode() registers a handler for it
#define SPOTIFY_REDIRECT_URI "http%3A%2F%2F" SPOTIFY_ESPOTIFIER_NODE_NAME ".local%2Fcallback%2F"

String authCode = "";
String scope    = "user-read-playback-state%20user-modify-playback-state";
WebServer server(80);
WiFiClientSecure client;

// Note: SpotifyArduino does not initialize its _refreshToken pointer.
// When declared as a global, this works because globals are zero-initialized by default,
// making _refreshToken safely nullptr. If the instance is created dynamically or locally,
// this assumption can lead to heap corruption when delete is called on an uninitialized
// _refreshToken in setRefreshToken().
SpotifyArduino spotify(client);

const char *webpageTemplate =
    R"(
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <meta http-equiv="X-UA-Compatible" content="IE=edge">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no" />
  </head>
  <body>
    <div>
     <a href="https://accounts.spotify.com/authorize?client_id=%s&response_type=code&redirect_uri=%s&scope=%s">Click</a> to load Spotify authentication code
    </div>
  </body>
</html>
)";

void handleCallback() {
  log_i("###### handleCallback().");
  String code = "";
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "code") {
      authCode = server.arg(i);
    }
  }

  if (authCode == "") {
    server.send(404, "text/plain", "Failed to fetch Spotify authentication code, check serial monitor. Maybe go back in browser history and try again.");
  } else {
    server.send(200, "text/plain", "Succesfully fetched Spotify authentication code. Follow instructions on device.");
  }
}

void handleFavicon() {
  log_i("*** Entering handleFavicon()");
  server.send(200, "image/vnd.microsoft.icon", "00000100");
}

void handleNotFound() {
  log_i("*** Entering handleNotFound()");
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  log_e("%s", message.c_str());
  server.send(404, "text/plain", message);
}

void printRootWebpage() {
  char webpage[800];
  sprintf(webpage, webpageTemplate, Vault::getInstance().getSpotifyClientID().c_str(), SPOTIFY_REDIRECT_URI, scope.c_str());
  log_i("webpage: '%s",webpage);
}

void handleRoot() {
  log_i("*** Entering handleRoot()");
  char webpage[800];
  sprintf(webpage, webpageTemplate, Vault::getInstance().getSpotifyClientID().c_str(), SPOTIFY_REDIRECT_URI, scope.c_str());
  server.send(200, "text/html", webpage);
}

String fetchSpotifyAuthCode() {
  log_i("*** Entering fetchSpotifyAuthCode()");
  if (MDNS.begin(SPOTIFY_ESPOTIFIER_NODE_NAME)) {
    log_i("MDNS responder started for node name '%s'.", SPOTIFY_ESPOTIFIER_NODE_NAME);
    log_i("Open browser at http://%s.local", SPOTIFY_ESPOTIFIER_NODE_NAME);
  }

  server.on("/", handleRoot);
  server.on("/callback/", handleCallback);
  server.on("/favicon.ico", handleFavicon);
  server.onNotFound(handleNotFound);
  server.begin();
  log_i("HTTP server started");

  while (authCode == "") {
    //log_i("--- calling server.handleClient().");
    server.handleClient();
    yield();
  }

  log_i("Successfully loaded Spotify authentication code: '%s'.", authCode.c_str());

  log_i("Stopping HTTP server");
  server.stop();
  log_i("Stopping MDNS responder");
  MDNS.end();

  return authCode;
}
