import json
import os
import shutil
import subprocess
from pathlib import Path

from . import config, ui


def imgur_upload(files: list[str]):
    cfg = config.load()
    client_id = cfg.get("imgur_client_id", "")

    if not client_id:
        client_id = ui.input_dialog(
            "Imgur Upload",
            "Enter your Imgur Client ID (register at https://api.imgur.com/oauth2/addclient):",
        )
        if not client_id:
            return
        if ui.confirm_dialog("Imgur Upload", "Save this Client ID for future uploads?"):
            cfg["imgur_client_id"] = client_id
            config.save(cfg)

    total = len(files)
    successes = []
    errors = []
    handle = ui.pbar_open("Upload to Imgur", f"Starting… (0 of {total})")

    for idx, filepath in enumerate(files):
        path = Path(filepath)
        if not path.exists():
            errors.append(f"File not found: {filepath}")
            continue

        short = path.name[:50]
        label = f"[{idx + 1}/{total}] {short}"
        ui.pbar_set(handle, int(idx / total * 100), f"Uploading: {label}")

        try:
            url = _imgur_upload_single(str(path), client_id)
            if url:
                successes.append((path.name, url))
            else:
                errors.append(f"{path.name}: upload failed (check client ID)")
        except Exception as e:
            errors.append(f"{path.name}: {e}")

    ui.pbar_close(handle)

    if successes:
        text = "\n".join(f"• {name}: {url}" for name, url in successes)
        ui.info_dialog("Imgur Upload - Done", f"Uploaded {len(successes)} file(s):\n\n{text}")
        ui.notify("Imgur Upload - Done", f"✔ {len(successes)} file{'s' if len(successes) != 1 else ''} uploaded", "internet-web-browser")
        for _, url in successes:
            _copy_to_clipboard(url)
    if errors:
        preview = "\n\n".join(errors[:3])
        if len(errors) > 3:
            preview += f"\n\n…and {len(errors) - 3} more"
        ui.error_dialog("Imgur Upload - Errors", preview)


def _imgur_upload_single(filepath: str, client_id: str) -> str | None:
    import http.client
    import mimetypes

    boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW"

    with open(filepath, "rb") as f:
        file_data = f.read()

    filename = os.path.basename(filepath)
    content_type, _ = mimetypes.guess_type(filepath)
    if content_type is None:
        content_type = "application/octet-stream"

    body_parts = [
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="image"; filename="{filename}"\r\n'
        f"Content-Type: {content_type}\r\n\r\n".encode(),
        file_data,
        f"\r\n--{boundary}--\r\n".encode(),
    ]
    body = b"".join(body_parts)

    conn = http.client.HTTPSConnection("api.imgur.com")
    conn.request(
        "POST",
        "/3/image",
        body=body,
        headers={
            "Authorization": f"Client-ID {client_id}",
            "Content-Type": f"multipart/form-data; boundary={boundary}",
        },
    )
    resp = conn.getresponse()
    data = json.loads(resp.read().decode())
    conn.close()

    if resp.status == 200 and data.get("success"):
        return data["data"]["link"]
    return None


def _copy_to_clipboard(text: str):
    for tool in ("wl-copy", "xclip", "xsel"):
        if shutil.which(tool):
            try:
                if tool == "wl-copy":
                    subprocess.run(["wl-copy"], input=text.encode(), check=False)
                elif tool == "xclip":
                    subprocess.run(["xclip", "-selection", "clipboard"], input=text.encode(), check=False)
                elif tool == "xsel":
                    subprocess.run(["xsel", "--clipboard", "--input"], input=text.encode(), check=False)
            except Exception:
                pass
            break
