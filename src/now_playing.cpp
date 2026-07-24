import { kv } from '@vercel/kv';
import sharp from 'sharp';

const ART_SIZE = 120; // change to match ESP32 screen size, e.g. 120x120

function rgb888ToRgb565Buffer(rgbBuffer, width, height) {
  // rgbBuffer: raw RGB (3 bytes/pixel) from sharp
  // returns a Buffer of RGB565, BIG-endian, 2 bytes/pixel -- must match
  // what the ESP32 firmware assumes (see now_playing.cpp's fetchArtwork()
  // comment: "RGB565 from the server is big-endian"). Using
  // writeUInt16LE() here while the client reads big-endian doesn't just
  // shift hue slightly -- byte-swapping a packed 5/6/5-bit value scrambles
  // essentially all the color bits, producing noisy/static-looking pixels
  // even on an otherwise perfectly-transferred, uncorrupted buffer.
  const out = Buffer.alloc(width * height * 2);
  for (let i = 0, p = 0; i < rgbBuffer.length; i += 3, p += 2) {
    const r = rgbBuffer[i];
    const g = rgbBuffer[i + 1];
    const b = rgbBuffer[i + 2];
    const val = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
    out.writeUInt16BE(val, p);
  }
  return out;
}

function rgbToHex(r, g, b) {
  return '#' + [r, g, b].map((v) => v.toString(16).padStart(2, '0')).join('');
}

// Recursively normalize all strings to NFC Unicode form.
// Spotify data (and any source data) can contain diacritics like Vietnamese
// text encoded as decomposed Unicode (NFD) instead of precomposed (NFC),
// which some ESP32 fonts/renderers won't display correctly. Normalizing
// here guarantees consistent, standard UTF-8 NFC bytes over the wire.
function toUtf8(value) {
  if (typeof value === 'string') return value.normalize('NFC');
  if (Array.isArray(value)) return value.map(toUtf8);
  if (value && typeof value === 'object') {
    const out = {};
    for (const key in value) out[key] = toUtf8(value[key]);
    return out;
  }
  return value;
}

async function getAccessToken(userId) {
  const refresh_token = await kv.get(`spotify_refresh_token:${userId}`);
  if (!refresh_token) return null;

  const resp = await fetch('https://accounts.spotify.com/api/token', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded',
      Authorization:
        'Basic ' +
        Buffer.from(
          `${process.env.SPOTIFY_CLIENT_ID}:${process.env.SPOTIFY_CLIENT_SECRET}`
        ).toString('base64'),
    },
    body: new URLSearchParams({
      grant_type: 'refresh_token',
      refresh_token,
    }),
  });
  const data = await resp.json();
  return data.access_token || null;
}

async function getDominantColor(trackId, imageUrl) {
  const cacheKey = `artwork_color:${trackId}`;
  const cached = await kv.get(cacheKey);
  if (cached) return cached;

  const imgResp = await fetch(imageUrl);
  const imgArrayBuffer = await imgResp.arrayBuffer();

  // downscale to 1x1, sharp automatically averages/blurs the whole image
  const { data: avgPixel } = await sharp(Buffer.from(imgArrayBuffer))
    .resize(1, 1)
    .removeAlpha()
    .raw()
    .toBuffer({ resolveWithObject: true });

  const dominant_color = rgbToHex(avgPixel[0], avgPixel[1], avgPixel[2]);
  await kv.set(cacheKey, dominant_color, { ex: 60 * 60 * 24 * 7 });
  return dominant_color;
}

async function getArtworkRgb565(trackId, imageUrl) {
  const cacheKey = `artwork_rgb565:${trackId}:${ART_SIZE}:be`; // "be" = big-endian format version, bump this suffix again if the byte format ever changes
  const cached = await kv.get(cacheKey);
  if (cached) return Buffer.from(cached, 'base64');

  const imgResp = await fetch(imageUrl);
  const imgArrayBuffer = await imgResp.arrayBuffer();

  const { data: rgbBuffer, info } = await sharp(Buffer.from(imgArrayBuffer))
    .resize(ART_SIZE, ART_SIZE)
    .removeAlpha()
    .raw()
    .toBuffer({ resolveWithObject: true });

  const rgb565 = rgb888ToRgb565Buffer(rgbBuffer, info.width, info.height);

  // cache as base64 in KV, expires after 7 days (avoid unbounded KV growth)
  await kv.set(cacheKey, rgb565.toString('base64'), { ex: 60 * 60 * 24 * 7 });

  return rgb565;
}

async function getNextTrack(access_token) {
  const resp = await fetch('https://api.spotify.com/v1/me/player/queue', {
    headers: { Authorization: `Bearer ${access_token}` },
  });
  if (!resp.ok) return null;

  const data = await resp.json();
  const next = data.queue?.[0];
  if (!next) return null;

  return {
    track_id: next.id,
    track: next.name,
    artist: next.artists?.[0]?.name,
    duration_ms: next.duration_ms,
  };
}

// wraps a Spotify API call, automatically detects 429 rate limiting and returns Retry-After
async function spotifyFetch(url, access_token, options = {}) {
  const resp = await fetch(url, {
    ...options,
    headers: { Authorization: `Bearer ${access_token}`, ...(options.headers || {}) },
  });
  if (resp.status === 429) {
    const retryAfter = parseInt(resp.headers.get('retry-after') || '1', 10);
    const err = new Error('rate_limited');
    err.retryAfter = retryAfter;
    throw err;
  }
  return resp;
}

export default async function handler(req, res) {
  // force explicit UTF-8 so non-ASCII text (Vietnamese, etc.) is never misinterpreted
  res.setHeader('Content-Type', 'application/json; charset=utf-8');

  if (req.headers['x-api-key'] !== process.env.ESP32_API_KEY) {
    return res.status(401).json({ error: 'unauthorized' });
  }

  const userId = req.query.user || 'default';
  const wantArt = req.query.art; // "raw" -> return binary RGB565 of the current artwork

  const access_token = await getAccessToken(userId);
  if (!access_token) {
    return res.status(404).json({ error: `user "${userId}" is not authorized yet` });
  }

  try {
    // use /v1/me/player instead of /currently-playing to get device, volume, context, shuffle, repeat in one call
    const spResp = await spotifyFetch(
      'https://api.spotify.com/v1/me/player',
      access_token
    );

    if (spResp.status === 204 || spResp.status === 202) {
      return res.json({ playing: false });
    }
    if (!spResp.ok) {
      return res.status(spResp.status).json({ error: 'spotify api error' });
    }

    const data = await spResp.json();
    const track = data.item;
    if (!track) return res.json({ playing: false });

    const imageUrl = track.album?.images?.[0]?.url;

    // raw mode: return the artwork's RGB565 bytes as base64 inside JSON.
    //
    // We deliberately do NOT send this as a raw `application/octet-stream`
    // body anymore. Vercel's serverless response path can re-chunk a binary
    // body (Transfer-Encoding: chunked) even when Content-Length is set
    // explicitly below -- the platform's proxy layer sits in front of this
    // handler and controls that independently of what we set here. A plain
    // HTTP client that reads the body byte-for-byte off the raw stream (as
    // the ESP32 was doing via http.getStreamPtr()) has no way to strip the
    // chunk-size/CRLF framing bytes that get interleaved into a chunked
    // stream, so those framing bytes end up read as if they were pixel
    // data -- corrupting everything after the first chunk boundary.
    //
    // Base64-in-JSON sidesteps this: it's parsed with the exact same
    // getString() + ArduinoJson path the ESP32 already uses successfully
    // for /api/now-playing's metadata response, which correctly handles
    // chunked transport because HTTPClient's own text-reading path does
    // the de-chunking for you (unlike a manual getStreamPtr() read loop).
    if (wantArt === 'raw') {
      if (!imageUrl) return res.status(404).json({ error: 'no artwork' });
      const rgb565 = await getArtworkRgb565(track.id, imageUrl);
      return res.json({
        artwork_base64: rgb565.toString('base64'),
        artwork_size: ART_SIZE,
        byte_length: rgb565.length,
      });
    }

    // fetch the next track in queue + the artwork's dominant color
    const [nextTrack, dominantColor] = await Promise.all([
      getNextTrack(access_token).catch(() => null),
      imageUrl ? getDominantColor(track.id, imageUrl).catch(() => null) : null,
    ]);

    // default mode: JSON metadata, ESP32 decides when to call ?art=raw
    res.json(toUtf8({
      playing: data.is_playing,
      track_id: track.id,
      track: track.name,
      artist: track.artists?.[0]?.name,
      album: track.album?.name,
      progress_ms: data.progress_ms,
      duration_ms: track.duration_ms,
      has_artwork: Boolean(imageUrl),
      artwork_size: ART_SIZE,
      dominant_color: dominantColor, // e.g. "#a83232", null if unavailable
      volume_percent: data.device?.volume_percent ?? null,
      device_name: data.device?.name ?? null,
      shuffle_state: data.shuffle_state ?? null,
      repeat_state: data.repeat_state ?? null, // "off" | "track" | "context"
      context_type: data.context?.type ?? null, // "playlist" | "album" | "artist" | null
      next: nextTrack, // null if queue is empty or the fetch failed
    }));
  } catch (err) {
    if (err.message === 'rate_limited') {
      res.setHeader('Retry-After', err.retryAfter);
      return res.status(429).json({
        error: 'rate_limited',
        retry_after_seconds: err.retryAfter,
      });
    }
    console.error(err);
    return res.status(500).json({ error: 'internal_error' });
  }
}