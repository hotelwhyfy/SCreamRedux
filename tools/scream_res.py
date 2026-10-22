#!/usr/bin/env python3
"""Reader/extractor for SCream engine resource archives (.res).

Layout (little-endian):
    0x000  char[256]  magic "SCream resource file\\0" + uninitialized padding
    0x100  u32        entry count
    0x104  entry[count], 92 bytes each:
               char[80] name (NUL-terminated, rest is garbage)
               u32      type
               u32      offset (absolute, from start of file)
               u32      size

Usage:
    scream_res.py list    ARCHIVE [--types]
    scream_res.py extract ARCHIVE OUTDIR [PATTERN...]
"""

import argparse
import collections
import fnmatch
import os
import struct
import sys

MAGIC = b"SCream resource file\0"
HEADER_SIZE = 0x104
ENTRY = struct.Struct("<80sIII")


class Entry:
    __slots__ = ("name", "type", "offset", "size")

    def __init__(self, name, type_, offset, size):
        self.name, self.type, self.offset, self.size = name, type_, offset, size


def read_index(f):
    head = f.read(HEADER_SIZE)
    if not head.startswith(MAGIC):
        raise ValueError("not a SCream resource file")
    (count,) = struct.unpack_from("<I", head, 0x100)
    f.seek(0, os.SEEK_END)
    file_size = f.tell()
    f.seek(HEADER_SIZE)
    raw = f.read(count * ENTRY.size)
    if len(raw) != count * ENTRY.size:
        raise ValueError("truncated index")
    entries = []
    for i in range(count):
        name, type_, offset, size = ENTRY.unpack_from(raw, i * ENTRY.size)
        name = name.split(b"\0", 1)[0].decode("latin-1")
        if offset + size > file_size:
            raise ValueError(f"entry {i} ({name!r}) runs past end of file")
        entries.append(Entry(name, type_, offset, size))
    return entries


def cmd_list(args):
    with open(args.archive, "rb") as f:
        entries = read_index(f)
    if args.types:
        by_ext = collections.Counter()
        by_type = collections.defaultdict(collections.Counter)
        for e in entries:
            ext = os.path.splitext(e.name)[1].lower() or "(none)"
            by_ext[ext] += 1
            by_type[e.type][ext] += 1
        print(f"{len(entries)} entries")
        for t in sorted(by_type):
            print(f"type {t}: " + ", ".join(f"{x}={n}" for x, n in by_type[t].most_common()))
        return
    for e in entries:
        print(f"{e.type:3d} {e.offset:10d} {e.size:10d}  {e.name}")


def cmd_extract(args):
    os.makedirs(args.outdir, exist_ok=True)
    with open(args.archive, "rb") as f:
        entries = read_index(f)
        n = 0
        for e in entries:
            if args.patterns and not any(fnmatch.fnmatch(e.name, p) for p in args.patterns):
                continue
            # Names are flat, but guard against path traversal anyway.
            safe = os.path.basename(e.name.replace("\\", "/"))
            if not safe:
                continue
            f.seek(e.offset)
            with open(os.path.join(args.outdir, safe), "wb") as out:
                out.write(f.read(e.size))
            n += 1
    print(f"extracted {n} files to {args.outdir}", file=sys.stderr)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    pl = sub.add_parser("list")
    pl.add_argument("archive")
    pl.add_argument("--types", action="store_true", help="summarize by type and extension")
    pl.set_defaults(func=cmd_list)
    pe = sub.add_parser("extract")
    pe.add_argument("archive")
    pe.add_argument("outdir")
    pe.add_argument("patterns", nargs="*", help="fnmatch patterns to filter names")
    pe.set_defaults(func=cmd_extract)
    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
