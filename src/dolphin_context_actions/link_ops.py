"""
Link Shell Extension-like functionality for KDE Dolphin.

This module implements hardlink, symlink, and clone operations similar to
Hermann Schinagl's Link Shell Extension for Windows, adapted for Linux/KDE.

Supported features:
- Pick Link Source (stores files/folders as source for link creation)
- Drop As: Hardlink, Symlink, Hardlink Clone, Symlink Clone, Smart Copy
- Cancel Link Creation
- Auto Rename (when dropping in same directory)
- Smart Copy (preserves hardlink/symlink structure)
- Enumerate Hardlinks (find all hardlink siblings)
- Link Properties (show link information)

NOT supported on Linux:
- Junctions (NTFS-specific directory hardlinks)
- Volume Mountpoints (Windows-specific)
- Some NTFS-specific features like reparse points
"""

import json
import os
import shutil
from pathlib import Path

from . import ui

# Storage for picked link sources (in memory + persistent storage)
_PICKED_SOURCES_FILE = Path.home() / ".cache" / "dolphin-link-extension" / "picked_sources.json"


def _ensure_cache_dir():
    """Ensure the cache directory exists."""
    _PICKED_SOURCES_FILE.parent.mkdir(parents=True, exist_ok=True)


def _save_picked_sources(sources: list[str]):
    """Save picked sources to persistent storage."""
    _ensure_cache_dir()
    with open(_PICKED_SOURCES_FILE, "w") as f:
        json.dump({"sources": sources}, f)


def _load_picked_sources() -> list[str]:
    """Load picked sources from persistent storage."""
    _ensure_cache_dir()
    if _PICKED_SOURCES_FILE.exists():
        try:
            with open(_PICKED_SOURCES_FILE, "r") as f:
                data = json.load(f)
                return data.get("sources", [])
        except (OSError, json.JSONDecodeError):
            pass
    return []


def _clear_picked_sources():
    """Clear picked sources from storage."""
    if _PICKED_SOURCES_FILE.exists():
        _PICKED_SOURCES_FILE.unlink()


class LinkSource:
    """Represents picked link sources."""
    
    def __init__(self):
        self._sources: list[str] = []
        self._loaded = False
    
    def load(self):
        """Load sources from storage."""
        if not self._loaded:
            self._sources = _load_picked_sources()
            self._loaded = True
        return self._sources
    
    def save(self, sources: list[str]):
        """Save sources to storage."""
        self._sources = sources
        _save_picked_sources(sources)
    
    def pick(self, paths: list[str]):
        """Pick new sources (replaces existing).

        Absolute but not resolved: resolve() follows symlinks, so picking a
        symlink stored its target instead, and the drop then created a link to
        the target under the target's name. The KIO plugin stores the selected
        path as-is, and these two must agree.
        """
        absolute = [os.path.abspath(p) for p in paths]
        self.save(absolute)
        return absolute

    def get(self) -> list[str]:
        """Get current picked sources."""
        return self.load()
    
    def clear(self):
        """Clear picked sources."""
        self._sources = []
        _clear_picked_sources()
    
    def has_sources(self) -> bool:
        """Check if there are picked sources."""
        return len(self.get()) > 0


# Global source manager
source_manager = LinkSource()


def pick_link_source(paths: list[str]):
    if not paths:
        return False
    
    # Validate paths
    valid_paths = []
    for p in paths:
        path = Path(p)
        # A broken symlink is still a valid thing to link to, and the KIO plugin
        # accepts it, so exists()-alone would disagree with the context menu.
        if not path.exists() and not path.is_symlink():
            ui.error_dialog("Pick Link Source", f"Path does not exist: {p}")
            return False
        valid_paths.append(str(path))
    
    # Store sources
    source_manager.pick(valid_paths)
    ui.notify("Link Source Picked", f"Picked {len(valid_paths)} item(s) as link source", "edit-link")
    return True


def cancel_link_creation():
    """Cancel the current pick operation."""
    if source_manager.has_sources():
        source_manager.clear()
        ui.notify("Link Creation Cancelled", "Pick operation cancelled", "edit-delete")
        return True
    else:
        ui.info_dialog("Cancel Link Creation", "No active pick operation to cancel.")
        return False


# 50 collisions on one name in one directory is already absurd; treat it as a
# stuck loop rather than retrying forever. Kept in sync with the same bound in
# the KIO plugin's elevated path (tryElevatedAttempt in
# kio-plugin/dolphinlinkfileitemaction.cpp).
_MAX_RENAME_ATTEMPTS = 50


def _candidate_leaf_name(source_name: str, kind: str, attempt: int, split_extension: bool) -> str:
    """Return the Link Shell Extension-style name for the nth collision.

    attempt 0 is the source name unchanged, 1 is "name - Hardlink.ext", and
    every attempt after that appends " (2)", " (3)", ... before the extension.
    Directories keep their whole name (a folder called "v1.2" must not become
    "v1 - Symlink.2"), so callers pass split_extension=False for those.

    Kept in sync with candidateLeafName() in the KIO plugin, so a collision
    looks the same whether or not the drop needed administrator rights.
    """
    if attempt == 0:
        return source_name

    dot = source_name.rfind(".")
    if split_extension and dot > 0:
        stem, suffix = source_name[:dot], source_name[dot:]
    else:
        stem, suffix = source_name, ""

    if attempt == 1:
        return f"{stem} - {kind}{suffix}"
    return f"{stem} - {kind} ({attempt}){suffix}"


def _splits_extension(source: Path) -> bool:
    """Whether a rename of this source should keep a trailing ".ext" last.

    Directories keep their whole name, so "v1.2" stays "v1.2 - Symlink" rather
    than becoming "v1 - Symlink.2".
    """
    return not source.is_dir()


def _create_with_rename(parent: Path, source_name: str, kind: str, split_extension: bool, create) -> Path:
    """Call create(destination), stepping to the next free name on collision.

    create() must claim the name atomically and raise FileExistsError if it is
    taken -- os.symlink, os.link and os.mkdir all do. Checking exists() first
    and creating second leaves a window where something else takes the name in
    between, which is exactly the EEXIST failure this replaces.
    """
    for attempt in range(_MAX_RENAME_ATTEMPTS + 1):
        destination = parent / _candidate_leaf_name(source_name, kind, attempt, split_extension)
        try:
            create(destination)
            return destination
        except FileExistsError:
            continue
    raise OSError(f"Too many name collisions in {parent} for {source_name}.")


def _claim_directory(parent: Path, source_name: str, kind: str, split_extension: bool = False) -> Path:
    """Create a new directory under parent, auto-renaming if the name is taken.

    Returns the directory actually created, which may carry a
    " - Hardlink Clone (2)" style suffix.
    """
    return _create_with_rename(parent, source_name, kind, split_extension, os.mkdir)


def _claim_file(parent: Path, source_name: str, kind: str, split_extension: bool = True) -> Path:
    """Create a new empty file under parent, auto-renaming if the name is taken."""

    def create(path: Path):
        os.close(os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600))

    return _create_with_rename(parent, source_name, kind, split_extension, create)


def drop_hardlink(target_dir: str, relative_to: str | None = None):
    """
    Drop hardlinks of picked sources into target directory.
    
    Args:
        target_dir: Destination directory path
        relative_to: If provided, create links relative to this path
    """
    sources = source_manager.get()
    if not sources:
        ui.error_dialog("Drop Hardlink", "No link source picked. Pick a source first.")
        return False
    
    target_path = Path(target_dir)
    if not target_path.exists() or not target_path.is_dir():
        ui.error_dialog("Drop Hardlink", f"Target is not a valid directory: {target_dir}")
        return False
    
    if not os.access(target_dir, os.W_OK):
        ui.error_dialog("Drop Hardlink", f"No write permission in: {target_dir}")
        return False
    
    created = []
    failed = []
    
    for source in sources:
        source_path = Path(source)
        # is_file() follows symlinks, so a symlink source would otherwise
        # silently hardlink to whatever it points at. The KAuth helper used
        # for the elevated path rejects symlink sources outright (lstat +
        # S_ISREG); match that here so the same source produces the same
        # behavior whether or not the destination needs elevation.
        is_real_file = source_path.is_file() and not source_path.is_symlink()

        try:
            if source_path.is_symlink():
                raise OSError("Symlinks cannot be hardlinked. Use Drop Symlink.")
            if not is_real_file:
                raise OSError("Directories cannot be hardlinked. Use Drop Symlink.")
            # A name already taken in the destination is not a failure: the
            # link lands as "name - Hardlink.ext", then "name - Hardlink (2).ext".
            dest_path = _create_with_rename(
                target_path,
                source_path.name,
                "Hardlink",
                _splits_extension(source_path),
                lambda destination: os.link(source, str(destination), follow_symlinks=False),
            )
            created.append(str(dest_path))
        except Exception as e:
            failed.append((source, str(e)))
    
    # Report results
    if created:
        if len(created) == 1:
            ui.notify("Hardlink Created", f"Created: {created[0]}", "edit-link")
        else:
            ui.notify("Hardlinks Created", f"Created {len(created)} hardlink(s)", "edit-link")
    
    if failed:
        error_msgs = [f"{src}: {err}" for src, err in failed]
        ui.error_dialog("Hardlink Errors", "\n".join(error_msgs))
    
    if failed:
        source_manager.save([source for source, _ in failed])
    else:
        source_manager.clear()
    return len(created) > 0


def drop_symlink(target_dir: str, relative: bool = True):
    """
    Drop symbolic links of picked sources into target directory.
    
    Args:
        target_dir: Destination directory path
        relative: Create relative symlinks if possible
    """
    sources = source_manager.get()
    if not sources:
        ui.error_dialog("Drop Symlink", "No link source picked. Pick a source first.")
        return False
    
    target_path = Path(target_dir)
    if not target_path.exists() or not target_path.is_dir():
        ui.error_dialog("Drop Symlink", f"Target is not a valid directory: {target_dir}")
        return False
    
    if not os.access(target_dir, os.W_OK):
        ui.error_dialog("Drop Symlink", f"No write permission in: {target_dir}")
        return False
    
    created = []
    failed = []
    
    target_dir_abs = str(target_path.resolve())
    
    for source in sources:
        source_path = Path(source)

        if relative:
            # Resolve the containing directory so ".." hops in the relative
            # path are correct, but keep the leaf name unresolved so a
            # picked symlink links to itself rather than to its target.
            source_abs = os.path.join(
                os.path.realpath(source_path.parent), source_path.name
            )
            link_value = os.path.relpath(source_abs, target_dir_abs)
        else:
            link_value = source

        try:
            # A name already taken in the destination is not a failure: the
            # link lands as "name - Symlink.ext", then "name - Symlink (2).ext".
            dest_path = _create_with_rename(
                target_path,
                source_path.name,
                "Symlink",
                _splits_extension(source_path),
                lambda destination: os.symlink(link_value, str(destination)),
            )
            created.append(str(dest_path))
        except Exception as e:
            failed.append((source, str(e)))
    
    # Report results
    if created:
        if len(created) == 1:
            ui.notify("Symlink Created", f"Created: {created[0]}", "edit-link")
        else:
            ui.notify("Symlinks Created", f"Created {len(created)} symlink(s)", "edit-link")
    
    if failed:
        error_msgs = [f"{src}: {err}" for src, err in failed]
        ui.error_dialog("Symlink Errors", "\n".join(error_msgs))
    
    if failed:
        source_manager.save([source for source, _ in failed])
    else:
        source_manager.clear()
    return len(created) > 0


def hardlink_clone(source_dir: str, target_dir: str):
    """
    Create a hardlink clone of a directory tree.
    
    Creates a copy of the directory structure where files are hardlinked
    to the original files instead of being copied.
    
    Args:
        source_dir: Source directory to clone
        target_dir: Destination directory
    """
    # Absolute so the "is this the clone I'm writing?" check below compares
    # like with like, whatever form the caller passed these in.
    source_path = Path(os.path.abspath(source_dir))
    target_path = Path(os.path.abspath(target_dir))

    if not source_path.exists() or not source_path.is_dir():
        ui.error_dialog("Hardlink Clone", f"Source is not a valid directory: {source_dir}")
        return False
    
    if not os.access(target_path.parent, os.W_OK):
        ui.error_dialog("Hardlink Clone", f"No write permission in: {target_path.parent}")
        return False

    # A taken name is not a failure: the clone lands as "name - Hardlink Clone",
    # then "name - Hardlink Clone (2)". Claiming the directory with mkdir is
    # also what reserves the name, so two clones started at once can't collide.
    try:
        clone_root = _claim_directory(target_path.parent, target_path.name, "Hardlink Clone")
    except OSError as error:
        ui.error_dialog("Hardlink Clone", f"Clone failed: {error}")
        return False

    created_count = {"files": 0, "dirs": 0, "symlinks": 0}
    try:
        for root, dirs, files in os.walk(source_path, followlinks=False):
            source_root = Path(root)
            target_root = clone_root / source_root.relative_to(source_path)
            target_root.mkdir(parents=True, exist_ok=True)

            for dirname in list(dirs):
                source_item = source_root / dirname
                target_item = target_root / dirname
                # Cloning a folder into itself would otherwise walk into the
                # clone being written and recurse without end.
                if source_item == clone_root:
                    dirs.remove(dirname)
                    continue
                if source_item.is_symlink():
                    os.symlink(os.readlink(source_item), target_item)
                    dirs.remove(dirname)
                    created_count["symlinks"] += 1
                else:
                    target_item.mkdir(exist_ok=True)
                    created_count["dirs"] += 1

            for filename in files:
                source_item = source_root / filename
                target_item = target_root / filename
                if source_item.is_symlink():
                    os.symlink(os.readlink(source_item), target_item)
                    created_count["symlinks"] += 1
                else:
                    os.link(source_item, target_item)
                    created_count["files"] += 1
    except Exception as error:
        shutil.rmtree(clone_root, ignore_errors=True)
        ui.error_dialog("Hardlink Clone", f"Clone failed: {error}")
        return False

    msg_parts = []
    if created_count["dirs"] > 0:
        msg_parts.append(f"{created_count['dirs']} directories")
    if created_count["files"] > 0:
        msg_parts.append(f"{created_count['files']} hardlinks")
    if created_count["symlinks"] > 0:
        msg_parts.append(f"{created_count['symlinks']} symlinks")

    ui.notify("Hardlink Clone Created", f"{clone_root.name}: {', '.join(msg_parts)}", "folder")
    return True


def symlink_clone(source_dir: str, target_dir: str, relative: bool = True):
    """
    Create a symbolic link clone of a directory tree.
    
    Creates a copy of the directory structure where files and directories
    are symlinked to the original locations.
    
    Args:
        source_dir: Source directory to clone
        target_dir: Destination directory
        relative: Create relative symlinks when possible
    """
    source_path = Path(os.path.abspath(source_dir))
    target_path = Path(os.path.abspath(target_dir))

    if not source_path.exists() or not source_path.is_dir():
        ui.error_dialog("Symlink Clone", f"Source is not a valid directory: {source_dir}")
        return False
    
    if not os.access(target_path.parent, os.W_OK):
        ui.error_dialog("Symlink Clone", f"No write permission in: {target_path.parent}")
        return False

    # Same rule as Hardlink Clone: a taken name renames to
    # "name - Symlink Clone", "name - Symlink Clone (2)", and so on.
    try:
        clone_root = _claim_directory(target_path.parent, target_path.name, "Symlink Clone")
    except OSError as error:
        ui.error_dialog("Symlink Clone", f"Clone failed: {error}")
        return False

    created_count = {"dirs": 0, "files": 0}
    try:
        for root, dirs, files in os.walk(source_path, followlinks=False):
            source_root = Path(root)
            target_root = clone_root / source_root.relative_to(source_path)
            target_root.mkdir(parents=True, exist_ok=True)

            for dirname in list(dirs):
                source_item = source_root / dirname
                target_item = target_root / dirname
                if source_item == clone_root:
                    dirs.remove(dirname)
                    continue
                if source_item.is_symlink():
                    link_target = os.path.relpath(source_item, target_root) if relative else source_item
                    os.symlink(link_target, target_item)
                    dirs.remove(dirname)
                    created_count["files"] += 1
                else:
                    target_item.mkdir(exist_ok=True)
                    created_count["dirs"] += 1

            for filename in files:
                source_item = source_root / filename
                target_item = target_root / filename
                link_target = os.path.relpath(source_item, target_root) if relative else source_item
                os.symlink(link_target, target_item)
                created_count["files"] += 1
    except Exception as error:
        shutil.rmtree(clone_root, ignore_errors=True)
        ui.error_dialog("Symlink Clone", f"Clone failed: {error}")
        return False

    msg_parts = []
    if created_count["dirs"] > 0:
        msg_parts.append(f"{created_count['dirs']} directories")
    if created_count["files"] > 0:
        msg_parts.append(f"{created_count['files']} symlinks")

    ui.notify("Symlink Clone Created", f"{clone_root.name}: {', '.join(msg_parts)}", "folder")
    return True


def smart_copy(source: str, target_dir: str):
    """
    Smart copy - copies directory structure while preserving hardlink/symlink relations.
    
    This preserves inner hardlinks (saturated) and inner symlinks.
    
    Args:
        source: Source file or directory
        target_dir: Target directory
    """
    source_path = Path(os.path.abspath(source))
    target_path = Path(os.path.abspath(target_dir))

    if not source_path.exists() and not source_path.is_symlink():
        ui.error_dialog("Smart Copy", f"Source does not exist: {source}")
        return False
    
    if not target_path.exists() or not target_path.is_dir():
        ui.error_dialog("Smart Copy", f"Target is not a valid directory: {target_dir}")
        return False
    
    if not os.access(target_dir, os.W_OK):
        ui.error_dialog("Smart Copy", f"No write permission in: {target_dir}")
        return False
    
    created_count = {"files": 0, "dirs": 0, "symlinks": 0, "hardlinks": 0}
    copied_inodes: dict[tuple[int, int], Path] = {}

    def _copy_recursive(src: Path, dst: Path):
        if src.is_symlink():
            os.symlink(os.readlink(src), dst)
            created_count["symlinks"] += 1
        elif src.is_file():
            file_stat = src.stat()
            inode_key = (file_stat.st_dev, file_stat.st_ino)
            if inode_key in copied_inodes:
                os.link(copied_inodes[inode_key], dst)
                created_count["hardlinks"] += 1
            else:
                shutil.copy2(src, dst)
                copied_inodes[inode_key] = dst
                created_count["files"] += 1
        elif src.is_dir():
            dst.mkdir(parents=True)
            created_count["dirs"] += 1
            for item in src.iterdir():
                _copy_recursive(item, dst / item.name)
            shutil.copystat(src, dst, follow_symlinks=False)

    # Only the top level can collide -- everything below it goes into a
    # directory this call just created. A taken name becomes
    # "name - Smart Copy.ext", then "name - Smart Copy (2).ext".
    split_extension = _splits_extension(source_path)
    target_dest: Path | None = None
    try:
        if source_path.is_symlink():
            target_dest = _create_with_rename(
                target_path,
                source_path.name,
                "Smart Copy",
                split_extension,
                lambda destination: os.symlink(os.readlink(source_path), destination),
            )
            created_count["symlinks"] += 1
        elif source_path.is_dir():
            target_dest = _claim_directory(target_path, source_path.name, "Smart Copy")
            created_count["dirs"] += 1
            for item in source_path.iterdir():
                # Copying a folder into itself would otherwise descend into the
                # copy being written.
                if item == target_dest:
                    continue
                _copy_recursive(item, target_dest / item.name)
            shutil.copystat(source_path, target_dest, follow_symlinks=False)
        else:
            target_dest = _claim_file(target_path, source_path.name, "Smart Copy", split_extension)
            shutil.copyfile(source_path, target_dest)
            shutil.copystat(source_path, target_dest)
            file_stat = source_path.stat()
            copied_inodes[(file_stat.st_dev, file_stat.st_ino)] = target_dest
            created_count["files"] += 1
    except Exception as error:
        if target_dest is not None:
            if target_dest.is_dir() and not target_dest.is_symlink():
                shutil.rmtree(target_dest, ignore_errors=True)
            else:
                target_dest.unlink(missing_ok=True)
        ui.error_dialog("Smart Copy", f"Copy failed: {error}")
        return False

    msg_parts = []
    if created_count["dirs"] > 0:
        msg_parts.append(f"{created_count['dirs']} directories")
    if created_count["files"] > 0:
        msg_parts.append(f"{created_count['files']} files")
    if created_count["hardlinks"] > 0:
        msg_parts.append(f"{created_count['hardlinks']} hardlinks")
    if created_count["symlinks"] > 0:
        msg_parts.append(f"{created_count['symlinks']} symlinks")

    ui.notify("Smart Copy Created", f"{target_dest.name}: {', '.join(msg_parts)}", "folder")
    return True


def enumerate_hardlinks(path: str) -> list[str]:
    """
    Find all hardlink siblings of a file.
    
    Args:
        path: Path to a file
    
    Returns:
        List of paths that are hardlinked to the same inode
    """
    file_path = Path(path)
    if not file_path.exists() or not file_path.is_file():
        return []
    
    try:
        file_stat = os.stat(file_path)
        inode_key = (file_stat.st_ino, file_stat.st_dev)
        
        # Search for files with the same inode
        # We'll search from the parent directory up to root
        siblings = []
        
        # Start from parent and search
        search_root = file_path.parent
        for root, dirs, files in os.walk(str(search_root)):
            for file in files:
                filepath = Path(root) / file
                try:
                    fstat = os.stat(filepath)
                    if (fstat.st_ino, fstat.st_dev) == inode_key:
                        siblings.append(str(filepath))
                except Exception:
                    pass
            # Limit depth to avoid infinite loops with symlinks
            if len(siblings) > 100:  # Safety limit
                break
        
        return sorted(set(siblings))
    except Exception as e:
        ui.error_dialog("Enumerate Hardlinks", f"Error: {e}")
        return []


def show_hardlink_properties(path: str):
    """
    Show properties of hardlinked file.
    
    Args:
        path: Path to file
    """
    file_path = Path(path)
    # exists() follows symlinks, so a broken link would be rejected here -- the
    # very case where seeing the link target is most useful.
    if not file_path.exists() and not file_path.is_symlink():
        ui.error_dialog("Link Properties", f"File does not exist: {path}")
        return

    try:
        file_stat = os.lstat(file_path)
        
        # Find all siblings
        siblings = enumerate_hardlinks(path)
        
        # Filter out the path itself
        siblings = [s for s in siblings if s != str(file_path)]
        
        ref_count = len(siblings) + 1  # +1 for the file itself
        
        if file_path.is_symlink():
            link_type = "Symbolic Link"
            link_target = os.readlink(file_path)
            msg = (
                f"<b>Symbolic Link</b><br><br>"
                f"Target: {link_target}<br><br>"
                f"Absolute target: {Path(file_path).resolve()}"
            )
        elif file_path.is_file():
            link_type = "Hard Link" if ref_count > 1 else "Regular File"
            msg = (
                f"<b>{link_type}</b><br><br>"
                f"Reference count: {ref_count}<br><br>"
                f"Inode: {file_stat.st_ino}<br>"
                f"Device: {file_stat.st_dev}<br><br>"
            )
            if siblings:
                msg += "<b>Siblings:</b><br>"
                for s in siblings[:20]:  # Limit to 20 siblings
                    msg += f"• {s}<br>"
                if len(siblings) > 20:
                    msg += f"• ... and {len(siblings) - 20} more<br>"
        else:
            msg = f"<b>Directory</b><br><br>Path: {file_path}"
        
        ui.info_dialog("Link Properties", msg, width=600, height=400)
    except Exception as e:
        ui.error_dialog("Link Properties", f"Error: {e}")


# Drop As menu handler
def drop_as(target_dir: str, drop_type: str, relative: bool = True):
    """
    Handle Drop As menu selection.
    
    Args:
        target_dir: The directory where items are dropped
        drop_type: One of: hardlink, symlink, hardlink-clone, symlink-clone, smart-copy
        relative: Use relative paths for symlinks when possible
    """
    sources = source_manager.get()
    if not sources:
        ui.error_dialog("Drop As", "No link source picked. Pick a source first.")
        return False
    
    if drop_type == "hardlink":
        return drop_hardlink(target_dir)
    elif drop_type == "symlink":
        return drop_symlink(target_dir, relative)
    elif drop_type == "hardlink-clone":
        if len(sources) != 1:
            ui.error_dialog("Hardlink Clone", "Please pick exactly one directory for cloning.")
            return False
        source = Path(sources[0])
        success = hardlink_clone(str(source), str(Path(target_dir) / source.name))
    elif drop_type == "symlink-clone":
        if len(sources) != 1:
            ui.error_dialog("Symlink Clone", "Please pick exactly one directory for cloning.")
            return False
        source = Path(sources[0])
        success = symlink_clone(str(source), str(Path(target_dir) / source.name), relative)
    elif drop_type == "smart-copy":
        if len(sources) != 1:
            ui.error_dialog("Smart Copy", "Please pick exactly one file or directory for smart copy.")
            return False
        success = smart_copy(sources[0], target_dir)
    else:
        ui.error_dialog("Drop As", f"Unknown drop type: {drop_type}")
        return False

    if success:
        source_manager.clear()
    return success
