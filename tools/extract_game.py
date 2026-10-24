#!/usr/bin/env python3
"""Collect the files the engine needs from the original discs into one directory.

The installers are Windows programs (NSIS), but nothing in them has to run: the
game files are unpacked with unar or 7z, which work on macOS and Linux
(brew install unar / apt install unar, or p7zip / sevenzip).

    extract_game.py SOURCE... -o OUTDIR [--edition 2006|dc]

OUTDIR must be new or empty (to extract again, delete it first or use a new
name), and it can't be a source folder or inside one.

A SOURCE is a disc (a mounted or copied directory, or an .iso image), an
installer .exe, or a directory the game is already installed in. Sources can be
given in any order. File names are matched ignoring case, in subdirectories
too (the shallowest match wins).

The edition is told by the scripts' Lua version (5.0 in 2006, 5.1 in the DC),
so other regions and languages are recognised too. Each file written is listed
with the original it matches, if it is one of the copies in KNOWN; a file the
same size as a known original but with different content is reported as
probably damaged (a bad disc read). The result runs with:

    ./build/scream --data OUTDIR

2006 release: scream.cfg, scream.sc, scratches.sc and scream.res come out of
the disc's setup.exe; scratches.res is on the disc itself. The 2006 patch
(scratches-patch.exe) only replaces scream.exe, which the engine does not use,
so it is not needed; passing it as a source is harmless.

Director's Cut, two-disc set: disc 1's setup.exe has scream.cfg, scream.sc,
scream.res, sdc-intro.srt and the first scratches.sc; scratches.res is in the
split archive scratches.7z.001... on the same disc. Disc 2's
scratches-dc-update.exe adds the June 2007 scratches.sc and update.res (the HD
textures, which the engine lays over scratches.res).

Director's Cut, single-disc release: SDC_Setup.exe has everything disc 1 has
(an older scratches.sc, and a scratches.res without the vid-logo_us.ogg logo).
Disc 2's update applies to it as well.

The DC patch (scratches-dc-patch.exe) is optional. It replaces scratches.sc
with scratches_post.sc (the update's script, which needs update.res) on an
updated install, and with scratches_pre.sc otherwise.
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

from scream_res import read_index

EDITIONS = {
    "2006": {
        "files": ["scream.cfg", "scream.sc", "scratches.sc", "scream.res", "scratches.res"],
        "optional": [],
    },
    "dc": {
        "files": ["scream.cfg", "scream.sc", "scratches.sc", "scream.res", "scratches.res", "sdc-intro.srt"],
        "optional": ["update.res"],
    },
}
EDITION_NAMES = {"2006": "Scratches (2006)", "dc": "Scratches - Director's Cut"}

# Installer -> rank. A file from a higher-ranked installer replaces one from a
# lower rank (disc 1 < disc 2 update < patch); at the same rank, files on the
# disc or in the install folder win over installer contents.
INSTALLERS = {"setup.exe": 0, "sdc_setup.exe": 0, "scratches-dc-update.exe": 1, "scratches-dc-patch.exe": 2}
SPLIT_ARCHIVE = "scratches.7z.001"  # DC disc 1: holds scratches.res
PATCH_SCRIPTS = {True: "scratches_post.sc", False: "scratches_pre.sc"}  # by: is the update installed?
UPDATE_ONLY_ENTRY = "trck-alt.ogg"  # in update.res, or in a scratches.res it was merged into
LUA_VERSION = {b"\x1bLuaP": "2006", b"\x1bLuaQ": "dc"}  # chunk header: Lua 5.0 / 5.1

# The originals checked so far (US English, Got Game): MD5 -> (name, size, what it is).
KNOWN = {
    "abbbf73465e1ddfde5ed939ad67f9a5a": ("scream.cfg", 774, "2006 disc, as shipped"),
    "87c8a33c4739548a1f510b49fa07d6ba": ("scream.sc", 5354, "2006 disc"),
    "6b15f443a27f4ea6ea70bf67455d4480": ("scratches.sc", 952519, "2006 disc (Feb 2006)"),
    "ae20116d6254c49818480d4316cae207": ("scream.res", 420343, "2006 disc"),
    "63bfa7f95b20644ae83787d42dcc7536": ("scratches.res", 504588657, "2006 disc"),
    "8d508aa0299804ab46ae1a6c3c5b42ad": ("scream.cfg", 772, "DC, as shipped"),
    "5f4ee3922cd75d658c6818b0bdc917fb": ("scream.sc", 5889, "DC"),
    "0f7030261ca035786985ed87cb12d05a": ("scream.res", 420343, "DC"),
    "330caf644c6268f16e61394c019c6018": ("sdc-intro.srt", 2135, "DC"),
    "26a4adfe47b7471804cf9e38f9be34a2": ("scratches.res", 714687667, "DC two-disc set, disc 1"),
    "74e6a98a3fede88c26c36d4d2ca77d7f": ("scratches.res", 714552906, "DC single-disc release"),
    "7944bbbe6a6d1c4cabdb6a49ef95f23e": ("scratches.sc", 1120793, "DC single-disc release (Apr 30 2007)"),
    "0c880333055b24ec119ee78489385b59": ("scratches.sc", 1120865, "DC patch, not updated (May 10 2007)"),
    "747b27c69f117c54253b9a1f4a7b1992": ("scratches.sc", 1121152, "DC two-disc set, disc 1 (May 22 2007)"),
    "d697dcb6491e30ce54c38e2f42fdd2c8": ("scratches.sc", 1124643, "DC disc 2 update / patch (Jun 2007)"),
    "decd946b1410c02b3c011832f489972a": ("update.res", 564153915, "DC disc 2 update"),
}


class Root:
    """A directory to take files from: a source, or an unpacked installer."""

    def __init__(self, path, rank, on_disc):
        self.path, self.rank, self.on_disc = path, rank, on_disc
        self.index = {}  # lower-case name -> shallowest path
        for dirpath, dirs, files in os.walk(path):
            dirs.sort()
            for f in sorted(files):
                p = os.path.join(dirpath, f)
                if os.path.isfile(p):
                    self.index.setdefault(f.lower(), p)  # walk is top-down: shallower first

    def find(self, name):
        return self.index.get(name.lower())


def unpack(archive, dest, only=None):
    """Unpack an installer, archive or disc image with whichever tool is available.
    ONLY: for an .iso, the lower-case file names worth extracting."""
    print("unpacking " + archive)
    os.makedirs(dest, exist_ok=True)
    if archive.lower().endswith(".iso") and shutil.which("bsdtar"):
        listing = subprocess.run(["bsdtar", "-tf", archive], capture_output=True, text=True).stdout.splitlines()
        members = [m for m in listing if only is None or os.path.basename(m).lower() in only]
        cmd = ["bsdtar", "-xf", archive, "-C", dest] + members
    elif shutil.which("unar"):
        cmd = ["unar", "-q", "-f", "-D", "-o", dest, archive]
    elif sevenzip := shutil.which("7z") or shutil.which("7zz"):
        cmd = [sevenzip, "x", "-y", "-bso0", "-bsp0", "-o" + dest, archive]
    else:
        sys.exit("need unar or 7z to unpack " + archive)
    if subprocess.run(cmd, stdout=subprocess.DEVNULL).returncode != 0:
        sys.exit("could not unpack " + archive)


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def res_entries(path):
    """Entry names of a .res archive, or an error message."""
    try:
        with open(path, "rb") as f:
            return {e.name.lower() for e in read_index(f)}
    except (OSError, ValueError) as e:
        return str(e)


def detect(roots):
    """The edition the scripts in ROOTS belong to, by their Lua version."""
    editions = set()
    for root in roots:
        for name in ["scream.sc", "scratches.sc"] + list(PATCH_SCRIPTS.values()):
            if path := root.find(name):
                with open(path, "rb") as f:
                    if edition := LUA_VERSION.get(f.read(5)):
                        editions.add(edition)
    if len(editions) > 1:
        sys.exit("the sources mix the 2006 release and the Director's Cut; pass one, or use --edition")
    if not editions:
        sys.exit("no game scripts (scream.sc, scratches.sc) found in the sources")
    return editions.pop()


def describe(name, digest, size, edition):
    if known := KNOWN.get(digest):
        other = (known[2].startswith("2006")) != (edition == "2006")
        return known[2] + (" - from the other edition!" if other else ""), False
    if name == "scream.cfg":
        return "edited", False  # setup choices are saved here
    for k_name, k_size, k_label in KNOWN.values():
        if k_name == name and k_size == size:
            return f"PROBABLY DAMAGED: the size of the {k_label} original, but different content", True
    return "not one of the known copies (another release or language?)", False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sources", nargs="+", metavar="SOURCE", help="disc directory, .iso image, installer or install directory")
    ap.add_argument("-o", "--out", required=True, help="directory to write the game files to")
    ap.add_argument("--edition", choices=sorted(EDITIONS), help="skip detection")
    args = ap.parse_args()
    # A new or empty output folder: nothing from an earlier run can be mixed in or left
    # behind, and no source (indexed recursively) can contain it.
    out = os.path.realpath(args.out)
    for src in args.sources:
        if os.path.isdir(src):
            src = os.path.realpath(src)
            if os.path.commonpath([out, src]) == src:
                sys.exit(f"the output folder can't be a source or inside one ({src}); give a new folder")
    if os.path.exists(out):
        if not os.path.isdir(out):
            sys.exit(args.out + " is a file")
        if [f for f in os.listdir(out) if f != ".DS_Store"]:
            sys.exit(args.out + " is not empty; give a new folder, or delete this one first")
    useful = {n for e in EDITIONS.values() for n in e["files"] + e["optional"]}
    useful |= set(INSTALLERS) | set(PATCH_SCRIPTS.values()) | {f"scratches.7z.{i:03}" for i in range(1, 100)}

    os.makedirs(args.out, exist_ok=True)
    # Unpack inside OUTDIR so the big files can be moved out instead of copied.
    with tempfile.TemporaryDirectory(prefix=".extract-", dir=args.out) as tmp:
        tmp = os.path.abspath(tmp)
        counter = iter(range(1 << 30))

        def unpacked(archive, rank, on_disc, only=None):
            dest = os.path.join(tmp, str(next(counter)))
            unpack(archive, dest, only)
            if archive.startswith(tmp):  # taken off a disc image: no longer needed
                os.remove(archive)
            return Root(dest, rank, on_disc)

        roots = []
        for src in args.sources:
            if os.path.isdir(src):
                roots.append(Root(src, 0, True))
            elif src.lower().endswith(".iso"):
                roots.append(unpacked(src, 0, True, useful))
            elif os.path.isfile(src):
                roots.append(unpacked(src, INSTALLERS.get(os.path.basename(src).lower(), 0), False))
            else:
                sys.exit(src + ": no such file or directory")
        for root in list(roots):
            for name, rank in INSTALLERS.items():
                if exe := root.find(name):
                    roots.append(unpacked(exe, rank, False))

        edition_id = args.edition or detect(roots)
        edition = EDITIONS[edition_id]
        wanted = edition["files"] + edition["optional"]

        def best(name):
            hits = [(r.rank, r.on_disc, p) for r in roots if (p := r.find(name))]
            return max(hits, key=lambda h: h[:2])[2] if hits else None

        found = {name: best(name) for name in wanted}
        # The split archive only matters when nothing else has its file.
        if edition_id == "dc" and not found["scratches.res"]:
            for root in list(roots):
                if part := root.find(SPLIT_ARCHIVE):
                    roots.append(unpacked(part, root.rank, False))
                    if part.startswith(tmp):
                        for other in os.listdir(os.path.dirname(part)):
                            if other.lower().startswith("scratches.7z."):
                                os.remove(os.path.join(os.path.dirname(part), other))
                    found["scratches.res"] = best("scratches.res")
                    break

        updated = False
        if edition_id == "dc":
            merged = found["scratches.res"] and UPDATE_ONLY_ENTRY in res_entries(found["scratches.res"])
            updated = bool(found["update.res"] or merged)
            choice = [(r.rank, p) for r in roots if (p := r.find(PATCH_SCRIPTS[updated]))]
            if choice and not any(r.find("scratches.sc") for r in roots if r.rank > max(choice)[0]):
                found["scratches.sc"] = max(choice)[1]

        missing = [name for name in edition["files"] if not found[name]]
        if missing:
            sys.exit("not found in the sources: " + ", ".join(missing))

        state = ""
        if edition_id == "dc":
            state = ", with the disc 2 update" if found["update.res"] else ", update merged into scratches.res" if updated else ", without the disc 2 update"
        print(EDITION_NAMES[edition_id] + state)
        problems = []
        for name in wanted:
            path = found[name]
            if not path:
                print(f"  {'':32}  {name:14} (none)")
                continue
            dest = os.path.join(args.out, name)
            if path.startswith(tmp):
                shutil.move(path, dest)
            else:
                shutil.copyfile(path, dest)
            os.chmod(dest, 0o644)  # disc files are read-only
            digest = md5(dest)
            label, damaged = describe(name, digest, os.path.getsize(dest), edition_id)
            if name.endswith(".res") and isinstance(err := res_entries(dest), str):
                label, damaged = "BROKEN: " + err, True
            if damaged:
                problems.append(name)
            print(f"  {digest}  {name:14} {label}")
        if problems:
            print("warning: " + ", ".join(problems) + " looks damaged; copy it from the disc again, or another copy")
        print(f"to check that everything decodes: ./build/scream --data {args.out} --verify-media")
        return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
