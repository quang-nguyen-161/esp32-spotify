import { kv } from '@vercel/kv';

// GET /api/callback?code=...&state=<userId>
// Spotify automatically calls this endpoint after the user grants permission.
export default async function handler(req, res) {
  const { code, state, error } = req.query;
  const userId = state || 'default';

  if (error) {
    res.status(400).send(`Authorization failed: ${error}`);
    return;
  }

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
      grant_type: 'authorization_code',
      code,
      redirect_uri: process.env.REDIRECT_URI,
    }),
  });

  const data = await resp.json();

  if (!data.refresh_token) {
    res.status(400).json({ error: 'failed to obtain refresh_token', detail: data });
    return;
  }

  await kv.set(`spotify_refresh_token:${userId}`, data.refresh_token);

  res.send(`Authorization complete for user "${userId}". You can close this tab.`);
}