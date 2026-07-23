import { kv } from '@vercel/kv';

// Recursively normalize all strings to NFC Unicode form.
// Source text (Vietnamese, etc.) can arrive as decomposed Unicode (NFD)
// depending on the client/OS that sent it, which some ESP32 fonts/renderers
// won't display correctly. Normalizing guarantees consistent NFC UTF-8 bytes.
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

// Parse LRC format "[mm:ss.xx]text" -> [{ time_ms, text }]
function parseLrc(lrcText) {
  const lines = lrcText.split('\n');
  const result = [];
  const timeTag = /\[(\d{2}):(\d{2})(?:\.(\d{2,3}))?\]/g;

  for (const line of lines) {
    const matches = [...line.matchAll(timeTag)];
    if (matches.length === 0) continue;

    const text = line.replace(timeTag, '').trim();
    for (const m of matches) {
      const min = parseInt(m[1], 10);
      const sec = parseInt(m[2], 10);
      const ms = m[3] ? parseInt(m[3].padEnd(3, '0'), 10) : 0;
      const time_ms = min * 60000 + sec * 1000 + ms;
      result.push({ time_ms, text });
    }
  }

  result.sort((a, b) => a.time_ms - b.time_ms);
  return result;
}

// GET /api/lyrics?track=<name>&artist=<name>&album=<name>&duration=<seconds>
// album and duration are optional but help lrclib match more accurately
export default async function handler(req, res) {
  // force explicit UTF-8 so non-ASCII text (Vietnamese, etc.) is never misinterpreted
  res.setHeader('Content-Type', 'application/json; charset=utf-8');

  if (req.headers['x-api-key'] !== process.env.ESP32_API_KEY) {
    return res.status(401).json({ error: 'unauthorized' });
  }

  // normalize incoming query text too, in case the caller sends decomposed Unicode
  const track = req.query.track ? req.query.track.normalize('NFC') : undefined;
  const artist = req.query.artist ? req.query.artist.normalize('NFC') : undefined;
  const album = req.query.album ? req.query.album.normalize('NFC') : undefined;
  const duration = req.query.duration;

  if (!track || !artist) {
    return res.status(400).json({ error: 'required query params: track, artist' });
  }

  const cacheKey = `lyrics:${artist}:${track}:${album || ''}`.toLowerCase();
  const cached = await kv.get(cacheKey);
  if (cached) {
    return res.json(toUtf8(cached));
  }

  const params = new URLSearchParams({
    track_name: track,
    artist_name: artist,
  });
  if (album) params.set('album_name', album);
  if (duration) params.set('duration', duration);

  const lrcResp = await fetch(`https://lrclib.net/api/get?${params.toString()}`, {
    headers: { 'User-Agent': 'esp32-vitalsync-lyrics/1.0' },
  });

  if (lrcResp.status === 404) {
    const notFound = { found: false, synced: false, lines: [] };
    await kv.set(cacheKey, notFound, { ex: 60 * 60 * 24 }); // cache 1 day, avoid hammering lrclib for missing songs
    return res.json(notFound);
  }

  if (!lrcResp.ok) {
    return res.status(lrcResp.status).json({ error: 'lrclib_error' });
  }

  const data = await lrcResp.json();

  let result;
  if (data.syncedLyrics) {
    result = {
      found: true,
      synced: true,
      lines: parseLrc(data.syncedLyrics),
    };
  } else if (data.plainLyrics) {
    result = {
      found: true,
      synced: false,
      lines: data.plainLyrics.split('\n').map((text) => ({ time_ms: null, text })),
    };
  } else {
    result = { found: false, synced: false, lines: [] };
  }

  result = toUtf8(result);

  // cache for 30 days, song lyrics don't change
  await kv.set(cacheKey, result, { ex: 60 * 60 * 24 * 30 });

  res.json(result);
}
