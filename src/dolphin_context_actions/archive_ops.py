"""7-Zip-shaped operations for the Archive context menu.

Menu labels, when items appear, extract-to folder names, and the
quick .7z/.zip targets follow 7-Zip's Explorer extension (ContextMenu.cpp
in 7-Zip 25+). The Ark app is used where 7-Zip would open a dialog (open,
extract files, add to archive). 7z does Extract Here / Extract to / Test /
the one-click compress items / CRC SHA, including -spe for 7-Zip's
"eliminate duplication of root folder" on Extract to.
"""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path

from . import ui

# 7-Zip only offers Open/Extract/Test when every selected name is outside
# this list. Same words, same spacing, as kExtractExcludeExtensions.
EXTRACT_EXCLUDE_EXTENSIONS = frozenset(
    """
    3gp aac ans ape asc asm asp aspx avi awk
    bas bat bmp
    c cs cls clw cmd cpp csproj css ctl cxx
    def dep dlg dsp dsw
    eps
    f f77 f90 f95 fla flac frm
    gif
    h hpp hta htm html hxx
    ico idl inc ini inl
    java jpeg jpg js
    la lnk log
    mak manifest wmv mov mp3 mp4 mpe mpeg mpg m4a
    ofr ogg
    pac pas pdf php php3 php4 php5 phptml pl pm png ps py pyo
    ra rb rc reg rka rm rtf
    sed sh shn shtml sln sql srt swa
    tcl tex tiff tta txt
    vb vcproj vbs
    mkv wav webm wma wv
    xml xsd xsl xslt
    """.split()
)

_SPLIT_ARC_EXTS = frozenset({"7z", "bz2", "gz", "rar", "zip"})
_NUMBERED_ARC_EXTS = ("7z", "zip", "tar", "wim")
_HASH_SIDECAR_EXT = "sha256"

HASH_METHODS = {
    "crc32": "CRC32",
    "crc64": "CRC64",
    "xxh64": "XXH64",
    "md5": "MD5",
    "sha1": "SHA1",
    "sha256": "SHA256",
    "sha384": "SHA384",
    "sha512": "SHA512",
    "sha3-256": "SHA3-256",
    "blake2sp": "BLAKE2sp",
    "all": "*",
}

ARK_ACTIONS = (
    "open",
    "extract",
    "extract-here",
    "extract-to",
    "test",
    "compress",
    "compress-email",
    "compress-to-7z",
    "compress-to-7z-email",
    "compress-to-zip",
    "compress-to-zip-email",
    *(f"hash-{key}" for key in HASH_METHODS),
    "hash-generate-sha256",
    "hash-test",
)


def _correct_fs_name(name: str) -> str:
    return name.replace("/", "_").replace("\x00", "_") or "Archive"


def _extension(name: str) -> str:
    dot = name.rfind(".")
    if dot < 0 or dot == len(name) - 1:
        return ""
    return name[dot + 1 :]


def needs_extract(name: str) -> bool:
    ext = _extension(name)
    if not ext or len(ext) > 32:
        return True
    return ext.lower() not in EXTRACT_EXCLUDE_EXTENSIONS


def extract_subfolder_name(arc_name: str) -> str:
    """Folder 7-Zip would use for 'Extract to \"name\\\"'."""
    dot = arc_name.rfind(".")
    if dot < 0:
        return _correct_fs_name(arc_name) + "~"

    ext = arc_name[dot + 1 :]
    res = arc_name[:dot].rstrip()
    inner_dot = res.rfind(".")
    if inner_dot > 0:
        ext2 = res[inner_dot + 1 :]
        part = ext2.lower()
        if (ext.lower() == "001" and ext2.lower() in _SPLIT_ARC_EXTS) or (
            ext.lower() == "rar"
            and part in {"part001", "part01", "part1"}
        ):
            res = res[:inner_dot].rstrip()
    return _correct_fs_name(res)


def reduce_string(text: str, max_size: int = 64) -> str:
    if len(text) <= max_size:
        return text
    half = max_size // 2
    return text[:half] + " ... " + text[len(text) - half :]


def quoted_reduced(name: str) -> str:
    return '"' + reduce_string(name).replace("&", "&&") + '"'


def _first_fileinfo_name(path: Path) -> tuple[str, bool]:
    return path.name, path.is_dir()


def create_archive_name(paths: list[Path], *, is_hash: bool = False) -> str:
    """Stem 7-Zip would feed to 'Add to \"name.7z\"' / the .sha256 sidecar."""
    if not paths:
        return "Archive"

    keep_name = is_hash
    name = "Archive"
    fi_name: str | None = None
    fi_is_dir = False

    if len(paths) == 1:
        fi_name, fi_is_dir = _first_fileinfo_name(paths[0])
    else:
        parent = paths[0].parent
        if parent.name:
            name = parent.name
        elif str(parent) not in {".", ""}:
            name = parent.name or "Archive"

    if fi_name is not None:
        name = fi_name
        if not fi_is_dir and not keep_name:
            dot = name.find(".")
            if dot > 0 and name.find(".", dot + 1) < 0:
                name = name[:dot]

    name = _correct_fs_name(name)
    numbered_exts = (_HASH_SIDECAR_EXT,) if is_hash else _NUMBERED_ARC_EXTS
    if _needs_numeric_suffix(paths, name, numbered_exts):
        name = f"{name}_2"
    return name


def _needs_numeric_suffix(paths: list[Path], name: str, exts: tuple[str, ...]) -> bool:
    """True when a selected item is already name.7z / name.zip / name.sha256.

    7-Zip then shows name_2. The full scanner also walks name_3, name_4, …;
    colliding with the un-numbered file is the case that actually shows up.
    """
    prefixes = {f"{name}.{ext}".lower() for ext in exts}
    return any(path.name.lower() in prefixes for path in paths)


def destination_dir(paths: list[Path]) -> Path:
    # 7-Zip's folderPrefix is the parent of the first selected item, even
    # when that item is a directory: right-click Photos → Add to Photos.7z
    # next to Photos, not inside it.
    return paths[0].parent


def relative_to_destination(paths: list[Path]) -> tuple[Path, list[str]]:
    dest = destination_dir(paths)
    names: list[str] = []
    for path in paths:
        try:
            names.append(str(path.resolve().relative_to(dest.resolve())))
        except ValueError:
            names.append(str(path.resolve()))
    return dest, names


def selection_wants_extract(paths: list[Path]) -> bool:
    if not paths:
        return False
    if any(path.is_dir() for path in paths):
        return False
    return all(needs_extract(path.name) for path in paths)


def find_7z() -> str | None:
    return shutil.which("7z") or shutil.which("7za")


def find_ark() -> str | None:
    return shutil.which("ark")


def find_mailer() -> str | None:
    return shutil.which("xdg-email")


def _require_7z() -> str:
    exe = find_7z()
    if not exe:
        ui.error_dialog(
            "Archive",
            "<b>7z</b> not found.\n\n"
            "The Archive menu uses 7-Zip for extract/compress/hash.\n"
            "Install it:\n"
            "• <tt>sudo dnf install 7zip</tt> (Fedora)\n"
            "• <tt>sudo apt install 7zip</tt> (Debian/Ubuntu)\n"
            "• <tt>sudo pacman -S 7zip</tt> (Arch)",
        )
        raise SystemExit(1)
    return exe


def _require_ark() -> str:
    exe = find_ark()
    if not exe:
        ui.error_dialog(
            "Archive",
            "<b>ark</b> not found.\n\nInstall the Ark archive manager.",
        )
        raise SystemExit(1)
    return exe


def _run_7z(args: list[str], *, cwd: Path | None = None) -> subprocess.CompletedProcess:
    exe = _require_7z()
    return subprocess.run(
        [exe, *args],
        cwd=cwd,
        capture_output=True,
        text=True,
    )


def _7z_ok(result: subprocess.CompletedProcess) -> bool:
    # 0 = ok, 1 = warning (e.g. files skipped). Same as 7-Zip's own GUI.
    return result.returncode in (0, 1)


def _show_7z_failure(title: str, result: subprocess.CompletedProcess) -> None:
    detail = (result.stderr or result.stdout or "").strip() or f"7z exited {result.returncode}"
    ui.error_dialog(title, detail)


def _show_text(title: str, text: str) -> None:
    body = text.strip() or "(no output)"
    if len(body) < 1200:
        ui.info_dialog(title, body, width=640, height=400)
        return
    handle = tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False, encoding="utf-8")
    try:
        handle.write(body)
        handle.close()
        ui.kdialog("--title", title, "--textbox", handle.name, "80", "24")
    finally:
        Path(handle.name).unlink(missing_ok=True)


def _start_detached(executable: str, args: list[str]) -> None:
    try:
        subprocess.Popen(
            [executable, *args],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
    except OSError as exc:
        ui.error_dialog("Archive", f"Cannot start {executable}: {exc}")
        raise SystemExit(1) from exc


def _email_attachment(archive: Path) -> None:
    mailer = find_mailer()
    if not mailer:
        ui.error_dialog(
            "Archive",
            "No mailer found (xdg-email). The archive was created:\n"
            f"<tt>{archive}</tt>",
        )
        raise SystemExit(1)
    result = subprocess.run(
        [mailer, "--attach", str(archive)],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "xdg-email failed").strip()
        ui.error_dialog("Archive", detail)
        raise SystemExit(1)


def open_archive(paths: list[Path]) -> None:
    if len(paths) != 1 or paths[0].is_dir():
        ui.error_dialog("Archive", "Open archive needs exactly one file.")
        raise SystemExit(1)
    _start_detached(_require_ark(), [str(paths[0])])


def extract_files(paths: list[Path]) -> None:
    if not paths:
        return
    dest = destination_dir(paths)
    if len(paths) == 1:
        dest = dest / extract_subfolder_name(paths[0].name)
    _start_detached(
        _require_ark(),
        ["--batch", "--dialog", "--destination", str(dest), *[str(p) for p in paths]],
    )


def extract_here(paths: list[Path]) -> None:
    dest = destination_dir(paths)
    _extract_with_7z(paths, dest, elim_dup=False, title="Extract Here")


def extract_to(paths: list[Path]) -> None:
    dest_root = destination_dir(paths)
    if len(paths) == 1:
        dest = dest_root / extract_subfolder_name(paths[0].name)
        _extract_with_7z(paths, dest, elim_dup=True, title="Extract to")
        return
    # 7-Zip's "Extract to *\" on a multi-selection unpacks each archive into
    # its own named folder, not a literal directory called *.
    errors: list[str] = []
    for path in paths:
        dest = dest_root / extract_subfolder_name(path.name)
        try:
            _extract_with_7z([path], dest, elim_dup=True, title="Extract to")
        except SystemExit:
            errors.append(path.name)
    if errors:
        raise SystemExit(1)
    ui.notify("Archive", f"Extracted {len(paths)} archive(s)", "ark")


def _extract_with_7z(
    paths: list[Path],
    dest: Path,
    *,
    elim_dup: bool,
    title: str,
) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    args = ["x", "-y"]
    if elim_dup:
        args.append("-spe")
    args.append(f"-o{dest}")
    args.append("--")
    args.extend(str(path) for path in paths)
    result = _run_7z(args)
    if not _7z_ok(result):
        _show_7z_failure(title, result)
        raise SystemExit(1)
    ui.notify("Archive", f"Extracted to {dest}", "ark")


def test_archive(paths: list[Path]) -> None:
    result = _run_7z(["t", "--", *[str(p) for p in paths]])
    output = (result.stdout or "") + (result.stderr or "")
    if not _7z_ok(result):
        _show_7z_failure("Test archive", result)
        raise SystemExit(1)
    _show_text("Test archive", output)


def add_to_archive(paths: list[Path]) -> None:
    dest, names = relative_to_destination(paths)
    _start_detached(
        _require_ark(),
        ["--add", "--changetofirstpath", *[str(dest / name) for name in names]],
    )


def _compress_to(paths: list[Path], suffix: str, arc_type: str) -> Path:
    dest, names = relative_to_destination(paths)
    archive = dest / f"{create_archive_name(paths)}{suffix}"
    result = _run_7z(
        ["a", f"-t{arc_type}", "--", str(archive), *names],
        cwd=dest,
    )
    if not _7z_ok(result):
        _show_7z_failure(f"Add to {archive.name}", result)
        raise SystemExit(1)
    return archive


def compress_to_7z(paths: list[Path]) -> None:
    archive = _compress_to(paths, ".7z", "7z")
    ui.notify("Archive", f"Created {archive.name}", "ark")


def compress_to_zip(paths: list[Path]) -> None:
    archive = _compress_to(paths, ".zip", "zip")
    ui.notify("Archive", f"Created {archive.name}", "ark")


def compress_to_7z_email(paths: list[Path]) -> None:
    _email_attachment(_compress_to(paths, ".7z", "7z"))


def compress_to_zip_email(paths: list[Path]) -> None:
    _email_attachment(_compress_to(paths, ".zip", "zip"))


def compress_and_email(paths: list[Path]) -> None:
    dest = destination_dir(paths)
    suggested = dest / f"{create_archive_name(paths)}.7z"
    chosen = ui.kdialog(
        "--title",
        "Compress and email",
        "--getsavefilename",
        str(suggested),
        "7-Zip (*.7z)|ZIP (*.zip)",
    )
    if chosen.returncode != 0:
        return
    archive = Path(chosen.stdout.strip())
    if not archive.name:
        return
    suffix = archive.suffix.lower()
    arc_type = "zip" if suffix == ".zip" else "7z"
    if suffix not in {".7z", ".zip"}:
        archive = archive.with_suffix(".7z")
        arc_type = "7z"
    dest_dir, names = relative_to_destination(paths)
    result = _run_7z(
        ["a", f"-t{arc_type}", "--", str(archive), *names],
        cwd=dest_dir,
    )
    if not _7z_ok(result):
        _show_7z_failure("Compress and email", result)
        raise SystemExit(1)
    _email_attachment(archive)


def hash_files(paths: list[Path], method_key: str) -> None:
    method = HASH_METHODS[method_key]
    dest, names = relative_to_destination(paths)
    args = ["h", f"-scrc{method}"]
    if any(path.is_dir() for path in paths):
        args.append("-r")
    args.append("--")
    args.extend(names)
    result = _run_7z(args, cwd=dest)
    output = (result.stdout or "") + (result.stderr or "")
    if not _7z_ok(result):
        _show_7z_failure("CRC SHA", result)
        raise SystemExit(1)
    _show_text(f"CRC SHA — {method}", output)


def generate_sha256(paths: list[Path]) -> None:
    dest, names = relative_to_destination(paths)
    sidecar = dest / f"{create_archive_name(paths, is_hash=True)}.sha256"
    args = ["h", "-scrcSHA256", "-ba"]
    if any(path.is_dir() for path in paths):
        args.append("-r")
    args.append("--")
    args.extend(names)
    result = _run_7z(args, cwd=dest)
    if not _7z_ok(result):
        _show_7z_failure("SHA-256 -> file.sha256", result)
        raise SystemExit(1)
    # 7z -ba prints: <hash>  <size>  <name>  (or similar). Keep the raw lines
    # so `7z t` / sha256sum -c still have something recognizable to chew on.
    sidecar.write_text(result.stdout, encoding="utf-8")
    ui.notify("Archive", f"Wrote {sidecar.name}", "ark")


def test_checksum(paths: list[Path]) -> None:
    hash_files_selected = [p for p in paths if p.suffix.lower() in {".sha256", ".sha1", ".md5", ".sfv"}]
    if hash_files_selected:
        dest, names = relative_to_destination(hash_files_selected)
        result = _run_7z(["t", "--", *names], cwd=dest)
        output = (result.stdout or "") + (result.stderr or "")
        if not _7z_ok(result):
            _show_7z_failure("Test archive : Checksum", result)
            raise SystemExit(1)
        _show_text("Test archive : Checksum", output)
        return
    test_archive(paths)


def run_action(action: str, files: list[str]) -> int:
    paths = [Path(raw).expanduser() for raw in files]
    paths = [p for p in paths if p.exists() or p.is_symlink()]
    if not paths:
        ui.error_dialog("Archive", "No local files or folders were selected.")
        return 1

    dispatch = {
        "open": open_archive,
        "extract": extract_files,
        "extract-here": extract_here,
        "extract-to": extract_to,
        "test": test_archive,
        "compress": add_to_archive,
        "compress-email": compress_and_email,
        "compress-to-7z": compress_to_7z,
        "compress-to-zip": compress_to_zip,
        "compress-to-7z-email": compress_to_7z_email,
        "compress-to-zip-email": compress_to_zip_email,
        "hash-generate-sha256": generate_sha256,
        "hash-test": test_checksum,
    }
    if action.startswith("hash-") and action[5:] in HASH_METHODS:
        hash_files(paths, action[5:])
        return 0

    handler = dispatch.get(action)
    if handler is None:
        ui.error_dialog("Archive", f"Unknown Archive action: {action}")
        return 1
    handler(paths)
    return 0
