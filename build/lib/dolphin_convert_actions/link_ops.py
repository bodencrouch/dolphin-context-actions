#!/usr/bin/env python3
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
import stat
import subprocess
from collections import defaultdict
from pathlib import Path
from typing import Optional

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
        except (json.JSONDecodeError, IOError):
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
        """Pick new sources (replaces existing)."""
        # Resolve to absolute paths
        resolved = [str(Path(p).resolve()) for p in paths]
        self.save(resolved)
        return resolved
    
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


def can_create_hardlinks(path: str) -> bool:
    """Check if hardlinks can be created for the given path."""
    try:
        st = os.stat(path)
        # Check if filesystem supports hardlinks
        # On Linux, most modern filesystems (ext4, btrfs, xfs, etc.) support hardlinks
        test_file = Path(path).parent / ".hardlink_test_temp"
        try:
            # Create a test hardlink
            os.link(path, str(test_file))
            test_file.unlink()
            return True
        except (OSError, PermissionError):
            return False
    except OSError:
        return False


def can_create_symlinks(path: str) -> bool:
    """Check if symlinks can be created (always true on Linux for regular users)."""
    # On Linux, symlinks are generally supported
    # Check write permission in parent directory
    parent = Path(path).parent
    return os.access(parent, os.W_OK)


def pick_link_source(paths: list[str]):
    """
    Pick files/folders as source for link creation.
    
    Args:
        paths: List of file/folder paths to pick
    """
    if not paths:
        ui.error_dialog("Pick Link Source", "No files or folders selected.")
        return False
    
    # Validate paths
    valid_paths = []
    for p in paths:
        path = Path(p)
        if not path.exists():
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


def _auto_rename_path(destination: Path, source: Path) -> Path:
    """
    Generate an auto-renamed path if destination exists.
    
    Pattern: "filename - Hardlink.ext", "filename - Hardlink (2).ext", etc.
    """
    if not destination.exists():
        return destination
    
    stem = source.stem
    suffix = source.suffix
    parent = destination.parent
    
    counter = 1
    while True:
        if counter == 1:
            new_name = f"{stem} - Hardlink{suffix}"
        else:
            new_name = f"{stem} - Hardlink ({counter}).{suffix[1:]}"
        
        new_path = parent / new_name
        if not new_path.exists():
            return new_path
        counter += 1


def _auto_rename_dir(destination: Path, source: Path) -> Path:
    """
    Generate an auto-renamed path for directories.
    
    Pattern: "dirname - Hardlink", "dirname - Hardlink (2)", etc.
    """
    if not destination.exists():
        return destination
    
    parent = destination.parent
    name = source.name
    
    counter = 1
    while True:
        if counter == 1:
            new_name = f"{name} - Hardlink"
        else:
            new_name = f"{name} - Hardlink ({counter})"
        
        new_path = parent / new_name
        if not new_path.exists():
            return new_path
        counter += 1


def drop_hardlink(target_dir: str, relative_to: Optional[str] = None):
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
                # Create hardlink for file
                if not can_create_hardlinks(source):
                    raise OSError(f"Filesystem does not support hardlinks for: {source}")
                os.link(source, str(dest_path))
                created.append(str(dest_path))
            else:
                # For directories, we need to create a directory and hardlink contents
                # But directories themselves cannot be hardlinked on most Unix filesystems
                # So we create a symlink instead for directories
                ui.info_dialog(
                    "Directory Hardlink",
                    "Directories cannot be hardlinked on this filesystem.\n"
                    "Creating a symbolic link instead."
                )
                # Create symlink to directory
                if dest_path.exists():
                    dest_path = _auto_rename_dir(dest_path, source_path)
                os.symlink(source, str(dest_path))
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
    
    # Clear sources after drop
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
            dest_path = _auto_rename_path(dest_path, source_path)
        else:
            dest_path = _auto_rename_dir(dest_path, source_path)
        
        try:
            if relative:
                # Try to create relative symlink
                source_abs = str(source_path.resolve())
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
    
    # Clear sources after drop
    source_manager.clear()
    return len(created) > 0


def hardlink_clone(source_dir: str, target_dir: str, relative: bool = True):
    """
    Create a hardlink clone of a directory tree.
    
    Creates a copy of the directory structure where files are hardlinked
    to the original files instead of being copied.
    
    Args:
        source_dir: Source directory to clone
        target_dir: Destination directory
        relative: Create relative symlinks for directories (not applicable here)
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
    
    # Validate that we can create hardlinks
    # Try with a sample file
    try:
        test_files = list(source_path.rglob("*"))
        if test_files:
            test_file = test_files[0]
            if test_file.is_file():
                if not can_create_hardlinks(str(test_file)):
                    ui.error_dialog(
                        "Hardlink Clone",
                        "Filesystem does not support hardlinks"
                    )
                    return False
    except Exception:
        pass
    
    # Create target directory
    target_path.mkdir(parents=True, exist_ok=True)
    
    created_count = {"files": 0, "dirs": 0, "symlinks": 0}
    failed = []
    
    # Walk through source and create hardlinks
    for root, dirs, files in os.walk(source_dir):
        # Compute relative path
        rel_path = os.path.relpath(root, source_dir)
        target_root = target_path / rel_path
        
        # Create directory
        if not target_root.exists():
            try:
                target_root.mkdir(parents=True, exist_ok=True)
                created_count["dirs"] += 1
            except Exception as e:
                failed.append(f"Directory {rel_path}: {e}")
                continue
        
        # Create hardlinks for files
        for file in files:
            source_file = Path(root) / file
            target_file = target_root / file
            
            try:
                if target_file.exists():
                    target_file.unlink()
                os.link(source_file, target_file)
                created_count["files"] += 1
            except Exception as e:
                # If hardlink fails, try symlink
                try:
                    rel_link = os.path.relpath(source_file, target_root)
                    os.symlink(rel_link, target_file)
                    created_count["symlinks"] += 1
                except Exception as e2:
                    failed.append(f"File {rel_path}/{file}: {e2}")
    
    # Report results
    msg_parts = []
    if created_count["dirs"] > 0:
        msg_parts.append(f"{created_count['dirs']} director(y/ies)")
    if created_count["files"] > 0:
        msg_parts.append(f"{created_count['files']} hardlink(s)")
    if created_count["symlinks"] > 0:
        msg_parts.append(f"{created_count['symlinks']} symlink(s)")
    
    if msg_parts:
        ui.notify(
            "Hardlink Clone Created",
            f"Created: {", ".join(msg_parts)}",
            "folder"
        )
    
    if failed:
        ui.error_dialog("Clone Errors", "\n".join(failed))
    
    return len(failed) == 0


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
    
    # Create target directory
    target_path.mkdir(parents=True, exist_ok=True)
    
    created_count = {"dirs": 0, "files": 0}
    failed = []
    
    source_dir_abs = str(source_path.resolve())
    target_dir_abs = str(target_path.resolve())
    
    # Walk through source and create symlinks
    for root, dirs, files in os.walk(source_dir):
        # Compute relative path
        rel_path = os.path.relpath(root, source_dir)
        target_root = target_path / rel_path
        
        # Create directory (actual directory, not symlink)
        if not target_root.exists():
            try:
                target_root.mkdir(parents=True, exist_ok=True)
                created_count["dirs"] += 1
            except Exception as e:
                failed.append(f"Directory {rel_path}: {e}")
                continue
        
        # Create symlinks for files and subdirectories
        for item in files + dirs:
            source_item = Path(root) / item
            target_item = target_root / item
            
            try:
                if target_item.exists():
                    target_item.unlink()
                
                if relative:
                    # Create relative symlink
                    rel_link = os.path.relpath(source_item, target_root)
                else:
                    # Absolute symlink
                    rel_link = source_item
                
                os.symlink(rel_link, target_item)
                created_count["files"] += 1
            except Exception as e:
                failed.append(f"Item {rel_path}/{item}: {e}")
    
    # Report results
    msg_parts = []
    if created_count["dirs"] > 0:
        msg_parts.append(f"{created_count['dirs']} director(y/ies)")
    if created_count["files"] > 0:
        msg_parts.append(f"{created_count['files']} symlink(s)")
    
    if msg_parts:
        ui.notify(
            "Symlink Clone Created",
            f"Created: {", ".join(msg_parts)}",
            "folder"
        )
    
    if failed:
        ui.error_dialog("Clone Errors", "\n".join(failed))
    
    return len(failed) == 0


def smart_copy(source: str, target_dir: str, 
               outer_handling: str = "unroll", 
               relative: bool = True):
    """
    Smart copy - copies directory structure while preserving hardlink/symlink relations.
    
    This preserves inner hardlinks (saturated) and inner symlinks.
    
    Args:
        source: Source file or directory
        target_dir: Target directory
        outer_handling: How to handle outer symlinks: "crop", "unroll", or "splice"
        relative: Create relative symlinks when possible
    """
    source_path = Path(source)
    target_path = Path(target_dir)
    
    if not source_path.exists():
        ui.error_dialog("Smart Copy", f"Source does not exist: {source}")
        return False
    
    if not target_path.exists() or not target_path.is_dir():
        ui.error_dialog("Smart Copy", f"Target is not a valid directory: {target_dir}")
        return False
    
    if not os.access(target_dir, os.W_OK):
        ui.error_dialog("Smart Copy", f"No write permission in: {target_dir}")
        return False
    
    # Determine target name
    target_name = source_path.name
    if source_path.is_dir():
        target_dest = target_path / target_name
    else:
        target_dest = target_path / target_name
    
    # Auto-rename if needed
    if source_path.is_file():
        target_dest = _auto_rename_path(target_dest, source_path)
    else:
        target_dest = _auto_rename_dir(target_dest, source_path)
    
    if target_dest.exists():
        ui.error_dialog("Smart Copy", f"Destination already exists: {target_dest}")
        return False
    
    created_count = {"files": 0, "dirs": 0, "symlinks": 0, "hardlinks": 0}
    failed = []
    
    source_abs = str(source_path.resolve())
    target_abs = str(target_path.resolve())
    
    # Build a map of inodes to identify hardlinks
    # inode -> list of paths
    inode_map: dict[tuple, list[str]] = defaultdict(list)
    
    # First pass: scan source and build inode map
    for root, dirs, files in os.walk(source_abs):
        for file in files:
            filepath = Path(root) / file
            try:
                file_stat = os.stat(filepath)
                inode_key = (file_stat.st_ino, file_stat.st_dev)
                inode_map[inode_key].append(str(filepath))
            except Exception:
                pass
    
    # Second pass: copy with hardlink preservation
    def _copy_recursive(src: Path, dst: Path):
        if src.is_symlink():
            # Handle symlinks
            link_target = os.readlink(src)
            
            # Determine if it's inner or outer
            if link_target.startswith("/"):
                target_abs_path = link_target
            else:
                target_abs_path = str((src.parent / link_target).resolve())
            
            source_ancestor = source_abs
            if target_abs_path.startswith(source_ancestor):
                # Inner symlink - relative to source
                rel_in_source = os.path.relpath(target_abs_path, source_ancestor)
                rel_in_target = os.path.relpath(
                    target_abs_path, 
                    os.path.join(target_abs, os.path.relpath(str(src), source_abs))
                )
                # Create relative symlink
                try:
                    if dst.exists():
                        dst.unlink()
                    os.symlink(rel_in_source, str(dst))
                    created_count["symlinks"] += 1
                except Exception as e:
                    failed.append(f"Symlink {src}: {e}")
            else:
                # Outer symlink
                if outer_handling == "crop":
                    # Don't create the symlink
                    pass
                elif outer_handling == "unroll":
                    # Copy the target content
                    target_src = Path(target_abs_path)
                    if target_src.exists():
                        if dst.exists():
                            if dst.is_dir():
                                shutil.rmtree(dst)
                            else:
                                dst.unlink()
                        if target_src.is_dir():
                            shutil.copytree(target_src, dst)
                        else:
                            shutil.copy2(target_src, dst)
                        created_count["files"] += 1
                    else:
                        failed.append(f"Outer symlink target missing: {target_abs_path}")
                else:  # splice
                    # Create symlink to original target (absolute or relative)
                    try:
                        if dst.exists():
                            dst.unlink()
                        if relative:
                            rel = os.path.relpath(target_abs_path, dst.parent)
                            os.symlink(rel, str(dst))
                        else:
                            os.symlink(target_abs_path, str(dst))
                        created_count["symlinks"] += 1
                    except Exception as e:
                        failed.append(f"Outer symlink {src}: {e}")
        elif src.is_file():
            # Handle regular files with hardlink preservation
            file_stat = os.stat(src)
            inode_key = (file_stat.st_ino, file_stat.st_dev)
            
            # Find all siblings in the source
            siblings = inode_map.get(inode_key, [])
            
            # Determine relative path in source
            rel_in_source = os.path.relpath(str(src), source_abs)
            rel_in_target = os.path.relpath(
                str(dst), 
                target_abs
            )
            
            # Check if this is the first sibling we're copying
            # If so, copy the file. Otherwise, create a hardlink.
            target_relative = os.path.join(target_abs, rel_in_target)
            
            # Check if we've already created a hardlink for this inode
            already_linked = False
            for sibling in siblings:
                sibling_rel = os.path.relpath(sibling, source_abs)
                sibling_target = Path(target_abs) / sibling_rel
                if sibling_target.exists() and sibling_target != dst:
                    # Already have a sibling, create hardlink
                    try:
                        if dst.exists():
                            dst.unlink()
                        os.link(str(sibling_target), str(dst))
                        created_count["hardlinks"] += 1
                        already_linked = True
                        break
                    except Exception:
                        pass
            
            if not already_linked:
                # First time seeing this inode, copy the file
                try:
                    if dst.parent.exists():
                        pass
                    else:
                        dst.parent.mkdir(parents=True, exist_ok=True)
                        created_count["dirs"] += 1
                    shutil.copy2(src, dst)
                    created_count["files"] += 1
                except Exception as e:
                    failed.append(f"File {src}: {e}")
        elif src.is_dir():
            # Create directory
            if not dst.exists():
                try:
                    dst.mkdir(parents=True, exist_ok=True)
                    created_count["dirs"] += 1
                except Exception as e:
                    failed.append(f"Directory {src}: {e}")
                    return
            
            # Recurse into directory
            for item in src.iterdir():
                _copy_recursive(item, dst / item.name)
    
    # Start copying
    _copy_recursive(source_path, target_dest)
    
    # Report results
    msg_parts = []
    if created_count["dirs"] > 0:
        msg_parts.append(f"{created_count['dirs']} director(y/ies)")
    if created_count["files"] > 0:
        msg_parts.append(f"{created_count['files']} file(s)")
    if created_count["hardlinks"] > 0:
        msg_parts.append(f"{created_count['hardlinks']} hardlink(s)")
    if created_count["symlinks"] > 0:
        msg_parts.append(f"{created_count['symlinks']} symlink(s)")
    
    if msg_parts:
        ui.notify(
            "Smart Copy Created",
            f"Copied: {", ".join(msg_parts)}",
            "folder"
        )
    
    if failed:
        ui.error_dialog("Smart Copy Errors", "\n".join(failed[:10]))  # Limit to 10 errors
    
    return len(failed) == 0


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
    if not file_path.exists():
        ui.error_dialog("Link Properties", f"File does not exist: {path}")
        return
    
    try:
        file_stat = os.stat(file_path)
        
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
                msg += f"<b>Siblings:</b><br>"
                for s in siblings[:20]:  # Limit to 20 siblings
                    msg += f"• {s}<br>"
                if len(siblings) > 20:
                    msg += f"• ... and {len(siblings) - 20} more<br>"
        else:
            msg = f"<b>Directory</b><br><br>Path: {file_path}"
        
        ui.info_dialog("Link Properties", msg, width=600, height=400)
    except Exception as e:
        ui.error_dialog("Link Properties", f"Error: {e}")


def smart_move(source: str, target: str):
    """
    Smart move - move directory while updating inner symlinks.
    
    This is integrated into the file manager's move operation via
    the service menu, but on Linux/KDE, this is harder to intercept.
    This function provides a manual alternative.
    """
    source_path = Path(source)
    target_path = Path(target)
    
    if not source_path.exists():
        ui.error_dialog("Smart Move", f"Source does not exist: {source}")
        return False
    
    if target_path.exists():
        ui.error_dialog("Smart Move", f"Target already exists: {target}")
        return False
    
    if not os.access(target_path.parent, os.W_OK):
        ui.error_dialog("Smart Move", f"No write permission in: {target_path.parent}")
        return False
    
    # On Linux, we can use shutil.move for the actual move
    # But we need to update symlinks that point to moved content
    
    # First, find all symlinks in the source tree
    symlinks_to_update = []
    for root, dirs, files in os.walk(source):
        for item in files + dirs:
            item_path = Path(root) / item
            if item_path.is_symlink():
                link_target = os.readlink(item_path)
                symlinks_to_update.append((item_path, link_target))
    
    # Move the source
    try:
        shutil.move(source, target)
    except Exception as e:
        ui.error_dialog("Smart Move", f"Move failed: {e}")
        return False
    
    # Update symlinks
    source_abs = str(source_path.resolve())
    target_abs = str(target_path.resolve())
    
    for symlink_path, link_target in symlinks_to_update:
        new_symlink_path = Path(target) / symlink_path.relative_to(source)
        
        if new_symlink_path.exists():
            # Update the symlink target
            if link_target.startswith("/"):
                # Absolute path - update if it was pointing into source
                if link_target.startswith(source_abs):
                    new_target = link_target.replace(source_abs, target_abs, 1)
                    try:
                        # Remove old symlink
                        new_symlink_path.unlink()
                        # Create new symlink
                        os.symlink(new_target, str(new_symlink_path))
                    except Exception:
                        pass
            else:
                # Relative path - need to recompute
                # This is complex, skip for now
                pass
    
    ui.notify("Smart Move", f"Moved {source} to {target}", "folder")
    return True


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
        # For clone, we expect a single directory source
        if len(sources) != 1:
            ui.error_dialog("Hardlink Clone", "Please pick exactly one directory for cloning.")
            return False
        return hardlink_clone(sources[0], target_dir, relative)
    elif drop_type == "symlink-clone":
        if len(sources) != 1:
            ui.error_dialog("Symlink Clone", "Please pick exactly one directory for cloning.")
            return False
        return symlink_clone(sources[0], target_dir, relative)
    elif drop_type == "smart-copy":
        if len(sources) != 1:
            ui.error_dialog("Smart Copy", "Please pick exactly one file or directory for smart copy.")
            return False
        return smart_copy(sources[0], target_dir, "unroll", relative)
    else:
        ui.error_dialog("Drop As", f"Unknown drop type: {drop_type}")
        return False


def get_drop_as_menu_items() -> list[tuple]:
    """
    Get the list of available Drop As menu items.
    
    Returns:
        List of (action_id, display_name, icon) tuples
    """
    sources = source_manager.get()
    
    # Check what types of sources we have
    has_files = any(Path(s).is_file() for s in sources)
    has_dirs = any(Path(s).is_dir() for s in sources)
    has_single = len(sources) == 1
    single_is_dir = has_single and Path(sources[0]).is_dir()
    
    items = []
    
    # Always available
    items.append(("hardlink", "Drop Hardlink", "edit-link"))
    items.append(("symlink", "Drop Symlink", "edit-link"))
    
    # Clone options (only for single directory)
    if single_is_dir:
        items.append(("separator1", "", ""))
        items.append(("hardlink-clone", "Drop Hardlink Clone", "folder"))
        items.append(("symlink-clone", "Drop Symlink Clone", "folder"))
        items.append(("smart-copy", "Drop Smart Copy", "folder"))
    
    return items


def has_picked_sources() -> bool:
    """Check if there are currently picked sources."""
    return source_manager.has_sources()
