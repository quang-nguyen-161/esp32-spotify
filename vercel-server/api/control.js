import { kv } from '@vercel/kv';

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

// map action -> { method, path, needsQuery }
const ACTIONS = {
  play: { method: 'PUT', path: '/me/player/play' },
  pause: { method: 'PUT', path: '/me/player/pause' },
  next: { method: 'POST', path: '/me/player/next' },
  previous: { method: 'POST', path: '/me/player/previous' },
  volume: { method: 'PUT', path: '/me/player/volume' }, // requires ?value=0-100
  shuffle: { method: 'PUT', path: '/me/player/shuffle' }, // requires ?state=true|false
  repeat: { method: 'PUT', path: '/me/player/repeat' }, // requires ?state=off|track|context
};

// GET /api/control?user=<id>&action=<play|pause|next|previous|volume|shuffle|repeat>&value=...&state=...
export default async function handler(req, res) {
  if (req.headers['x-api-key'] !== process.env.ESP32_API_KEY) {
    return res.status(401).json({ error: 'unauthorized' });
  }

  const userId = req.query.user || 'default';
  const action = req.query.action;
  const def = ACTIONS[action];

  if (!def) {
    return res.status(400).json({
      error: 'invalid action',
      valid_actions: Object.keys(ACTIONS),
    });
  }

  const access_token = await getAccessToken(userId);
  if (!access_token) {
    return res.status(404).json({ error: `user "${userId}" is not authorized yet` });
  }

  let url = `https://api.spotify.com${def.path}`;
  const params = new URLSearchParams();

  if (action === 'volume') {
    const value = req.query.value;
    if (value === undefined) {
      return res.status(400).json({ error: 'missing ?value=0-100' });
    }
    params.set('volume_percent', value);
  }

  if (action === 'shuffle') {
    const state = req.query.state;
    if (state === undefined) {
      return res.status(400).json({ error: 'missing ?state=true|false' });
    }
    params.set('state', state);
  }

  if (action === 'repeat') {
    const state = req.query.state;
    if (!['off', 'track', 'context'].includes(state)) {
      return res.status(400).json({ error: 'state must be off|track|context' });
    }
    params.set('state', state);
  }

  if ([...params].length > 0) {
    url += `?${params.toString()}`;
  }

  const spResp = await fetch(url, {
    method: def.method,
    headers: { Authorization: `Bearer ${access_token}` },
  });

  if (spResp.status === 429) {
    const retryAfter = parseInt(spResp.headers.get('retry-after') || '1', 10);
    res.setHeader('Retry-After', retryAfter);
    return res.status(429).json({ error: 'rate_limited', retry_after_seconds: retryAfter });
  }

  // Spotify returns 204 No Content on success
  if (spResp.status === 204 || spResp.ok) {
    return res.json({ ok: true, action });
  }

  // 404 usually means there is no active device, but show Spotify's real error body to confirm
  if (spResp.status === 404) {
    const detail = await spResp.text();
    return res.status(404).json({
      error: 'no active device found (open Spotify on a device first)',
      spotify_detail: detail,
    });
  }

  const detail = await spResp.text();
  return res.status(spResp.status).json({ error: 'spotify api error', detail });
}
