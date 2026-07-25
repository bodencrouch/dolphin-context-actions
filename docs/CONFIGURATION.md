# Configuration

## Interactive configuration

Right-click any media file in Dolphin → **Convert Actions** → **Configure…**
to open a kdialog menu where you can set:

- **Default audio format** — the target format used when converting audio
  files from the smart menu. Options: MP3 (V0), OGG (Q6), FLAC, WAV, M4A
  (AAC 192k), Opus (128k), ALAC.
- **Default GIF preset** — the quality preset for video-to-GIF conversion.
  Options: Small (10 fps, 480px), Medium (15 fps, 800px), Large (24 fps,
  1280px), Source size (15 fps).
- **Imgur Client ID** — your Imgur API client ID for uploads. Register an
  application at https://api.imgur.com/oauth2/addclient to get one.

## Configuration file

Configuration is stored in `~/.config/dolphin-convert-actions/config.json`:

```json
{
  "audio_preset": "mp3",
  "video_preset": "medium",
  "imgur_client_id": ""
}
```

| Key | Type | Default | Description |
|---|---|---|---|
| `audio_preset` | string | `"mp3"` | Default audio format. One of: `mp3`, `ogg`, `flac`, `wav`, `m4a`, `opus`, `alac` |
| `video_preset` | string | `"medium"` | Default GIF preset. One of: `small`, `medium`, `large`, `source` |
| `imgur_client_id` | string | `""` | Imgur API client ID. Required for Imgur upload. |

## Service menu files

The `.desktop` files control when the context menus appear:

| File | MIME types | Purpose |
|---|---|---|
| `dolphin-convert-actions.desktop` | `image/gif;video/*;audio/*;image/png;image/jpeg;` | Smart adaptive menu |
| `dolphin-audio-converter.desktop` | `audio/*;video/*;` | Dedicated audio conversion submenu |

To modify which file types trigger the menus, edit these files and change
the `MimeType` line, then restart Dolphin (`killall dolphin`).

## Logging

The tool does not write log files. Errors are shown in kdialog dialogs and
desktop notifications. For detailed ffmpeg output, run the CLI directly
from a terminal:

```bash
dolphin-convert-actions --batch video-to-gif video.mp4
```
