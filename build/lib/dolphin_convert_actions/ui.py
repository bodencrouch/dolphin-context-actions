import os
import shutil
import subprocess
from pathlib import Path

QDBUS = next(
    (n for n in ("qdbus-qt5", "qdbus", "qdbus6") if shutil.which(n)),
    None,
)


def kdialog(*args) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["kdialog"] + list(args),
        capture_output=True,
        text=True,
        env=os.environ.copy(),
    )


def notify(title: str, msg: str, icon: str = "document-convert"):
    subprocess.run(
        ["notify-send", "-i", icon, "-a", "Convert Actions", title, msg],
        capture_output=True,
    )


def error_dialog(title: str, msg: str):
    kdialog("--title", title, "--error", msg)


def info_dialog(title: str, msg: str, width: int = 0, height: int = 0):
    args = ["--title", title, "--msgbox", msg]
    if width > 0 and height > 0:
        args.extend(["--geometry", f"{width}x{height}"])
    kdialog(*args)


def confirm_dialog(title: str, msg: str) -> bool:
    r = kdialog("--title", title, "--yesno", msg)
    return r.returncode == 0


def menu_dialog(title: str, prompt: str, items: list[tuple[str, str]]) -> str | None:
    args = ["--title", title, "--menu", prompt]
    for key, label in items:
        args += [key, label]
    r = kdialog(*args)
    if r.returncode != 0:
        return None
    return r.stdout.strip()


def input_dialog(title: str, prompt: str, text: str = "") -> str | None:
    r = kdialog("--title", title, "--inputbox", prompt, text)
    if r.returncode != 0:
        return None
    return r.stdout.strip()


def pbar_open(title: str, label: str) -> tuple | None:
    r = kdialog("--title", title, "--progressbar", label, "100")
    parts = r.stdout.strip().split()
    if len(parts) >= 2:
        return (parts[0], parts[1])
    if len(parts) == 1:
        return (parts[0], "/")
    return None


def pbar_set(handle: tuple | None, value: int, label: str | None = None) -> bool:
    if not handle or not QDBUS:
        return True
    service, path = handle
    r = subprocess.run(
        [QDBUS, service, path, "Set", "", "value", str(value)],
        capture_output=True,
    )
    if r.returncode != 0:
        return False
    if label is not None:
        subprocess.run(
            [QDBUS, service, path, "setLabelText", label],
            capture_output=True,
        )
    return True


def pbar_close(handle: tuple | None):
    if handle and QDBUS:
        subprocess.run([QDBUS, handle[0], handle[1], "close"], capture_output=True)
