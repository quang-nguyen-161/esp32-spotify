#ifndef API_RESOLVE_H
#define API_RESOLVE_H

#include <Arduino.h>

/*
  Module: api_resolve
  Chuc nang: quan ly Authorization + goi Spotify Web API tu ESP32 (PlatformIO)

  Cach dung trong main.cpp:

    #include "api_resolve.h"

    void setup() {
      ...
      // sau khi da co WiFi va da luu spotifyClientId/Secret/RefreshToken (vi du tu captive portal)
      ApiResolve::begin(spotifyClientId, spotifyClientSecret, spotifyRefreshToken);

      String token = ApiResolve::getAccessToken(); // tu dong refresh neu can
      ApiResolve::fetchTopTracks(token);
    }

  Luu y ve refresh_token:
    - refresh_token KHONG the lay tu ESP32. Phai lay 1 lan tren may tinh
      qua Authorization Code Flow (dang nhap Spotify tren browser), roi
      copy refresh_token do vao ESP32 (vi du qua captive portal form).
    - access_token het han sau ~1 gio, ApiResolve se tu goi refresh khi can.
*/

namespace ApiResolve {

  // Khoi tao voi credentials (goi 1 lan truoc khi dung cac ham khac)
  void begin(const String& clientId, const String& clientSecret, const String& refreshToken);

  // Lay access token hien tai; tu dong xin token moi neu chua co hoac da het han
  String getAccessToken(bool forceRefresh = false);

  // Xin access token bang Client Credentials Flow (chi dung cho API public,
  // KHONG dung duoc cho /v1/me/... vi khong gan voi user cu the)
  String getAccessTokenClientCredentials();

  // Fetch top tracks cua user (can access token tu refresh_token / user login that)
  // In ra Serial danh sach bai hat, tra ve so luong track lay duoc
  int fetchTopTracks(const String& accessToken, int limit = 5);

  // Fetch bai hat dang phat (currently playing)
  // Tra ve ten bai hat, hoac "" neu khong co gi dang phat / loi
  String fetchCurrentlyPlaying(const String& accessToken);

  // Search 1 track theo tu khoa (dung duoc voi Client Credentials token)
  // Tra ve ten bai hat dau tien tim duoc, hoac "" neu khong tim thay
  String searchTrack(const String& accessToken, const String& query);

} // namespace ApiResolve

#endif // API_RESOLVE_H