#include "spotify_auth.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <base64.h>
#include <ArduinoJson.h>
#include <Preferences.h>

namespace SpotifyAuth {

static Preferences prefs;
static const char* PREF_NS = "spotify";
static WebServer server(80);

static String s_clientId, s_clientSecret;
static bool s_done = false;
static bool s_success = false;

// ---------- NVS ----------
bool loadCredentials(String& clientId, String& clientSecret, String& refreshToken) {
  prefs.begin(PREF_NS, true);
  clientId = prefs.getString("client_id", "");
  clientSecret = prefs.getString("client_secret", "");
  refreshToken = prefs.getString("refresh_token", "");
  prefs.end();
  return refreshToken.length() > 0;
}

bool hasTokens() {
  String a, b, c;
  return loadCredentials(a, b, c);
}

void saveAppCredentials(const String& clientId, const String& clientSecret) {
  prefs.begin(PREF_NS, false);
  prefs.putString("client_id", clientId);
  prefs.putString("client_secret", clientSecret);
  prefs.end();
}

static void saveRefreshToken(const String& refreshToken) {
  prefs.begin(PREF_NS, false);
  prefs.putString("refresh_token", refreshToken);
  prefs.end();
}

void clearTokens() {
  prefs.begin(PREF_NS, false);
  prefs.remove("refresh_token");
  prefs.end();
}

// ---------- HTTP handlers ----------
static String s_scope;
static String s_redirectUri;

static String urlEncode(const String& s) {
  String out;
  char buf[4];
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else if (c == ' ') {
      out += "%20";
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      out += buf;
    }
  }
  return out;
}

static void handleRoot() {
  String authUrl = "https://accounts.spotify.com/authorize"
                    "?response_type=code"
                    "&client_id=" + urlEncode(s_clientId) +
                    "&scope=" + urlEncode(s_scope) +
                    "&redirect_uri=" + urlEncode(s_redirectUri);

  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<style>body{font-family:sans-serif;background:#111;color:#eee;padding:24px;text-align:center;}"
                "a{display:inline-block;margin-top:20px;background:#1DB954;color:#000;padding:14px 24px;"
                "border-radius:30px;text-decoration:none;font-weight:bold;}</style></head><body>"
                "<h2>Connect Spotify to your device</h2>"
                "<p>Tap the button below and log in with your Spotify account.</p>"
                "<a href='" + authUrl + "'>Log in with Spotify</a>"
                "</body></html>";
  server.send(200, "text/html", html);
}

static bool exchangeCodeForTokens(const String& code) {
  HTTPClient http;
  http.begin("https://accounts.spotify.com/api/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String raw = s_clientId + ":" + s_clientSecret;
  http.addHeader("Authorization", "Basic " + base64::encode(raw));

  String body = "grant_type=authorization_code"
                 "&code=" + code +
                 "&redirect_uri=" + urlEncode(s_redirectUri);

  int httpCode = http.POST(body);
  bool ok = false;

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    if (!deserializeJson(doc, payload)) {
      if (doc.containsKey("refresh_token")) {
        saveRefreshToken(doc["refresh_token"].as<String>());
        Serial.println("[spotify_auth] refresh_token saved to NVS.");
        ok = true;
      } else {
        Serial.println("[spotify_auth] Response did not contain a refresh_token.");
      }
    }
  } else {
    Serial.printf("[spotify_auth] Error exchanging code for token, HTTP %d\n", httpCode);
    Serial.println(http.getString());
  }
  http.end();
  return ok;
}

static void handleCallback() {
  if (server.hasArg("error")) {
    server.send(400, "text/html", "<h3>Login failed: " + server.arg("error") + "</h3>");
    s_done = true;
    s_success = false;
    return;
  }
  if (!server.hasArg("code")) {
    server.send(400, "text/plain", "Missing 'code'");
    return;
  }

  String code = server.arg("code");
  bool ok = exchangeCodeForTokens(code);

  s_done = true;
  s_success = ok;

  if (ok) {
    server.send(200, "text/html",
      "<html><body style='font-family:sans-serif;background:#111;color:#1DB954;padding:24px;'>"
      "<h2>Successfully connected to Spotify!</h2><p>You can close this page.</p></body></html>");
  } else {
    server.send(200, "text/html",
      "<html><body style='font-family:sans-serif;background:#111;color:#e74c3c;padding:24px;'>"
      "<h2>Connection failed.</h2><p>Please try again.</p></body></html>");
  }
}

bool runAuthPortal(unsigned long timeoutMs, const String& scope) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[spotify_auth] No WiFi connection, cannot run auth portal.");
    return false;
  }

  String clientId, clientSecret, refreshTokenUnused;
  prefs.begin(PREF_NS, true);
  clientId = prefs.getString("client_id", "");
  clientSecret = prefs.getString("client_secret", "");
  prefs.end();

  if (clientId.length() == 0 || clientSecret.length() == 0) {
    Serial.println("[spotify_auth] client_id/client_secret not set, call saveAppCredentials() first.");
    return false;
  }

  s_clientId = clientId;
  s_clientSecret = clientSecret;
  s_scope = scope;
  s_redirectUri = "http://" + WiFi.localIP().toString() + ":80/callback";
  s_done = false;
  s_success = false;

  server.on("/", handleRoot);
  server.on("/callback", handleCallback);
  server.begin();

  Serial.println("[spotify_auth] Open in a browser: http://" + WiFi.localIP().toString() + "/");
  Serial.println("[spotify_auth] Redirect URI to register in the Spotify Dashboard: " + s_redirectUri);

  unsigned long start = millis();
  while (!s_done && millis() - start < timeoutMs) {
    server.handleClient();
    delay(5);
  }

  server.stop();
  return s_success;
}

} // namespace SpotifyAuth