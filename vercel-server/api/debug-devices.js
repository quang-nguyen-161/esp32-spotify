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

// TEMPORARY debug endpoint - lists devices Spotify currently sees for this account.
// Delete this file once debugging is done, it's not needed for normal operation.
// GET /api/debug-devices?user=<id>
export default async function handler(req, res) {
  if (req.headers['x-api-key'] !== process.env.ESP32_API_KEY) {
    return res.status(401).json({ error: 'unauthorized' });
  }

  const userId = req.query.user || 'default';
  const access_token = await getAccessToken(userId);
  if (!access_token) {
    return res.status(404).json({ error: `user "${userId}" is not authorized yet` });
  }

  const resp = await fetch('https://api.spotify.com/v1/me/player/devices', {
    headers: { Authorization: `Bearer ${access_token}` },
  });

  const data = await resp.json();
  res.status(resp.status).json(data);
}
