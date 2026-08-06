# Provider API

Last Music Player plays **local files** out of the box with no server. Its
**remote** features (search, streaming, radio, lyrics, link import) are powered
by an external **provider service** that the app talks to over plain HTTP.

This project ships the *client* only — it does **not** include a provider
implementation. If you want remote playback, you run your own provider that
implements the contract below, then point the app at it in
**Settings → Provider** (base URL + an auth token of your choosing).

This document describes only the HTTP shapes the app speaks. How a provider
actually resolves and streams audio is entirely up to you.

---

## Configuration

In the app's Settings you set two values:

| Setting | Sent as |
| --- | --- |
| **Provider base URL** | e.g. `https://provider.example.com` (no trailing slash needed). Default is `http://127.0.0.1:4527`. |
| **Provider API key**  | An opaque token your provider issues. The app does not interpret it. |

## Authentication

The app presents the API key two ways, depending on who makes the request:

- **JSON endpoints** (`/v1/search`, `/v1/resolve`, …) — fetched by the app, which
  sends an HTTP header:
  ```
  Authorization: Bearer <api key>
  ```
- **Media endpoints** (`/v1/stream/...`, `/v1/artwork...`) — fetched directly by
  the OS media/image pipeline, which can't set custom headers, so the key is
  passed as a query parameter instead:
  ```
  ?access_token=<api key>
  ```

Your provider decides what a valid token is and how to verify it. Reject
unauthorized requests with `401`. The app surfaces non-2xx responses using the
error JSON shape described at the end.

---

## Data model: a Track

Search-style responses return a JSON object with a `results` array of **Track**
objects. The app reads these fields (all optional unless noted; unknown fields
are ignored):

| Field | Type | Notes |
| --- | --- | --- |
| `title` | string | Track title. |
| `artist` | string | Artist / creator. |
| `album` | string | Album; defaults to the provider name if absent. |
| `sourceUrl` | string | **Important.** A stable provider-owned identifier for the track. The app keys the library, stream URL, radio, and lyrics off this. |
| `streamUrl` | string | Playable stream URL. If this points at your `/v1/stream/...` endpoint, the app preserves the path and refreshes the provider host/token from current settings. |
| `durationMs` | number | Track length in milliseconds. |
| `artworkUrl` | string | Cover-art URL. May point back at your `/v1/artwork` endpoint, or anywhere. |
| `provider` | string | Provider namespace (e.g. `library`, `service`). |

```jsonc
{
  "results": [
    {
      "title": "Example Song",
      "artist": "Example Artist",
      "album": "Example Album",
      "sourceUrl": "https://catalog.example.com/tracks/abc123",
      "durationMs": 213000,
      "artworkUrl": "https://provider.example.com/v1/artwork?id=abc123",
      "provider": "example"
    }
  ]
}
```

---

## Endpoints

### `GET /v1/providers`
Health/identity check. The app calls this to test connectivity and considers any
`2xx` a healthy provider. Body is not required (return your provider list if you
like).

### `GET /v1/search?q=<query>`
Full-text search. `q` is URL-encoded.
**Response:** `{ "results": [ Track, … ] }`.

### `GET /v1/related?sourceUrl=<url>`
"Radio" / autoplay — tracks related to a seed track. `sourceUrl` is URL-encoded.
**Response:** `{ "results": [ Track, … ] }` (return `{"results":[]}` if you don't
support it).

### `POST /v1/resolve`
Resolve a pasted link into a playable track.
**Body:** `{ "url": "<pasted link>" }`
**Response:** `{ "results": [ Track, … ] }` (typically one).

### `POST /v1/import-album`
Expand an album/playlist link into its tracks.
**Body:** `{ "url": "<album or playlist link>" }`
**Response:** `{ "results": [ Track, … ] }`.

### `GET /v1/lyrics?artist=&title=&album=&durationMs=&sourceUrl=`
Lyrics lookup. `artist` and `title` are always sent; `album`, `durationMs`, and
`sourceUrl` are added when known. All string params are URL-encoded.
**Response:** a JSON object the app parses for plain and/or time-synced lyrics,
e.g.:
```jsonc
{
  "found": true,
  "instrumental": false,
  "source": "lyrics",
  "trackName": "Example Song",
  "artistName": "Example Artist",
  "plain": "line one\nline two\n…",
  "synced": [
    { "timeMs": 0,    "text": "line one" },
    { "timeMs": 4200, "text": "line two" }
  ]
}
```
Return `{ "found": false }` when you have nothing.

### `GET /v1/stream/<streamId>?url=<sourceUrl>&access_token=<key>`
The actual audio. If a Track includes a `streamUrl` that already points at your
`/v1/stream/...` endpoint, the app keeps that Music API stream path and query,
then refreshes the provider host and `access_token` from current settings. This
lets your provider treat `streamId` as opaque server-owned state.

When no provider stream endpoint is available, the app can rebuild a generic
`"remote:<hash>"` or `"direct:<hash>"` stream URL from `sourceUrl`. Providers may
ignore `streamId` and rely on the URL-encoded `sourceUrl` query param.

Respond with the audio bytes (any container the Windows Media stack can play —
m4a/AAC, webm/opus, mp3, …). Support HTTP range requests for best seeking
behavior.

### `GET /v1/artwork...&access_token=<key>`
Cover-art image bytes. The app fetches whatever `artworkUrl` you returned; if it
points at your `/v1/artwork` path, the app refreshes the `access_token` query
param on it. Respond with the image bytes.

### `POST /v1/discord/artwork-url` *(optional)*
Used only by the optional Discord Rich Presence integration to obtain a
banner-art URL that Discord's CDN whitelist accepts.
**Body:** `{ "title": "…", "artist": "…" }`
**Response:** `{ "url": "https://…" }` (or `{}` / omit `url` if none found).

---

## Errors

For non-2xx responses, return a JSON body the app can surface:
```jsonc
{ "code": "not_found", "message": "Human-readable explanation" }
```
The app falls back to the raw body or the status code if this shape is absent.

## Timeouts

The app aborts JSON requests after **15 seconds**. Keep responses prompt; do
heavy work (transcoding, etc.) on the `/v1/stream` path, which is not bounded by
that timeout.

## Official service adapters

The provider contract is a client transport boundary, not an authorization to
extract or relay protected media from third-party services.

An Apple Music adapter may use Apple’s official Apple Music API for catalog and
authorized subscriber-library data. Developer tokens must be signed outside the
public client, and subscriber-specific requests require Apple’s Music User Token
flow. Apple Music playback must remain within an Apple-supported MusicKit playback
runtime.

This repository intentionally does not publish such an adapter, its credentials,
or its production configuration.
