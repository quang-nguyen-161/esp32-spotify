# API Testing Reference — ESP32 Spotify Proxy

Base URL: `https://esp32-spotify.vercel.app`
All endpoints require header: `x-api-key: <ESP32_API_KEY>`

## PowerShell setup (run once per session)

```powershell
$base = "https://esp32-spotify.vercel.app"
$headers = @{"x-api-key" = "quang1612004"}
$user = "default"
```

## curl setup (Git Bash / WSL / macOS / Linux)

```bash
BASE="https://esp32-spotify.vercel.app"
KEY="quang1612004"
USER="default"
```

---

## 1. Login / Authorize

Open in **browser** (not curl/PowerShell — needs interactive login + redirect):
```
https://esp32-spotify.vercel.app/api/login?user=default
```
Expected result: redirected to Spotify → Agree → page shows `Authorization complete for user "default"`.

Re-run this any time the account needs re-authorizing (e.g. after adding a new scope).

---

## 2. Now Playing

**PowerShell**
```powershell
Invoke-RestMethod -Uri "$base/api/now-playing?user=$user" -Headers $headers
```

**curl**
```bash
curl -H "x-api-key: $KEY" "$BASE/api/now-playing?user=$USER"
```

Expected: JSON with `playing`, `track`, `artist`, `album`, `progress_ms`, `duration_ms`, `dominant_color`, `volume_percent`, `device_name`, `shuffle_state`, `repeat_state`, `context_type`, `next`.

If nothing is playing: `{"playing": false}`.

---

## 3. Artwork (raw RGB565 binary)

**PowerShell**
```powershell
Invoke-RestMethod -Uri "$base/api/now-playing?user=$user&art=raw" -Headers $headers -OutFile artwork.bin
(Get-Item artwork.bin).Length   # expect: 8192
```

**curl**
```bash
curl -H "x-api-key: $KEY" "$BASE/api/now-playing?user=$USER&art=raw" --output artwork.bin
ls -la artwork.bin   # expect: 8192 bytes
```

8192 bytes = 64 × 64 pixels × 2 bytes/pixel (RGB565). If size is wrong, check `ART_SIZE` in `now-playing.js`.

---

## 4. Playback Control

All control actions require an **active Spotify device** (app open somewhere) and a **Premium account**.

### Pause
```powershell
Invoke-RestMethod -Uri "$base/api/control?user=$user&action=pause" -Headers $headers
```
```bash
curl -H "x-api-key: $KEY" "$BASE/api/control?user=$USER&action=pause"
```

### Play
```powershell
Invoke-RestMethod -Uri "$base/api/control?user=$user&action=play" -Headers $headers
```

### Next track
```powershell
Invoke-RestMethod -Uri "$base/api/control?user=$user&action=next" -Headers $headers
```

### Previous track
```powershell
Invoke-RestMethod -Uri "$base/api/control?user=$user&action=previous" -Headers $headers
```

### Volume (0-100)
```powershell
Invoke-RestMethod -Uri "$base/api/control?user=$user&action=volume&value=50" -Headers $headers
```

### Shuffle (true/false)
```powershell
Invoke-RestMethod -Uri "$base/api/control?user=$user&action=shuffle&state=true" -Headers $headers
```

### Repeat (off/track/context)
```powershell
Invoke-RestMethod -Uri "$base/api/control?user=$user&action=repeat&state=track" -Headers $headers
```

Expected success response: `{"ok": true, "action": "<action>"}`.

If `{"error": "no active device found..."}` → open Spotify app on any device and make sure it's actively playing/paused (not just installed and idle).

---

## 5. Lyrics (synced, via lrclib.net)

**PowerShell**
```powershell
Invoke-RestMethod -Uri "$base/api/lyrics?track=Levitating&artist=Dua%20Lipa" -Headers $headers
```

**curl**
```bash
curl -H "x-api-key: $KEY" "$BASE/api/lyrics?track=Levitating&artist=Dua%20Lipa"
```

With album + duration for a more accurate match:
```powershell
Invoke-RestMethod -Uri "$base/api/lyrics?track=Levitating&artist=Dua%20Lipa&album=Future%20Nostalgia&duration=203" -Headers $headers
```

Expected: `{"found": true, "synced": true, "lines": [{"time_ms": ..., "text": "..."}, ...]}`.
If lrclib doesn't have the song: `{"found": false, "synced": false, "lines": []}`.

---

## 6. Error case checks (should fail on purpose)

### Wrong API key (expect 401)
```powershell
Invoke-RestMethod -Uri "$base/api/now-playing?user=$user" -Headers @{"x-api-key"="wrong_key"}
```
Expected: `{"error": "unauthorized"}`

### Unauthorized user (expect 404)
```powershell
Invoke-RestMethod -Uri "$base/api/now-playing?user=someone_never_logged_in" -Headers $headers
```
Expected: `{"error": "user \"someone_never_logged_in\" is not authorized yet"}`

### Invalid control action (expect 400)
```powershell
Invoke-RestMethod -Uri "$base/api/control?user=$user&action=teleport" -Headers $headers
```
Expected: `{"error": "invalid action", "valid_actions": [...]}`

---

## Quick full-run script (PowerShell)

Paste this whole block to smoke-test everything in sequence:

```powershell
$base = "https://esp32-spotify.vercel.app"
$headers = @{"x-api-key" = "quang1612004"}
$user = "default"

Write-Host "`n== now-playing ==" -ForegroundColor Cyan
Invoke-RestMethod -Uri "$base/api/now-playing?user=$user" -Headers $headers | Format-List

Write-Host "`n== artwork ==" -ForegroundColor Cyan
Invoke-RestMethod -Uri "$base/api/now-playing?user=$user&art=raw" -Headers $headers -OutFile artwork.bin
Write-Host "artwork.bin size: $((Get-Item artwork.bin).Length) bytes (expect 8192)"

Write-Host "`n== control: pause ==" -ForegroundColor Cyan
Invoke-RestMethod -Uri "$base/api/control?user=$user&action=pause" -Headers $headers

Write-Host "`n== control: play ==" -ForegroundColor Cyan
Invoke-RestMethod -Uri "$base/api/control?user=$user&action=play" -Headers $headers

Write-Host "`n== lyrics ==" -ForegroundColor Cyan
Invoke-RestMethod -Uri "$base/api/lyrics?track=Levitating&artist=Dua%20Lipa" -Headers $headers | ConvertTo-Json -Depth 5

Write-Host "`n== wrong key (expect 401) ==" -ForegroundColor Cyan
try {
  Invoke-RestMethod -Uri "$base/api/now-playing?user=$user" -Headers @{"x-api-key"="wrong"}
} catch {
  Write-Host $_.Exception.Response.StatusCode
}

Write-Host "`nDone." -ForegroundColor Green
```

---

## Known gotchas

- `curl` on Windows PowerShell is aliased to `Invoke-WebRequest` and doesn't accept `-H` the same way. Use `curl.exe` explicitly, or use `Invoke-RestMethod` with `-Headers @{...}` instead.
- Pasting an API URL directly into the browser address bar will always return `{"error":"unauthorized"}` — browsers don't send custom headers like `x-api-key`. Use PowerShell/curl/Postman/Thunder Client instead.
- Playback control (`/api/control`) requires a **Spotify Premium** account. Free accounts cannot use Spotify's playback control API regardless of device state.
- After adding a new OAuth scope to `login.js`, existing tokens don't automatically gain that permission — re-run `/api/login?user=<id>` to re-authorize.
- All Spotify Web API paths must include `/v1/` (e.g. `https://api.spotify.com/v1/me/player/pause`) — a very easy typo to make.
