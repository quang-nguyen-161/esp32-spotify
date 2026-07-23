#include "wifi_portal.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

namespace WifiPortal {

  // ---------- Internal state ----------
  static const char* AP_SSID = "ESP32-Setup";
  static const char* AP_PASS = "12345678"; // must be at least 8 characters

  static DNSServer dnsServer;
  static WebServer server(80);
  static Preferences prefs;
  static const byte DNS_PORT = 53;
  static IPAddress apIP(192, 168, 4, 1);

  static bool s_portalMode = false;

  static String s_wifiSsid, s_wifiPass;
  static String s_vercelBaseUrl, s_userId, s_apiKey;

  // ---------- HTML form ----------
  static const char* FORM_HTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Setup</title>
  <style>
    body{font-family:sans-serif;background:#111;color:#eee;padding:20px;}
    h2{color:#1DB954;}
    h4{color:#888;margin-top:24px;}
    input{width:100%;padding:10px;margin:6px 0;border-radius:6px;border:1px solid #333;background:#222;color:#fff;box-sizing:border-box;}
    label{font-size:14px;color:#aaa;}
    button{width:100%;padding:12px;background:#1DB954;border:none;border-radius:6px;color:#000;font-weight:bold;margin-top:16px;}
    .hint{font-size:12px;color:#666;margin-top:-2px;}
  </style>
</head>
<body>
  <h2>WiFi + Server Setup</h2>
  <form action="/save" method="POST">
    <h4>WiFi</h4>
    <label>SSID</label>
    <input name="wifi_ssid" required>
    <label>Password</label>
    <input name="wifi_pass" type="password">

    <h4>Vercel (Now Playing proxy)</h4>
    <label>Base URL</label>
    <input name="vercel_url" placeholder="https://your-app.vercel.app" required>
    <label>Spotify User ID</label>
    <input name="user_id" placeholder="xuan123" required>
    <label>API Key</label>
    <input name="api_key" type="password" required>
    <div class="hint">Must match the ESP32_API_KEY set on Vercel.</div>

    <button type="submit">Save and Connect</button>
  </form>
</body>
</html>
)rawliteral";

  // ---------- Load / Save config ----------
  static void loadConfig() {
    prefs.begin("cfg", true);
    s_wifiSsid = prefs.getString("wifi_ssid", "");
    s_wifiPass = prefs.getString("wifi_pass", "");
    s_vercelBaseUrl = prefs.getString("vercel_url", "");
    s_userId = prefs.getString("user_id", "");
    s_apiKey = prefs.getString("api_key", "");
    prefs.end();
  }

  static void saveConfig() {
    prefs.begin("cfg", false);
    prefs.putString("wifi_ssid", s_wifiSsid);
    prefs.putString("wifi_pass", s_wifiPass);
    prefs.putString("vercel_url", s_vercelBaseUrl);
    prefs.putString("user_id", s_userId);
    prefs.putString("api_key", s_apiKey);
    prefs.end();
  }

  void resetConfig() {
    prefs.begin("cfg", false);
    prefs.clear();
    prefs.end();
  }

  // ---------- Helper: strip trailing "/" from URL if present ----------
  static String stripTrailingSlash(String url) {
    while (url.length() > 0 && url.endsWith("/")) {
      url.remove(url.length() - 1);
    }
    return url;
  }

  // ---------- Web handlers ----------
  static void handleRoot() {
    server.send(200, "text/html", FORM_HTML);
  }

  static void handleSave() {
    if (server.hasArg("wifi_ssid")) s_wifiSsid = server.arg("wifi_ssid");
    if (server.hasArg("wifi_pass")) s_wifiPass = server.arg("wifi_pass");
    if (server.hasArg("vercel_url")) s_vercelBaseUrl = stripTrailingSlash(server.arg("vercel_url"));
    if (server.hasArg("user_id")) s_userId = server.arg("user_id");
    if (server.hasArg("api_key")) s_apiKey = server.arg("api_key");

    saveConfig();

    server.send(200, "text/html",
      "<html><body style='font-family:sans-serif;background:#111;color:#eee;padding:20px'>"
      "<h3>Config saved. ESP32 is restarting...</h3></body></html>");

    delay(1500);
    ESP.restart();
  }

  static void handleNotFound() {
    server.sendHeader("Location", String("http://") + apIP.toString(), true);
    server.send(302, "text/plain", "");
  }

  static void startCaptivePortal() {
    s_portalMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASS);

    dnsServer.start(DNS_PORT, "*", apIP);

    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleNotFound);
    server.begin();

    Serial.println("[wifi_portal] Captive portal started.");
    Serial.println("[wifi_portal] Connect to WiFi: " + String(AP_SSID) + " (pass: " + String(AP_PASS) + ")");
    Serial.println("[wifi_portal] IP: " + apIP.toString());
  }

  static bool connectWiFi(unsigned long timeoutMs = 15000) {
    if (s_wifiSsid.length() == 0) return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(s_wifiSsid.c_str(), s_wifiPass.c_str());

    unsigned long start = millis();
    Serial.print("[wifi_portal] Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
      delay(300);
      Serial.print(".");
    }
    Serial.println();

    return WiFi.status() == WL_CONNECTED;
  }

  bool begin() {
    loadConfig();

    // Requires both WiFi and Vercel config (url + key) before considering setup "ready"
    bool hasFullConfig = s_wifiSsid.length() > 0 &&
                          s_vercelBaseUrl.length() > 0 &&
                          s_userId.length() > 0 &&
                          s_apiKey.length() > 0;

    if (hasFullConfig && connectWiFi()) {
      Serial.println("[wifi_portal] WiFi connected: " + WiFi.localIP().toString());
      s_portalMode = false;
      return true;
    }

    Serial.println("[wifi_portal] Missing config or connection failed -> starting captive portal.");
    startCaptivePortal();
    return false;
  }

  void loop() {
    if (s_portalMode) {
      dnsServer.processNextRequest();
      server.handleClient();
    }
  }

  bool isInPortalMode() {
    return s_portalMode;
  }

  String getWifiSsid()      { return s_wifiSsid; }
  String getVercelBaseUrl() { return s_vercelBaseUrl; }
  String getUserId()        { return s_userId; }
  String getApiKey()        { return s_apiKey; }

} // namespace WifiPortal