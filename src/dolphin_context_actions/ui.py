import os
import shutil
import subprocess
import sys

QDBUS = next(
    (n for n in ("qdbus-qt5", "qdbus", "qdbus6") if shutil.which(n)),
    None,
)


def kdialog(*args) -> subprocess.CompletedProcess:
    try:
        r = subprocess.run(
            ["kdialog"] + list(args),
            capture_output=True,
            text=True,
            env=os.environ.copy(),
        )
    except OSError as exc:
        # Absent on headless systems; callers inspect returncode/stdout, so
        # return a failed result instead of crashing.
        print(f"kdialog unavailable: {exc}. Args: {args}", file=sys.stderr)
        return subprocess.CompletedProcess(["kdialog", *args], 1, "", str(exc))
    if r.returncode != 0 and not shutil.which("kdialog"):
        print(f"kdialog not found. Args: {args}", file=sys.stderr)
    return r


def notify(title: str, msg: str, icon: str = "document-convert"):
    # Fire and forget. notify-send blocks indefinitely when no notification
    # daemon is running, which used to stall the caller mid-operation.
    try:
        subprocess.Popen(
            ["notify-send", "-i", icon, "-a", "Context Actions", title, msg],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
    except OSError:
        pass  # notify-send not installed; notifications are advisory


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
        if not label:
            continue  # kdialog has no separators; a blank row is just a dead entry
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
    # This runs every ~0.5s from the ffmpeg progress-polling loop, so a qdbus
    # call that hangs (unresponsive D-Bus daemon) would otherwise stall that
    # loop indefinitely. Treat a timeout as "can't reach the progress UI right
    # now", not "user clicked cancel" -- return True (keep converting) rather
    # than aborting a healthy conversion over an unrelated D-Bus hiccup.
    try:
        r = subprocess.run(
            [QDBUS, service, path, "Set", "", "value", str(value)],
            capture_output=True,
            timeout=5,
        )
    except subprocess.TimeoutExpired:
        return True
    if r.returncode != 0:
        return False
    if label is not None:
        try:
            subprocess.run(
                [QDBUS, service, path, "setLabelText", label],
                capture_output=True,
                timeout=5,
            )
        except subprocess.TimeoutExpired:
            pass
    return True


def pbar_close(handle: tuple | None):
    if handle and QDBUS:
        try:
            subprocess.run([QDBUS, handle[0], handle[1], "close"], capture_output=True, timeout=5)
        except subprocess.TimeoutExpired:
            pass
