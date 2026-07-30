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


def _auto_rename_path(destination: Path, source: Path, kind: str = "Hardlink") -> Path:
    """
    Generate an auto-renamed path if destination exists.
    
    Pattern: "filename - Hardlink.ext", "filename - Hardlink (2).ext", etc.
    """
    if not destination.exists() and not destination.is_symlink():
        return destination
    
    stem = source.stem
    suffix = source.suffix
    parent = destination.parent
    
    counter = 1
    while True:
        if counter == 1:
            new_name = f"{stem} - {kind}{suffix}"
        else:
            new_name = f"{stem} - {kind} ({counter}){suffix}"
        
        new_path = parent / new_name
        if not new_path.exists() and not new_path.is_symlink():
            return new_path
        counter += 1


def _auto_rename_dir(destination: Path, source: Path, kind: str = "Hardlink") -> Path:
    """
    Generate an auto-renamed path for directories.
    
    Pattern: "dirname - Hardlink", "dirname - Hardlink (2)", etc.
    """
    if not destination.exists() and not destination.is_symlink():
        return destination
    
    parent = destination.parent
    name = source.name
    
    counter = 1
    while True:
        if counter == 1:
            new_name = f"{name} - {kind}"
        else:
            new_name = f"{name} - {kind} ({counter})"
        
        new_path = parent / new_name
        if not new_path.exists() and not new_path.is_symlink():
            return new_path
        counter += 1


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
        
        # Determine destination name
        dest_name = source_path.name
        dest_path = target_path / dest_name
        
        # Auto-rename if needed
        if source_path.is_file():
            dest_path = _auto_rename_path(dest_path, source_path)
        else:
            dest_path = _auto_rename_dir(dest_path, source_path)
        
        try:
            if source_path.is_file():
                os.link(source, str(dest_path))
                created.append(str(dest_path))
            else:
                raise OSError("Directories cannot be hardlinked. Use Drop Symlink.")
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
        
        # Determine destination name
        dest_name = source_path.name
        dest_path = target_path / dest_name
        
        # Auto-rename if needed
        if source_path.is_file():
            dest_path = _auto_rename_path(dest_path, source_path, "Symlink")
        else:
            dest_path = _auto_rename_dir(dest_path, source_path, "Symlink")
        
        try:
            if relative:
                # Resolve the containing directory so ".." hops in the relative
                # path are correct, but keep the leaf name unresolved so a
                # picked symlink links to itself rather than to its target.
                source_abs = os.path.join(
                    os.path.realpath(source_path.parent), source_path.name
                )
                # Compute relative path from target to source
                rel_path = os.path.relpath(source_abs, target_dir_abs)
                os.symlink(rel_path, str(dest_path))
            else:
                # Absolute symlink
                os.symlink(source, str(dest_path))
            
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
    source_path = Path(source_dir)
    target_path = Path(target_dir)
    
    if not source_path.exists() or not source_path.is_dir():
        ui.error_dialog("Hardlink Clone", f"Source is not a valid directory: {source_dir}")
        return False
    
    if target_path.exists():
        ui.error_dialog("Hardlink Clone", f"Target directory already exists: {target_dir}")
        return False
    
    if not os.access(target_path.parent, os.W_OK):
        ui.error_dialog("Hardlink Clone", f"No write permission in: {target_path.parent}")
        return False
    
    created_count = {"files": 0, "dirs": 0, "symlinks": 0}
    try:
        target_path.mkdir(parents=True)
        for root, dirs, files in os.walk(source_path, followlinks=False):
            source_root = Path(root)
            target_root = target_path / source_root.relative_to(source_path)
            target_root.mkdir(parents=True, exist_ok=True)

            for dirname in list(dirs):
                source_item = source_root / dirname
                target_item = target_root / dirname
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
        shutil.rmtree(target_path, ignore_errors=True)
        ui.error_dialog("Hardlink Clone", f"Clone failed: {error}")
        return False

    msg_parts = []
    if created_count["dirs"] > 0:
        msg_parts.append(f"{created_count['dirs']} directories")
    if created_count["files"] > 0:
        msg_parts.append(f"{created_count['files']} hardlinks")
    if created_count["symlinks"] > 0:
        msg_parts.append(f"{created_count['symlinks']} symlinks")

    ui.notify("Hardlink Clone Created", f"Created: {', '.join(msg_parts)}", "folder")
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
    source_path = Path(source_dir)
    target_path = Path(target_dir)
    
    if not source_path.exists() or not source_path.is_dir():
        ui.error_dialog("Symlink Clone", f"Source is not a valid directory: {source_dir}")
        return False
    
    if target_path.exists():
        ui.error_dialog("Symlink Clone", f"Target directory already exists: {target_dir}")
        return False
    
    if not os.access(target_path.parent, os.W_OK):
        ui.error_dialog("Symlink Clone", f"No write permission in: {target_path.parent}")
        return False
    
    created_count = {"dirs": 0, "files": 0}
    try:
        target_path.mkdir(parents=True)
        for root, dirs, files in os.walk(source_path, followlinks=False):
            source_root = Path(root)
            target_root = target_path / source_root.relative_to(source_path)
            target_root.mkdir(parents=True, exist_ok=True)

            for dirname in list(dirs):
                source_item = source_root / dirname
                target_item = target_root / dirname
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
        shutil.rmtree(target_path, ignore_errors=True)
        ui.error_dialog("Symlink Clone", f"Clone failed: {error}")
        return False

    msg_parts = []
    if created_count["dirs"] > 0:
        msg_parts.append(f"{created_count['dirs']} directories")
    if created_count["files"] > 0:
        msg_parts.append(f"{created_count['files']} symlinks")

    ui.notify("Symlink Clone Created", f"Created: {', '.join(msg_parts)}", "folder")
    return True


def smart_copy(source: str, target_dir: str):
    """
    Smart copy - copies directory structure while preserving hardlink/symlink relations.
    
    This preserves inner hardlinks (saturated) and inner symlinks.
    
    Args:
        source: Source file or directory
        target_dir: Target directory
    """
    source_path = Path(source)
    target_path = Path(target_dir)
    
    if not source_path.exists() and not source_path.is_symlink():
        ui.error_dialog("Smart Copy", f"Source does not exist: {source}")
        return False
    
    if not target_path.exists() or not target_path.is_dir():
        ui.error_dialog("Smart Copy", f"Target is not a valid directory: {target_dir}")
        return False
    
    if not os.access(target_dir, os.W_OK):
        ui.error_dialog("Smart Copy", f"No write permission in: {target_dir}")
        return False
    
    target_dest = target_path / source_path.name
    if source_path.is_file():
        target_dest = _auto_rename_path(target_dest, source_path, "Copy")
    else:
        target_dest = _auto_rename_dir(target_dest, source_path, "Copy")
    
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

    try:
        _copy_recursive(source_path, target_dest)
    except Exception as error:
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

    ui.notify("Smart Copy Created", f"Copied: {', '.join(msg_parts)}", "folder")
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
        destination = _auto_rename_dir(Path(target_dir) / source.name, source, "Hardlink Clone")
        success = hardlink_clone(str(source), str(destination))
    elif drop_type == "symlink-clone":
        if len(sources) != 1:
            ui.error_dialog("Symlink Clone", "Please pick exactly one directory for cloning.")
            return False
        source = Path(sources[0])
        destination = _auto_rename_dir(Path(target_dir) / source.name, source, "Symlink Clone")
        success = symlink_clone(str(source), str(destination), relative)
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


def has_picked_sources() -> bool:
    """Check if there are currently picked sources."""
    return source_manager.has_sources()
