# Configuration

## Interactive configuration

Right-click any media file in Dolphin → **Context Actions** → **Configure…**
to open a kdialog menu where you can set:

- **Default audio format** — used by `dolphin-context-actions --batch audio`
  when it's run without an explicit `--format`. Options: MP3 (V0), OGG (Q6),
  FLAC, WAV, M4A (AAC 192k), Opus (128k), ALAC.
- **Default GIF preset** — used by `dolphin-context-actions --batch
  video-to-gif` when it's run without an explicit `--format`. Options: Small
  (10 fps, 480px), Medium (15 fps, 800px), Large (24 fps, 1280px), Source
  size (15 fps).
- **Imgur Client ID** — your Imgur API client ID for uploads. Register an
  application at https://api.imgur.com/oauth2/addclient to get one.

Every shipped `.desktop` menu entry already passes an explicit `--format`,
so the two defaults above don't currently change what any menu item does;
they take effect for a bare `--batch audio` / `--batch video-to-gif`
invocation, e.g. from a terminal or a custom `.desktop` action you add
yourself. The interactive smart menu (right-click a file → **Context
Actions**) always shows its own format picker regardless of these defaults.

## Configuration file

Configuration is stored in `~/.config/dolphin-context-actions/config.json`:

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
| `dolphin-context-actions.desktop` | `image/gif;video/*;audio/*;image/png;image/jpeg;` | Smart adaptive menu |
| `dolphin-audio-converter.desktop` | `audio/*;video/*;` | Dedicated audio conversion submenu |

To modify which file types trigger the menus, edit these files and change
the `MimeType` line, then restart Dolphin (`killall dolphin`).

## Logging

The tool does not write log files. Errors are shown in kdialog dialogs and
desktop notifications. For detailed ffmpeg output, run the CLI directly
from a terminal:

```bash
dolphin-context-actions --batch video-to-gif video.mp4
```
