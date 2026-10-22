#!/usr/bin/env python3
"""Check a decompiled SCream game script for references that don't resolve.

Complements `scream --check` (which validates the world the script builds) by
looking at what handlers do at run time: every literal name passed to the
sc* API (rooms, nodes, zooms, items, scripts, choices, tracks, handlers) must
be created somewhere, every literal file name must exist in the data, and
every global function called must be defined.

Node names are checked per room where the room is known statically (a
handler that switches room and then node); otherwise against all rooms.

Usage:
    check_script.py SCRIPT.lua DATA_DIR [DATA_DIR...]
"""

import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from api_usage import split_args  # noqa: E402
from scream_res import read_index  # noqa: E402

CALL = re.compile(r"\b([A-Za-z_]\w*)\(")
FUNC = re.compile(r"^\s*(?:local\s+)?function\s+([A-Za-z_][\w.]*)\s*\(", re.M)
ASSIGN_FUNC = re.compile(r"^\s*([A-Za-z_]\w*)\s*=\s*function\b", re.M)
STRING = re.compile(r'^"(.*)"$')
FILE = re.compile(r"\.(ogg|jpg|tga|srt)$", re.I)

LUA_BUILTINS = set("""assert collectgarbage dofile error getfenv getmetatable ipairs load loadfile
loadstring next pairs pcall print rawequal rawget rawset select setfenv setmetatable tonumber
tostring type unpack xpcall require module""".split())


def literal(arg):
    m = STRING.match(arg.strip())
    return m.group(1) if m else None


def data_files(dirs):
    names = set()
    for d in dirs:
        for f in os.listdir(d):
            names.add(f.lower())
            if f.lower().endswith(".res"):
                with open(os.path.join(d, f), "rb") as fh:
                    names.update(e.name.lower() for e in read_index(fh))
    return names


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src = open(sys.argv[1], encoding="latin-1").read()
    files = data_files(sys.argv[2:])

    calls = []  # (line, name, args)
    for m in CALL.finditer(src):
        args, _ = split_args(src, m.end())
        calls.append((src.count("\n", 0, m.start()) + 1, m.group(1), args))

    defined = collections.defaultdict(set)  # kind -> names
    nodes = collections.defaultdict(set)    # room -> nodes
    room = None
    for line, fn, args in calls:
        if fn == "scCreate" and len(args) >= 2 and literal(args[1]) is not None:
            kind, name = args[0], literal(args[1])
            defined[kind].add(name)
            if kind == "SC_ROOM":
                room = name
            elif kind == "SC_NODE" and room:
                nodes[room].add(name)
    functions = set(FUNC.findall(src)) | set(ASSIGN_FUNC.findall(src))
    all_nodes = set().union(*nodes.values()) if nodes else set()

    problems = []

    def need(kind, name, line, what):
        if name is not None and name not in defined[kind] and not (kind == "SC_ZOOM" and name == "black"):
            problems.append(f"{line}: {what}: no {kind[3:].lower()} '{name}'")

    def need_func(name, line, what):
        if name and name not in functions:
            problems.append(f"{line}: {what}: no function '{name}'")

    # Function boundaries reset the room context.
    starts = sorted(src.count("\n", 0, m.start()) + 1 for m in FUNC.finditer(src))
    current_room = None  # last literal scSwitch(SC_ROOM, ...) seen earlier in the same function
    next_start = iter(starts + [10**9])
    boundary = next(next_start)
    for line, fn, args in calls:
        while line >= boundary:
            current_room = None
            boundary = next(next_start)
        lits = [literal(a) for a in args]
        for s in lits:
            if s and FILE.search(s) and s.lower() not in files and s.lower() != "black.jpg":
                problems.append(f"{line}: {fn}: missing file '{s}'")
        if fn == "scSwitch" and len(args) >= 2:
            kind, name = args[0], lits[1]
            if kind == "SC_ROOM":
                need("SC_ROOM", name, line, fn)
                current_room = name
            elif kind == "SC_NODE" and name is not None:
                scope = nodes.get(current_room) if current_room else None
                if name not in (scope if scope is not None else all_nodes):
                    problems.append(f"{line}: scSwitch: no node '{name}'" +
                                    (f" in room {current_room}" if scope is not None else ""))
            elif kind in ("SC_ZOOM", "SC_OVERLAY"):
                need(kind, name, line, fn)
        elif fn == "scBind" and len(args) >= 2 and args[0] in ("SC_ROOM", "SC_ITEM", "SC_SCRIPT", "SC_CHOICE",
                                                                 "SC_TRACK", "SC_OVERLAY", "SC_ZOOM"):
            need(args[0], lits[1], line, fn)
        elif fn == "scBind" and len(args) >= 2 and args[0] == "SC_NODE" and lits[1] not in all_nodes:
            problems.append(f"{line}: scBind: no node '{lits[1]}'")
        elif fn == "scScriptGo":
            need("SC_SCRIPT", lits[0] if lits else None, line, fn)
        elif fn == "scChoiceGo":
            need("SC_CHOICE", lits[0] if lits else None, line, fn)
        elif fn in ("scTrackPlay", "scPlaylistQueue") and lits:
            need("SC_TRACK", lits[0], line, fn)
        elif fn in ("scTimerCreate", "scTimerCreateGlobal") and len(lits) >= 3:
            need_func(lits[2], line, fn)
        elif fn == "scChoiceAdd" and len(lits) >= 2:
            need_func(lits[1], line, fn)
        elif fn in ("scOnClick", "scReaction", "scInventoryReaction") and "SC_CUSTOM" in args:
            need_func(lits[args.index("SC_CUSTOM") + 1] if args.index("SC_CUSTOM") + 1 < len(lits) else None, line, fn)
        elif fn in ("scOnEnter", "scOnExit", "scMenuSetEx") and lits:
            if fn != "scMenuSetEx" or args[0] in ("SC_INTRO", "SC_OUTRO"):
                need_func(lits[-1], line, fn)
        elif fn == "scMenuSet":
            for name in lits[7:]:
                need_func(name, line, fn)
        elif not fn.startswith("sc") and fn not in functions and fn not in LUA_BUILTINS and fn != "function":
            # string/table/math methods appear as e.g. string.format( -> 'format'; skip dotted calls
            problems.append(f"{line}: call to undefined function '{fn}'")

    # Dotted library calls (string.format) show up by their last part; drop those.
    lib = set(re.findall(r"\b(?:string|table|math|os|io|coroutine)\.(\w+)\(", src))
    problems = [p for p in problems if not any(p.endswith(f"undefined function '{f}'") for f in lib)]

    for p in problems:
        print(p)
    counts = {k[3:].lower(): len(v) for k, v in sorted(defined.items())}
    print(f"{len(functions)} functions, {counts}: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
