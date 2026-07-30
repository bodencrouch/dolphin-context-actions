"""Pick an ffmpeg binary that actually has the encoder a preset asks for.

ffmpeg builds ship different encoder sets. A Homebrew/linuxbrew build commonly
omits libvorbis while the distro build in /usr/bin has it, so hardcoding
"ffmpeg" made "Convert to OGG" fail with a raw "Unknown encoder" dump.
"""

import shutil
import subprocess

# Searched in order. The PATH binary wins when it can do the job.
_CANDIDATE_BINARIES = ("ffmpeg", "/usr/bin/ffmpeg", "/usr/local/bin/ffmpeg")

_encoder_cache: dict[str, frozenset[str]] = {}


def _encoders(binary: str) -> frozenset[str]:
    """Encoder names the given ffmpeg build reports."""
    if binary in _encoder_cache:
        return _encoder_cache[binary]

    names: set[str] = set()
    try:
        result = subprocess.run(
            [binary, "-hide_banner", "-encoders"],
            capture_output=True, text=True, timeout=15,
        )
        for line in result.stdout.splitlines():
            # Rows look like " A....D libmp3lame  libmp3lame MP3 ...". The
            # legend above them ("V..... = Video", "A..... = Audio", ...) has
            # the same 6-char flag-column width, so parts[1] == "=" is the
            # extra check that tells a legend line from a real encoder row.
            parts = line.split()
            if len(parts) >= 2 and len(parts[0]) == 6 and parts[1] != "=" and not line.startswith(" -"):
                names.add(parts[1])
    except Exception:
        pass

    resolved = frozenset(names)
    _encoder_cache[binary] = resolved
    return resolved


def resolve(*encoders: str) -> str | None:
    """Return an ffmpeg path providing every encoder, or None if none does."""
    wanted = {e for e in encoders if e}
    for binary in _CANDIDATE_BINARIES:
        path = shutil.which(binary)
        if not path:
            continue
        if wanted <= _encoders(path):
            return path
    return None


def missing_encoder_message(label: str, *encoders: str) -> str:
    """Explain which encoder is unavailable and how to get it."""
    names = ", ".join(f"<b>{e}</b>" for e in encoders if e)
    return (
        f"Cannot convert to {label}.\n\n"
        f"No ffmpeg on this system provides: {names}\n\n"
        "Install an ffmpeg build that includes it:\n"
        "• <tt>sudo dnf install ffmpeg</tt> (Fedora)\n"
        "• <tt>sudo apt install ffmpeg</tt> (Debian/Ubuntu/Mint)\n"
        "• <tt>sudo pacman -S ffmpeg</tt> (Arch/Manjaro)"
    )


def clear_cache():
    """Forget probed encoder sets. Used by tests."""
    _encoder_cache.clear()
