"""Shared helpers for the media converters."""

from pathlib import Path


def unique_output(destination: Path) -> Path:
    """Return a free path so a conversion never overwrites an existing file.

    Converting song.wav to MP3 in a folder that already holds song.mp3 used to
    destroy the existing file, because ffmpeg runs with -y. Matches the
    auto-rename behaviour of the link operations.
    """
    if not destination.exists() and not destination.is_symlink():
        return destination

    stem = destination.stem
    suffix = destination.suffix
    parent = destination.parent

    counter = 2
    while True:
        candidate = parent / f"{stem} ({counter}){suffix}"
        if not candidate.exists() and not candidate.is_symlink():
            return candidate
        counter += 1
