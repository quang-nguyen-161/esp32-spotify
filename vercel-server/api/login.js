// GET /api/login?user=<id>
// Open this URL once in a browser to authorize a Spotify account.
// If you have multiple accounts/devices, use a different user id for each authorization.
export default function handler(req, res) {
  const userId = req.query.user || 'default';
  const scope =
    'user-read-currently-playing user-read-playback-state user-modify-playback-state';

  const params = new URLSearchParams({
    client_id: process.env.SPOTIFY_CLIENT_ID,
    response_type: 'code',
    redirect_uri: process.env.REDIRECT_URI,
    scope,
    state: userId,
  });

  res.redirect(`https://accounts.spotify.com/authorize?${params.toString()}`);
}