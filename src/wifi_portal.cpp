#include "wifi_portal.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

namespace WifiPortal {

static Preferences prefs;
static const char* PREF_NS = "wifi";

static DNSServer dnsServer;
static WebServer server(80);
static const byte DNS_PORT = 53;
static IPAddress apIP(192, 168, 4, 1);

static bool s_credentialsSubmitted = false;
static String s_pendingSsid, s_pendingPass;

// ---------- NVS helpers ----------
String getSavedSsid() {
  prefs.begin(PREF_NS, true);
  String s = prefs.getString("ssid", "");
  prefs.end();
  return s;
}

static String getSavedPass() {
  prefs.begin(PREF_NS, true);
  String p = prefs.getString("pass", "");
  prefs.end();
  return p;
}

bool hasCredentials() {
  return getSavedSsid().length() > 0;
}

void clearCredentials() {
  prefs.begin(PREF_NS, false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
}

static void saveCredentials(const String& ssid, const String& pass) {
  prefs.begin(PREF_NS, false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

// ---------- Configuration page HTML ----------
static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<title>WiFi Setup</title>
<style>
body{font-family:sans-serif;background:#111;color:#eee;padding:24px;}
h2{color:#1DB954;}
input{width:100%;padding:10px;margin:8px 0;border-radius:6px;border:none;box-sizing:border-box;}
button{width:100%;padding:12px;background:#1DB954;color:#000;border:none;border-radius:6px;font-weight:bold;font-size:16px;}
.card{background:#222;padding:20px;border-radius:12px;max-width:400px;margin:auto;}
</style></head><body>
<div class="card">
<h2>Configure WiFi for the device</h2>
<form action="/save" method="POST">
  <label>WiFi name (SSID)</label>
  <input type="text" name="ssid" required>
  <label>Password</label>
  <input type="password" name="pass">
  <button type="submit">Save and connect</button>
</form>
</div>
</body></html>
)rawliteral";

static void handleRoot() {
  server.send(200, "text/html", PAGE_HTML);
}

static void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0) {
    server.send(400, "text/plain", "Missing SSID");
    return;
  }
  s_pendingSsid = ssid;
  s_pendingPass = pass;
  s_credentialsSubmitted = true;

  server.send(200, "text/html",
    "<html><body style='font-family:sans-serif;background:#111;color:#eee;padding:24px;'>"
    "<h3>Saved. The device is now trying to connect to WiFi...</h3>"
    "<p>You can close this page.</p></body></html>");
}

// OS captive-portal-detection endpoints: return the config page so the
// system automatically pops up the "Sign in to network" prompt
// (Android/iOS/Windows).
static void handleCaptivePortalRedirect() {
  handleRoot();
}

static void handleNotFound() {
  handleRoot();
}

static void startAP(const char* apSsid, const char* apPass) {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  if (apPass && strlen(apPass) > 0) {
    WiFi.softAP(apSsid, apPass);
  } else {
    WiFi.softAP(apSsid);
  }
  delay(200);

  dnsServer.start(DNS_PORT, "*", apIP); // return apIP for EVERY domain

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  // Common "internet check" URLs used by each OS
  server.on("/generate_204", handleCaptivePortalRedirect);       // Android
  server.on("/gen_204", handleCaptivePortalRedirect);            // Android
  server.on("/hotspot-detect.html", handleCaptivePortalRedirect);// iOS/macOS
  server.on("/ncsi.txt", handleCaptivePortalRedirect);           // Windows
  server.on("/connecttest.txt", handleCaptivePortalRedirect);    // Windows
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.printf("[wifi_portal] AP '%s' started, IP: %s\n", apSsid, apIP.toString().c_str());
}

static bool tryConnectSTA(const String& ssid,
                          const String& pass,
                          unsigned long timeoutMs)
{
  Serial.printf("[wifi_portal] Connecting to '%s'...\n", ssid.c_str());

  WiFi.mode(WIFI_STA);

  // Static IP configuration
  IPAddress local_IP(192, 168, 31, 50);
  IPAddress gateway(192, 168, 31, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns1(8, 8, 8, 8);
  IPAddress dns2(1, 1, 1, 1);

  if (!WiFi.config(local_IP, gateway, subnet, dns1, dns2)) {
    Serial.println("[wifi_portal] Failed to configure static IP");
  }

  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[wifi_portal] Connected successfully.");
    Serial.print("[wifi_portal] IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("[wifi_portal] Connection failed.");
  return false;
}

bool connectOrStartPortal(unsigned long connectTimeoutMs,
                           unsigned long portalTimeoutMs,
                           const char* apSsid,
                           const char* apPass) {
  // 1) If credentials already exist in NVS, try connecting first
  if (hasCredentials()) {
    String ssid = getSavedSsid();
    String pass = getSavedPass();
    if (tryConnectSTA(ssid, pass, connectTimeoutMs)) {
      return true;
    }
    Serial.println("[wifi_portal] Saved credentials failed to connect, starting captive portal.");
  } else {
    Serial.println("[wifi_portal] No WiFi credentials yet, starting captive portal.");
  }

  // 2) Start AP + captive portal, wait for user input
  startAP(apSsid, apPass);
  s_credentialsSubmitted = false;

  unsigned long portalStart = millis();
  bool connected = false;

  while (true) {
    dnsServer.processNextRequest();
    server.handleClient();

    if (s_credentialsSubmitted) {
      saveCredentials(s_pendingSsid, s_pendingPass);
      // Switch to STA to connect (this also tears down the AP)
      server.stop();
      dnsServer.stop();

      if (tryConnectSTA(s_pendingSsid, s_pendingPass, connectTimeoutMs)) {
        connected = true;
        break;
      } else {
        // Connection failed -> reopen the portal so the user can retry
        Serial.println("[wifi_portal] Wrong WiFi info, reopening portal.");
        startAP(apSsid, apPass);
        s_credentialsSubmitted = false;
      }
    }

    if (portalTimeoutMs > 0 && millis() - portalStart > portalTimeoutMs) {
      Serial.println("[wifi_portal] WiFi setup timed out.");
      break;
    }
    delay(5);
  }

  return connected;
}

} // namespace WifiPortal