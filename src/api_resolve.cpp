#include "api_resolve.h"
#include <HTTPClient.h>
#include <base64.h>
#include <ArduinoJson.h>

namespace ApiResolve {

  // ---------- State noi bo cua module ----------
  static String s_clientId;
  static String s_clientSecret;
  static String s_refreshToken;

  static String s_accessToken;
  static unsigned long s_tokenExpiryMillis = 0; // millis() tai thoi diem token het han

  void begin(const String& clientId, const String& clientSecret, const String& refreshToken) {
    s_clientId = clientId;
    s_clientSecret = clientSecret;
    s_refreshToken = refreshToken;
    s_accessToken = "";
    s_tokenExpiryMillis = 0;
  }

  static String basicAuthHeader() {
    String raw = s_clientId + ":" + s_clientSecret;
    return "Basic " + base64::encode(raw);
  }

  // Xin access token moi bang refresh_token (Authorization Code Flow, du lieu ca nhan user)
  static String requestAccessTokenFromRefreshToken() {
    if (s_refreshToken.length() == 0 || s_clientId.length() == 0 || s_clientSecret.length() == 0) {
      Serial.println("[api_resolve] Thieu refresh_token / client id / client secret");
      return "";
    }

    HTTPClient http;
    http.begin("https://accounts.spotify.com/api/token");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Authorization", basicAuthHeader());

    String body = "grant_type=refresh_token&refresh_token=" + s_refreshToken;
    int httpCode = http.POST(body);

    String token = "";
    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      DeserializationError err = deserializeJson(doc, payload);
      if (!err) {
        token = doc["access_token"].as<String>();
        int expiresIn = doc["expires_in"] | 3600; // giay
        s_tokenExpiryMillis = millis() + (unsigned long)(expiresIn - 60) * 1000UL; // tru 60s cho an toan

        // Spotify doi khi tra ve refresh_token moi, neu co thi cap nhat
        if (doc.containsKey("refresh_token")) {
          s_refreshToken = doc["refresh_token"].as<String>();
        }

        Serial.println("[api_resolve] Refresh access token thanh cong.");
      } else {
        Serial.println("[api_resolve] Loi parse JSON khi refresh token.");
      }
    } else {
      Serial.printf("[api_resolve] Loi refresh token, HTTP code: %d\n", httpCode);
      Serial.println(http.getString());
    }

    http.end();
    return token;
  }

  String getAccessTokenClientCredentials() {
    if (s_clientId.length() == 0 || s_clientSecret.length() == 0) {
      Serial.println("[api_resolve] Thieu client id / client secret");
      return "";
    }

    HTTPClient http;
    http.begin("https://accounts.spotify.com/api/token");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Authorization", basicAuthHeader());

    int httpCode = http.POST("grant_type=client_credentials");

    String token = "";
    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(512);
      if (!deserializeJson(doc, payload)) {
        token = doc["access_token"].as<String>();
      }
    } else {
      Serial.printf("[api_resolve] Loi client credentials, HTTP code: %d\n", httpCode);
    }

    http.end();
    return token;
  }

  String getAccessToken(bool forceRefresh) {
    bool expired = (millis() >= s_tokenExpiryMillis);

    if (!forceRefresh && !expired && s_accessToken.length() > 0) {
      return s_accessToken; // token con hieu luc, khong can xin lai
    }

    s_accessToken = requestAccessTokenFromRefreshToken();
    return s_accessToken;
  }

  int fetchTopTracks(const String& accessToken, int limit) {
    if (accessToken.length() == 0) return 0;

    HTTPClient http;
    String url = "https://api.spotify.com/v1/me/top/tracks?time_range=long_term&limit=" + String(limit);
    http.begin(url);
    http.addHeader("Authorization", "Bearer " + accessToken);

    int httpCode = http.GET();
    int count = 0;

    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(8192);
      DeserializationError err = deserializeJson(doc, payload);
      if (!err) {
        JsonArray items = doc["items"].as<JsonArray>();
        for (JsonObject track : items) {
          const char* name = track["name"];
          String artistNames = "";
          JsonArray artists = track["artists"].as<JsonArray>();
          for (JsonObject artist : artists) {
            if (artistNames.length() > 0) artistNames += ", ";
            artistNames += artist["name"].as<String>();
          }
          Serial.printf("[api_resolve] %s by %s\n", name, artistNames.c_str());
          count++;
        }
      } else {
        Serial.println("[api_resolve] Loi parse JSON top tracks.");
      }
    } else {
      Serial.printf("[api_resolve] Loi fetch top tracks, HTTP code: %d\n", httpCode);
      Serial.println(http.getString());
    }

    http.end();
    return count;
  }

  String fetchCurrentlyPlaying(const String& accessToken) {
    if (accessToken.length() == 0) return "";

    HTTPClient http;
    http.begin("https://api.spotify.com/v1/me/player/currently-playing");
    http.addHeader("Authorization", "Bearer " + accessToken);

    int httpCode = http.GET();
    String trackName = "";

    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(4096);
      DeserializationError err = deserializeJson(doc, payload);
      if (!err && doc.containsKey("item")) {
        trackName = doc["item"]["name"].as<String>();
        Serial.println("[api_resolve] Dang phat: " + trackName);
      }
    } else if (httpCode == 204) {
      Serial.println("[api_resolve] Khong co bai nao dang phat.");
    } else {
      Serial.printf("[api_resolve] Loi fetch currently playing, HTTP code: %d\n", httpCode);
    }

    http.end();
    return trackName;
  }

  String searchTrack(const String& accessToken, const String& query) {
    if (accessToken.length() == 0 || query.length() == 0) return "";

    HTTPClient http;
    // URL-encode co ban cho khoang trang; voi ky tu dac biet khac can ham encode day du hon
    String encodedQuery = query;
    encodedQuery.replace(" ", "%20");

    String url = "https://api.spotify.com/v1/search?q=" + encodedQuery + "&type=track&limit=1";
    http.begin(url);
    http.addHeader("Authorization", "Bearer " + accessToken);

    int httpCode = http.GET();
    String trackName = "";

    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(4096);
      DeserializationError err = deserializeJson(doc, payload);
      if (!err) {
        trackName = doc["tracks"]["items"][0]["name"].as<String>();
        Serial.println("[api_resolve] Ket qua search: " + trackName);
      }
    } else {
      Serial.printf("[api_resolve] Loi search track, HTTP code: %d\n", httpCode);
    }

    http.end();
    return trackName;
  }

} // namespace ApiResolve