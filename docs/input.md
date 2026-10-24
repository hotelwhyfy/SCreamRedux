# Input

This page describes how the original SCream engine (`scream.exe`) reads the keyboard and the
mouse: which Windows messages it handles, what each key and button does in each engine state,
which cursor it shows, and how a click finds its hotspot. It covers only what the executables,
the shipped scripts and the shipped data define. Where the 2006 engine differs from the
Director's Cut (DC), the difference is listed in [2006 differences](#2006-differences).

Related pages:

- [`api.md`](api.md) for the `sc*` functions named here (`scOnHover`, `scOnClick`,
  `scReaction`, `scInventoryReaction`, `scCursor`, `scLock`, `scWait`, `scSwitch`, …).
- [`formats.md`](formats.md) for the settings block and the save slots.
- The [camera spec](camera.md) for how the camera angles, free look, edge turns and crossfades behave.
- The [screens spec](screens.md) for the layout of the inventory, menu, choice list and journal.

> **AI-assisted.** Claude (Anthropic's AI model) wrote this page. The sources were Ghidra
> decompilation and disassembly of the two executables listed in [api.md](api.md#sources),
> the shipped scripts decompiled with unluac, and measurements of the shipped files. A person
> has not yet checked it. A source tag says where a claim can be checked; it is not a
> guarantee. Check a claim against its source before you build on it, and treat
> `[unverified]` claims as open questions.

## Sources

| Tag | Means | File (MD5) |
|---|---|---|
| `[exe DC 0x…]` | code or data at that address in the DC update's `scream.exe` (May 2007, 649728 bytes) | `f696d90b1f1fe5eb050beda06781dcc8` |
| `[exe 2006 0x…]` | the same in the patched 2006 `scream.exe` (April 2006, 543744 bytes) | `5de7958817eecb3815d816d123ab9217` |
| `[script DC: name]`, `[script 2006: name]` | the decompiled shipped scripts (see [api.md](api.md#sources)) | |
| `[data FILE …]` | a measurement of a shipped file | DC `scratches.res` `74e6a98a3fede88c26c36d4d2ca77d7f`, 2006 `scratches.res` `63bfa7f95b20644ae83787d42dcc7536`, 2006 `scream.res` `ae20116d6254c49818480d4316cae207` |
| `[unverified: …]` | not confirmed in any of the above | |

Addresses are virtual addresses (image base 0x400000). Both executables were built with Open
Watcom, which passes arguments in registers (EAX, EDX, EBX, ECX). Key names for virtual-key
codes (VK_…) are those of the Windows virtual-key code table; the exe tests only the numbers.

Coordinates are client-area pixels with the origin at the top left, as Windows reports them,
unless a section says otherwise. `W` and `H` are the `WIDTH` and `HEIGHT` settings (DC
default 1024×768, see [formats.md](formats.md#screamcfg)).

## Engine states

The handlers below branch on the engine state: node 4000, video 4001, zoom 4002, inventory
4003, overlay 4004, menu 4005, choice 4007 and book 4008 [script DC: scream.sc top level].
What each state is, how it is entered, and how 4006 returns to the previous state are in
[mainloop.md](mainloop.md#states) [exe DC 0x402d20–0x402d73].

## Windows messages

**The window.**

- The window class has style 3 (`CS_HREDRAW | CS_VREDRAW`), without `CS_DBLCLKS`
  [exe DC 0x4196a3]. So a double click arrives as two ordinary button-down/up pairs.
- The window class cursor is the arrow (`LoadCursorA(0, 0x7f00)`) [exe DC 0x4196c2–0x4196cd],
  but the system cursor is hidden with `ShowCursor(FALSE)` once the window exists
  [exe DC 0x419785]. The engine draws its own cursor (see [Cursors](#cursors)).
  `ShowCursor(TRUE)` is called at shutdown and around the fatal-error message box
  [exe DC 0x419ab7, 0x419b55–0x419b71].
- Three hot keys are registered on the window [exe DC 0x4198a2–0x4198d2]:
  - id 0, no modifier, VK 0x79 (F10);
  - id −2, no modifier, VK 0x2c (Print Screen);
  - id −1, Alt, VK 0x2c (Alt+Print Screen).

**The window procedure** (0x419bd0) handles these messages and passes everything else to
`DefWindowProcA` [exe DC 0x419d9b]:

| Message | What the engine does |
|---|---|
| `WM_KEYDOWN` (0x100) | calls the key handler with `wParam`, the virtual-key code [exe DC 0x419cbf–0x419ccc] |
| `WM_HOTKEY` (0x312) | calls the key handler with the high word of `lParam`, which is the hot key's virtual-key code [exe DC 0x419c93–0x419ca3] |
| `WM_MOUSEMOVE` (0x200) | mouse handler, button 0, event "move" [exe DC 0x419cfa–0x419d0a] |
| `WM_LBUTTONDOWN` (0x201) / `WM_LBUTTONUP` (0x202) | mouse handler, button 1, event "down" / "up" [exe DC 0x419d1b–0x419d2e, 0x419c5b–0x419c71] |
| `WM_RBUTTONDOWN` (0x204) / `WM_RBUTTONUP` (0x205) | mouse handler, button 2, event "down" / "up" [exe DC 0x419d3f–0x419d55, 0x419d66–0x419d79] |
| `WM_SIZE` (5) | reads the client rectangle and calls the resize callback [exe DC 0x419c01–0x419c17] |
| `WM_PAINT` (0xf) | `BeginPaint` / `EndPaint` only [exe DC 0x419cdd–0x419cea] |
| `WM_CLOSE` (0x10) | `PostQuitMessage(0)` [exe DC 0x419d8a] |

Consequences:

- Key-up messages are ignored. Auto-repeat key-downs are passed on like any other, since only
  `wParam` is read [exe DC 0x419cca].
- `WM_SYSKEYDOWN`, `WM_CHAR`, the middle button and the wheel are not handled
  [exe DC 0x419bd0]. F10 reaches the key handler only through its hot key. Print Screen and
  Alt+Print Screen also reach it as VK 0x2c, which it ignores [exe DC 0x4175b0], so while the
  game runs these keys do nothing.
- The mouse position is the low and high word of `lParam`, zero-extended
  [exe DC 0x419c28–0x419c33, 0x419d1f–0x419d22].

**The message loop** (0x419b00) removes one message at a time with `PeekMessageA(…,
PM_REMOVE)`. It ends on `WM_QUIT`, translates and dispatches every other message, and runs
one engine frame (the per-frame routine 0x412010) whenever the queue is empty
[exe DC 0x419b08–0x419b41; frame routine registered at 0x4105e1].

**Inside blocking calls** (`scCutscene`, `scSleep`, `scScriptGo`, `scFXPlayEx` with `wait`),
the only input is Esc: each pass takes one message off the queue without dispatching it, and
every other message is lost, so a click does not skip a video and the known pointer position
is not updated [exe DC 0x419b80–0x419bb7]. A smooth `scLookAt` reads no messages, so input
waits until it returns. The details are in
[mainloop.md](mainloop.md#blocking-calls-and-nested-loops).

---

## Keyboard

### Key map

There is one key handler (0x4175b0) for every state. Only Esc and F5 test the state; every
other key acts the same in every state. Neither the input lock (`scLock`) nor the wait flag
(`scWait`) is checked [exe DC 0x4175b0].

| VK code | Key | Also reached by | Action |
|---|---|---|---|
| 0x1b | Esc | | see [Esc](#esc) [exe DC 0x417934] |
| 0x20 | Space | E, numpad 5 | camera: look-y set to 0 [exe DC 0x41760c] |
| 0x25 | Left arrow | A, numpad 1 | camera: yaw − 1.57 [exe DC 0x417923 → 0x417bc8; constant at 0x483e1c] |
| 0x26 | Up arrow | D, numpad 4 | camera: yaw + 1.57 [exe DC 0x417929 → 0x41787a; constant at 0x483e18] |
| 0x27 | Right arrow | W, F8 | camera: look-y set to +1.25 [exe DC 0x417932 → 0x4178ea] |
| 0x28 | Down arrow | S, F4 | camera: look-y set to −1.25 [exe DC 0x417857 → 0x417c0c] |
| 0x41 | A | | as Left arrow [exe DC 0x41786b] |
| 0x44 | D | | as Up arrow [exe DC 0x417871] |
| 0x45 | E | | as Space [exe DC 0x4178be] |
| 0x4c | L | VK 0x6c | toggles logging (the `LOG` setting); no message is shown [exe DC 0x4178c9, 0x4177b8–0x4177d4, 0x417c45] |
| 0x53 | S | | as Down arrow [exe DC 0x4178db] |
| 0x57 | W | | as Right arrow [exe DC 0x4178e1] |
| 0x70 | F1 | | toggles the `SC_HINTS` setting and shows "hints ON" / "hints OFF" [exe DC 0x4177da–0x417802, 0x417a77] |
| 0x71 | F2 | | toggles surround music, shows "surround music ON" / "OFF", and restarts a playing playlist; see [api.md](api.md#music-tracks) [exe DC 0x41780b–0x417849, 0x417a94] |
| 0x74 | F5 | | quicksave, except in the menu state (see below) [exe DC 0x417aa5–0x417ac9] |
| 0x76 | F7 | | quickload (see below) [exe DC 0x41765a–0x417704] |
| 0x78 | F9 | | cycles the camera speed and shows its name (see below) [exe DC 0x41771d–0x417745, 0x417aea–0x417b26] |
| 0x79 | F10 | | cycles the text speed the same way (see below) [exe DC 0x41774d–0x417775, 0x417b2f–0x417b6b] |
| 0x7b | F12 | | cycles the gamma setting (see below) [exe DC 0x41777b–0x417792; jump table data scream.exe VA 0x4175a0] |

No other key does anything [exe DC 0x4175df]. In particular Enter (VK 0x0d), Tab, the digit
keys, F3, F6 and F11 are ignored.

**Codes that alias other keys.** The handler compares lowercase character codes as well, but
it is given virtual-key codes, where those numbers mean other keys [exe DC 0x4175e8–0x417601,
0x417642, 0x417712; 0x419cca passes `wParam`]. Letter keys always arrive as 0x41–0x5a. So:

| Code | Character | Key it really is | Effect |
|---|---|---|---|
| 0x61 | `a` | numpad 1 | turn as A |
| 0x64 | `d` | numpad 4 | turn as D |
| 0x65 | `e` | numpad 5 | level as E |
| 0x6c | `l` | VK_SEPARATOR | logging toggle as L |
| 0x73 | `s` | F4 | look down as S |
| 0x77 | `w` | F8 | look up as W |

The F-key codes are themselves the codes of `p q t v x y {` (0x70–0x7b), so no character key
reaches them.

### Camera keys

Each camera key does the same three things [exe DC 0x41760c–0x417634 and the parallel blocks
at 0x41787a, 0x4178ea, 0x417bc8, 0x417c0c]:

1. sets the input lock and copies the current frame for a crossfade with start value 0.75
   [exe DC 0x407b20, 0x407b5b];
2. clears the input lock, so a lock set by a script with `scLock` is also cleared;
3. sets the camera at once: a yaw step of ±1.57 keeps the vertical value, and a vertical key
   keeps the yaw [exe DC 0x407d30, 0x407d10]. In the DC the vertical value is look-y, a
   component of the look vector, not an angle (see [camera.md](camera.md#camera-angles)).

The arrow keys do not match the letters: Up arrow turns like D, and Right arrow looks up
like W [exe DC 0x417923–0x417932]. Increasing yaw turns toward the `SC_RIGHT` face, so
−1.57 turns left (derived; see [camera.md](camera.md#camera-angles)).

The keys act in every state, so a camera key in a zoom, the inventory or the menu changes the
stored node camera too [exe DC 0x4175b0]. [unverified: what is visible when this happens
outside node view]

### Speed and gamma keys

F9 and F10 cycle a setting through three values. Any other stored value goes to NORMAL
[exe DC 0x41771d–0x417745, 0x417aea–0x417b26]:

| Current value | New value | Message |
|---|---|---|
| 50 | 75 | "camera speed FAST" / "text speed FAST" |
| 75 | 25 | "camera speed SLOW" / "text speed SLOW" |
| anything else | 50 | "camera speed NORMAL" / "text speed NORMAL" |

F9 changes settings block offset 0x1a4 and F10 offset 0x1a8; both default to 50
[exe DC 0x417733, 0x417763; defaults 0x40f260]. What the values do is described in the
[camera spec](camera.md) (camera speed). The text speed sets how fast feedback text fades in
and out ([screens.md](screens.md#feedback-text)) and how long it stays
([api.md](api.md#scfeedback)) [exe DC 0x40ad36–0x40ad58, 0x40afc2–0x40afe3, 0x40e26d].

F12 cycles settings block offset 0x024 (default 0) and shows the new name
[exe DC 0x417784–0x417bc7]:

| Current | New | Message |
|---|---|---|
| 0 | 1 | "gamma correction LOW" |
| 1 | 2 | "gamma correction NORMAL" |
| 2 | 3 | "gamma correction HIGH" |
| 3 | 0 | "gamma correction DISABLED" |
| other | unchanged | none |

What each level does to the picture is in [rendering.md](rendering.md#gamma-f12).

All these messages are shown as feedback text, the same text `scFeedback` sets
[exe DC 0x40e150].

### Quicksave and quickload

- **F5** does nothing in the menu state. In any other state it saves slot 11 (`scream.011`)
  with the description `quicksave` and shows "Game saved" [exe DC 0x417aa5–0x417ac9]. The
  file is described in [formats.md](formats.md#appendix-original-save-files-scream0nn).
- **F7** works in every state, but only if `scream.011` can be opened for reading
  [exe DC 0x41765f–0x417670]. It then [exe DC 0x417676–0x417704]:
  1. closes the test file and drops any held item;
  2. fades out channels 1–18 at rate 1 [exe DC 0x417680–0x4176a0] (the same off-by-one as
     the menu; see [mainloop.md](mainloop.md));
  3. switches to the `black` zoom, sleeps one second, and zooms out;
  4. stops the playlist as `scPlaylistStop(0)` does;
  5. stops and unloads every channel from 1 to 17 that is still playing;
  6. loads slot 11 and shows "Game loaded".

### Esc

Esc first drops any held item, in every state [exe DC 0x417934 → 0x403c50]. Dropping clears
the held item only; the cursor image and its highlight state are left as they were until the
next hover update or drop changes them [exe DC 0x403c50]. Then:

| State | Effect |
|---|---|
| menu | nothing more [exe DC 0x41793e] |
| inventory | closes the inventory: copies the frame for a crossfade, returns to the previous state, and clears the input lock. With alternate control on, centres the pointer [exe DC 0x417955–0x4179b4] |
| book | closes the journal (see [api.md](api.md#scjournalset)). With alternate control on, centres the pointer [exe DC 0x4179c9–0x4179f9] |
| choice | nothing more [exe DC 0x417a11]. This includes the menu's yes/no prompts |
| overlay | copies the frame for a crossfade, returns to the previous state and plays the overlay's leave sound on a free channel from 14 to 17. The input lock is not touched [exe DC 0x417a3a–0x417a5b] |
| node, zoom (any other) | opens the menu if the setting at 0x1d0 is set, else shuts the game down as `scTerminate` does [exe DC 0x417a23–0x417a69] |

The setting at 0x1d0 is set by `scMenuSet` and by `scEnable(SC_SHOWMENU)`, and
`scDisable(SC_SHOWMENU)` clears it [exe DC 0x41349d, 0x40fbcf, 0x40fdb2]. So after
`scDisable(SC_SHOWMENU)`, Esc in node or zoom view quits the game. Esc in a zoom opens the
menu; it does not zoom out [exe DC 0x417a23].

"Alternate control" is the setting at settings block offset 0x2ac (see
[Settings that change input](#settings-that-change-input)).

### Menu entries that act as keys

The options page of the menu (page 4) has eight entries. Five of them call the key handler
with a key code, so they do exactly what the key does [exe DC 0x413c02–0x413c7c; jump table
data scream.exe VA 0x413620]:

| Entry | Same as |
|---|---|
| 0 | F1 (hints) |
| 1 | F2 (surround) |
| 2 | F9 (camera speed) |
| 3 | F10 (text speed) |
| 4 | F12 (gamma) |
| 5 | toggles slideshow mode, shows "slideshow mode ON" / "OFF", and sets yaw 3.14, look-y 0 [exe DC 0x413c8e–0x413ce4] |
| 6 | toggles alternate control, shows "alternate control ENABLED" / "DISABLED", and sets yaw 3.14, look-y 0 [exe DC 0x413cf6–0x413d4c] |
| 7 | not an input setting [exe DC 0x413d5e] |

The entries' positions on screen are in the [screens spec](screens.md).

---

## Mouse

### The mouse handler

The mouse handler (0x417280) gets the button (0 none, 1 left, 2 right), the event (0 move,
1 down, 2 up) and the position [exe DC 0x419c5b–0x419d79].

1. **Lock.** While the input lock is set, every mouse message is ignored, including moves.
   The stored pointer position is not updated either [exe DC 0x41728f–0x417296].
2. **Position.** Otherwise the position is stored. An x above `W` is set to `W`, and a y above
   `H` is set to `H`; in each case the system pointer is moved there with `SetCursorPos`
   [exe DC 0x41729c–0x41734d]. (The negative tests at 0x4172b2 and 0x4172e0 can't fire,
   because the position is zero-extended.)
3. **Left button up** runs the click for the current state (table below)
   [exe DC 0x4172e9–0x4172f2, 0x417354].
4. **Right button up** runs the right-click rules below [exe DC 0x4172f4–0x4172fe, 0x417425].
5. **Anything else** (a move, or either button going down) runs the hover routine of the
   inventory, menu, choice or book state. In node, zoom, overlay and video state it does
   nothing [exe DC 0x41753d–0x417598; jump table data scream.exe VA 0x41725c].

So every action happens on button **release**. A press only updates the hover.

In node and zoom view the hover does not come from mouse messages. The per-frame routine runs
the node/zoom hover with the stored pointer position once per frame [exe DC 0x412038–0x412047].

The wait flag set by `scWait` blocks only the node, zoom and overlay hover and click
[exe DC 0x4142c4–0x4142d3]. The inventory, menu, choice and book handlers and the right click
do not check it [exe DC 0x417280–0x417598].

### Left click by state

| State | Left button up |
|---|---|
| node | in slideshow mode, a click in an edge zone turns the camera (see the [camera spec](camera.md)); otherwise the [node/zoom click](#node-and-zoom-click) [exe DC 0x4173be–0x417419] |
| zoom | the [node/zoom click](#node-and-zoom-click) [exe DC 0x417410] |
| overlay | any click anywhere closes the overlay: the frame is copied for a crossfade, the input lock is cleared, the previous state returns, and the leave sound plays on a free channel from 14 to 17 [exe DC 0x414501–0x41454b] |
| inventory | the [inventory click](#inventory-click) [exe DC 0x41736a] |
| menu | the menu click, see the [screens spec](screens.md) [exe DC 0x41737f] |
| choice | picks the option under the pointer (see below) [exe DC 0x417394] |
| book | the journal click (see below) [exe DC 0x4173a9] |

**Choice.** A click on an option row ends the choice: the previous state returns, the
feedback text is cleared, the cursor becomes `SC_NORMAL`, the option's index is recorded and
its handler runs [exe DC 0x413f29–0x413f63]. A click elsewhere does nothing
[exe DC 0x413e91–0x413e9e]. When the choice is one of the menu's yes/no prompts, a click on
the first or second row is passed on to the menu [exe DC 0x413ed7–0x413f06]. The rows'
positions are in the [screens spec](screens.md).

**Book (DC).** The book uses fixed pixel positions [exe DC 0x413f80–0x414020]:

- y ≤ 604: the cursor is `SC_USE`. A click with x < 512 turns back a page, and with x > 512
  turns forward. A click at x = 512 does nothing. The turn sound plays only if the page
  changed.
- y > 604: the cursor is `SC_ZOOMOUT`, and a click closes the book. With alternate control
  on, the pointer is then centred.

### Right click

On right button up [exe DC 0x417425–0x417538]:

| State | Effect |
|---|---|
| menu, choice | nothing |
| inventory | closes the inventory (see below) |
| overlay | closes the overlay the same way as the inventory. No leave sound plays |
| book | closes the journal; with alternate control on, centres the pointer |
| node, zoom, while `SC_DREAM` is on | the same closing steps as for the inventory, so the engine swaps to the previous state |
| node, zoom, otherwise | opens the inventory if its first slot holds an item; else nothing |

- **Closing** sets the input lock, draws a frame, copies it for a crossfade, clears the lock,
  returns to the previous state, draws a frame, and raises the tutorial event
  `SC_HINTS_CLOSE`. With alternate control on, it then centres the pointer
  [exe DC 0x417503–0x417538, 0x417466–0x417491].
- **Opening** raises the tutorial event `SC_HINTS_OPEN` (2), then sets the lock, draws a
  frame, copies it for a crossfade, clears the lock and enters the inventory state
  [exe DC 0x4174bf–0x4174f7]. The value 2 is the button number, still in EDX
  [exe DC 0x4172f4, 0x4174d1].
- A held item stays held when the inventory opens or closes this way
  [exe DC 0x417425–0x417538: no call to 0x403c50].

### Node and zoom click

The same routine handles the hover (every frame) and the click (left button up) in node and
zoom view [exe DC 0x4142b0].

1. It does nothing while the wait flag or the input lock is set [exe DC 0x4142c4–0x4142dc].
2. **Overlay state:** a click closes the overlay (above); a hover does nothing
   [exe DC 0x414501–0x41454b].
3. **Edge arrows (node, slideshow mode, hover only, no held item):** if the pointer is in an
   edge zone, the cursor becomes the matching arrow and nothing else is tested
   [exe DC 0x414448–0x4144d5]. See [Edge zones](#edge-zones).
4. **Picking:** the hotspot under the pointer is found (see
   [Hotspot picking](#hotspot-picking)) [exe DC 0x4142f8–0x414303, 0x4144d6–0x4144e1].
5. **Cursor reset:** the cursor image becomes `SC_NORMAL`, or, with an item held, the
   highlight state becomes "plain" (0xca) [exe DC 0x41430c–0x41431e, 0x41442e].
6. **Band:** if y ≤ 64 or y ≥ 704, nothing more happens, for hover and click alike. These are
   fixed pixel values, not derived from `H` [exe DC 0x414323–0x41432e].
7. **No hotspot:** a click with an item held drops it; the cursor becomes `SC_NORMAL`, plain
   [exe DC 0x414755–0x41477e].
8. **Hotspot:** the object is bound (`SC_OBJECT`), on hover as well as on click
   [exe DC 0x41433c–0x414343]. Then:
   - **Hover, no item held:** the cursor shows the object's `scOnHover` value: `SC_NORMAL`,
     `SC_FORWARD`, `SC_LOOK`, `SC_USE`, or `SC_ZOOMOUT` (also for `SC_ZOOMBACK`). Any other
     value leaves the `SC_NORMAL` set in step 5 [exe DC 0x41436d–0x414384, 0x41455a–0x41457d;
     jump table data scream.exe VA 0x414278].
   - **Hover, item held:** the cursor stays the item. It glows (highlight 0xcb) if the
     object's click is `SC_FEEDBACK`, `SC_FEEDBACK_EX` or `SC_CUSTOM`
     [exe DC 0x414582–0x414598].
   - **Click, no item held:** logs "Clicked on %s" and performs the object's
     [action](api.md#actions) [exe DC 0x41468e–0x414744].
   - **Click, item held:** see [Using a held item](#using-a-held-item).

A disabled object can't be hit, because disabled objects are left out of picking
[exe DC 0x416942, 0x417d86]. Each frame's hover therefore leaves the hovered object bound.
In a zoom, the hit test binds every object it tries, so after a miss the zoom's last object
is bound [exe DC 0x417cfa–0x417d05].

### Inventory click

The inventory routine handles hover (any mouse message) and click (left button up)
[exe DC 0x414030]:

1. The slot under the pointer is found (see [Inventory grid](#inventory-grid)). A slot number
   of 60 or more is ignored [exe DC 0x414039–0x41404c].
2. **Cursor reset:** `SC_NORMAL`, or plain (0xca) with an item held
   [exe DC 0x414063–0x414075, 0x4140f0].
3. **Empty slot or no slot:** a click drops a held item; the cursor becomes `SC_NORMAL`,
   plain [exe DC 0x414239–0x414255].
4. **Item in the slot:**
   - Hover, no item held: cursor `SC_USE` [exe DC 0x414098].
   - Hover, item held, over a different item: the item cursor glows (0xcb)
     [exe DC 0x4140fa–0x414103]. Over the held item itself: unchanged.
   - The slot's item is bound (`SC_ITEM`) [exe DC 0x4140a2–0x4140a9].
   - Click, no item held: the item named `journal` opens the journal (DC). Any other item is
     **grabbed**: the cursor becomes its icon, plain, and the tutorial event `SC_HINTS_GRAB`
     is raised [exe DC 0x4140cf–0x414138].
   - Click on the held item: drops it; cursor `SC_USE` [exe DC 0x414149–0x41414b,
     0x41418f–0x4141a3].
   - Click on another item with an item held: see below.

Grabbing does not close the inventory [exe DC 0x414105–0x414148]. To carry an item into the
scene the player closes the inventory with a right click; Esc would drop it.

### Using a held item

There is one held item at a time. It is not removed from the inventory while held
[exe DC 0x403c20: only the held index is set].

**On a scene object** (node or zoom click inside the band) [exe DC 0x414391–0x41467d]:

- If the object's click is `SC_FEEDBACK`, `SC_FEEDBACK_EX` or `SC_CUSTOM`: the item is dropped
  first (cursor `SC_NORMAL`, plain), then the object's `scReaction` list is searched for the
  held item. A match shows the text, plays the voice on channel 13, or calls the handler. No
  match shows the item's fail text and plays its fail voice on channel 13. Details are in
  [api.md](api.md#screaction).
- Any other click kind: the item is dropped and nothing else happens
  [exe DC 0x4145a9–0x4145ea].

**On another inventory item** [exe DC 0x41414d–0x414234]:

- The clicked item's reactions (from `scInventoryReaction`) are searched for the held item.
  A match acts as above. No match shows the held item's fail text and voice.
- Unlike the scene case, the item is dropped **after** the reaction runs, so a `SC_CUSTOM`
  handler runs while the item is still held [exe DC 0x414225–0x414234 → 0x41418f]. The
  cursor then becomes `SC_USE`, plain.

**Dropping** without use happens on: a scene click on empty space inside the band; a scene
click on a switch-type object; an inventory click on an empty slot or on the held item; Esc
in any state; F7 [exe DC 0x414755, 0x4145d6, 0x414239, 0x41418f, 0x417934, 0x41767b].

---

## Cursors

### Images

`scCursor(kind, image)` loads an image into a cursor texture slot [exe DC 0x4148c0–0x41496c]:

| Kind | Slot | Default image (`scream.res`) | Shipped image | Image size |
|---|---|---|---|---|
| `SC_MENU` (3005) | 0x12 | none | `cur-menu.tga` | 32² |
| `SC_NORMAL` (21) | 0x13 | `cursor_def.tga` | `cur-normal.tga` | 64² |
| `SC_FORWARD` (22) | 0x14 | `cursor_def.tga` | `cur-forward.tga` | 64² |
| `SC_LOOK` (23) | 0x15 | `cursor_def.tga` | `cur-look.tga` | 64² |
| `SC_USE` (24) | 0x16 | `cursor_def.tga` | `cur-use.tga` | 64² |
| `SC_ZOOMOUT` (32), `SC_ZOOMBACK` (33) | 0x17 | `cursor_def.tga` | `cur-back.tga` | 64² |
| `SC_WAIT` (25) | 0x39 | `wait_def.tga` | `cur-wait.tga` | 64² |
| `SC_LEFT` (3) | 0x3a | none | `cur-left.tga` | 32² |
| `SC_RIGHT` (4) | 0x3b | none | `cur-right.tga` | 32² |
| `SC_UP` (5) | 0x3c | none | `cur-up.tga` | 32² |
| `SC_DOWN` (6) | 0x3d | none | `cur-down.tga` | 32² |

Sources: slots [exe DC 0x4148c0–0x41496c]; defaults [exe DC 0x4103aa–0x4103ff]; shipped
calls [script DC: top level, after createInventory] [script 2006: same]; sizes
[data DC scratches.res, 2006 scratches.res: TGA headers]. All shipped cursor files are RLE
32-bit TGA with a top-left origin; `cursor_def.tga` and `wait_def.tga` are 32²
[data 2006 scream.res]. The archives also hold `cur-cross.tga` and `cur-spear.tga` (32²),
which no script loads [data] [script DC] [script 2006].

A held item's cursor is the item's icon: the texture slot is the item's id + 0x40, the slot
`scInventorySet` loads the icon into [exe DC 0x414113–0x41411b, 0x412a97–0x412a9f].

### Size and hotspot

The cursor is drawn as a 64×64-pixel quad, whatever the image size, in a pixel projection
that covers the whole window [exe DC 0x40b620, 0x40b4ae–0x40b602]. The pointer is not at the
same point of every cursor [exe DC 0x40b496–0x40b4a7; jump table data scream.exe VA 0x40b3e0]:

| Cursor | Pointer position in the 64×64 quad (from its top-left corner) |
|---|---|
| `SC_MENU`, `SC_FORWARD`, `SC_LOOK` | (8, 8) |
| `SC_NORMAL`, `SC_USE`, `SC_ZOOMOUT` | (24, 24) |
| everything else: wait, edge arrows, item icons | (32, 32), the centre |

For a 32² image such as `SC_MENU` this is image pixel (4, 4). [unverified: that the image is
drawn upright; this depends on how the texture loader orders TGA rows]

### Highlight states

Besides its image, the cursor has a highlight state. The set-cursor routine (0x40b6f0)
treats the values 0xc9, 0xca and 0xcb as highlight states and every other value as an image
[exe DC 0x40b6f0–0x40b718]:

| State | Set when | Drawn as |
|---|---|---|
| 0xc9 | at start, and whenever an item is dropped | the image, white, normal blending |
| 0xca | an item is grabbed, and each hover update with an item held | the same as 0xc9 |
| 0xcb | an item is held over a valid target | the image, then twice more with additive blending; the second extra copy is 1 pixel larger on each side, so the cursor glows |

Sources: drawing [exe DC 0x40b444–0x40b46f, 0x40b529–0x40b5c0]; start values [data
scream.exe VA 0x48bad4 = 0x13, 0x48bad8 = 0xc9].

Esc and F7 drop the item without resetting the highlight state [exe DC 0x403c50]. So if Esc
is pressed while the cursor glows, the next `SC_NORMAL` cursor glows too, until the next drop
sets 0xc9. [unverified: confirmed in the code only, not seen in the running game]

### Which cursor when

| Situation | Cursor |
|---|---|
| node or zoom, hover | see [Node and zoom click](#node-and-zoom-click) |
| node, slideshow mode, pointer in an edge zone, no item held | the arrow for that edge |
| inventory | `SC_NORMAL`; `SC_USE` over an item; the item icon while one is held |
| choice list | `SC_FORWARD` when the list opens; `SC_NORMAL` after a pick [exe DC 0x4158c1, 0x413f41] |
| menu | `SC_MENU` when it opens; `SC_NORMAL` when it closes [exe DC 0x412ed9, 0x41318a] |
| book | `SC_USE`, or `SC_ZOOMOUT` below y = 604 |
| `scWait` (while `SC_WAIT` is on) | the wait cursor; `scGo` sets `SC_NORMAL` [exe DC 0x412590–0x4125c9] |

**When the cursor is drawn** [exe DC 0x416a10]:

- It is not drawn while the input lock is set, unless the wait flag is also set
  [exe DC 0x416b71–0x416b8b, 0x416bf5–0x416c0e, 0x416d59–0x416d72, 0x416f83–0x416fa1].
- It is not drawn in video state [exe DC 0x416d00–0x416d40].
- In the choice state it is drawn every frame [exe DC 0x416e15–0x416e25].
- It is drawn at the stored pointer position, except in node view with alternate control on,
  where it is drawn at the window centre (`W`/2, `H`/2) [exe DC 0x416b91–0x416bbe,
  0x416cf0].

---

## Hotspot picking

### Node view

Picking uses OpenGL selection mode, not a colour render [exe DC 0x40b720–0x40b83f]:

1. **The pick list.** Each enabled object of the current node is drawn into a display list
   as a quad on its cube face, with the object's id as its GL name
   [exe DC 0x4168f0–0x41697b, 0x40b860]. The list is rebuilt only when the player switches to
   a node, and by `scEnable(SC_OBJECT)` / `scDisable(SC_OBJECT)` while in node state
   [exe DC 0x415bba, 0x40faf2–0x40fafe, 0x40fc92–0x40fca2]. Enabling or disabling a node's
   objects while in a zoom or another state does not change the list. Rebuilding binds each
   object in turn, so afterwards the node's last object is bound [exe DC 0x416928–0x41693d].
2. **The pick.** The list is drawn in selection mode with a 32-word selection buffer, a 2×2
   pixel pick region at (x, window-height − y), and the same perspective as the view: the
   field of view at 0x4df8b8c, the viewport's aspect ratio, near 0.1, far 1000
   [exe DC 0x40b72c–0x40b7f9]. The current camera is used as it stands.
3. **The result** is the name of the first hit record in drawing order, which is node object
   order, not depth order [exe DC 0x40b823–0x40b827]. With no hit, or if the buffer overflows
   (more than 8 hotspots under the pick region), the result is "nothing"
   [exe DC 0x40b823–0x40b835].

**Hotspot geometry.** The cube has edge length `S` = the setting at 0x4df8d20 (settings
block 0x1bc, default 1024; see [`scPlace`](api.md#scplace)), and `h` = `S`/2
[exe DC 0x40b872–0x40b88a]. A rectangle (x, y, w, h') on a face is a quad whose corners are
the face pixels (x, y) and (x + w, y + h'), placed as follows [exe DC 0x40b8cc–0x40bba1;
face mapping jump table data scream.exe VA 0x4168cc]:

| Face | Plane | Face pixel (px, py) maps to |
|---|---|---|
| `SC_FRONT` (1) | z = +h | (h − px, h − py, h) |
| `SC_BACK` (2) | z = −h | (px − h, h − py, −h) |
| `SC_LEFT` (3) | x = +h | (h, h − py, px − h) |
| `SC_RIGHT` (4) | x = −h | (−h, h − py, h − px) |
| `SC_UP` (5) | y = +h | (h − px, h, py − h) |
| `SC_DOWN` (6) | y = −h | (h − px, −h, h − py) |

So (0, 0) is the top-left corner of each side face as seen from the centre, and y grows
downwards. Rectangles are not clipped to the face [exe DC 0x40b860: no clamp]. How these
world axes relate to the camera angles is in the [camera spec](camera.md).

Picking and the hover/click rules then follow [Node and zoom click](#node-and-zoom-click),
including the fixed y band 64 < y < 704.

`scEnable(SC_SHOW_HOTSPOTS)` makes the hotspot quads draw additively in colour
(0.5, 1, 1) [exe DC 0x40b88c–0x40b8b7]. The node frame draws them right after the faces and the lighting pass [exe DC 0x4094ef–0x4094f6] (see [rendering.md](rendering.md#node-frame)).

### Zoom view

A zoom is tested in software against the pointer position [exe DC 0x417cd8–0x417db6]:

- The bound zoom's objects are tried in order 1…count; the first match wins.
- An object matches if it is enabled and
  `1.28·x ≤ px ≤ 1.28·x + 1.28·w` and `1.28·y ≤ py ≤ 1.28·y + 1.28·h`, with the stored
  rectangle (inclusive bounds, float compare; constant 1.28 at 0x483e20).
- `scPlace` at zoom level stores y + 64 [exe DC 0x4155fb–0x415602], so a script rectangle
  (x, y, w, h) covers window x from 1.28·x to 1.28·(x + w), and window y from 1.28·(y + 64)
  to 1.28·(y + 64 + h).
- The face argument is not used [exe DC 0x417cd8–0x417db6].

The scale 1.28 is fixed; it does not depend on `W` [exe DC 0x417d17].

### Inventory grid

The DC inventory hit test uses `S` = the setting at 0x4df8d2c (settings block 0x1c8,
default 64) [exe DC 0x417c9c, 0x417db7–0x417e44]:

- Columns c = 1…10: x from 32·c + (c − 1)·`S` to that + `S`.
- Rows r = 0…10: y from 64 + 96·r to 64 + 96·r + `S`.
- The slot number is c + 10·r. Bounds are inclusive. The inventory click ignores slots 60 and
  above [exe DC 0x414049].

With `S` = 64, column 1 spans x 32–96 and row 0 spans y 64–128. Which slots are drawn where
is in the [screens spec](screens.md).

### Other screens

The menu, the choice list and the book have their own fixed hit areas in the same routine
[exe DC 0x417c90, cases 4005 and 4007] and in 0x413f80. They are given in the [screens spec](screens.md).

### Edge zones

The edge zones are recomputed every frame with the viewport [exe DC 0x40df60]. The viewport
is the window less 64 rows at the top and the bottom: (0, 64, `W`, `H` − 128)
[exe DC 0x40df66–0x40dfae]. With `mx` = `W`·15/100 and `my` = (`H` − 192)·15/100 (integer
division) [exe DC 0x40df8c–0x40dfa6, 0x40dfce–0x40dff6]:

| Zone | Alternate control off | Alternate control on |
|---|---|---|
| left | x < `mx` | x < `W`/2 − 1 |
| right | x > `W` − `mx` | x > `W`/2 + 1 |
| up | y < 64 + `my` | y < `H`/2 − 1 |
| down | y > (`H` − 128) − `my` | y > `H`/2 + 1 |

Sources [exe DC 0x40dfc1–0x40dfc8, 0x40e0a1–0x40e0bb, 0x40e007–0x40e021, 0x40e0c5–0x40e0d6].
At 1024×768 with alternate control off, the zones are x < 153, x > 871, y < 150, y > 554.

The edge test returns left or right from x, then up or down from y, and an up or down result
replaces left or right [exe DC 0x407d50–0x407dcd]. "Up" counts only if look-y is at most
1.25, and "down" only if look-y is at least −1.25 [exe DC 0x407d76–0x407d87,
0x407dae–0x407dbf; constants at 0x483214, 0x483218]. The [camera spec](camera.md) describes what the
zones do to the camera.

---

## Settings that change input

| Setting | Where | Effect on input |
|---|---|---|
| slideshow mode | settings block 0x278, default 0; menu option entry 5 | edge arrows and edge-click turns in node view, instead of free look [exe DC 0x414448, 0x4173be, 0x416a6f] |
| alternate control | settings block 0x2ac, default 1; menu option entry 6 | node cursor drawn at the window centre; the pointer is pulled back to the centre (below); the pointer is centred after closing the inventory, an overlay or the book, and after the last zoom closes; edge zones shrink to the centre [exe DC 0x416b91, 0x416a8c, 0x417466, 0x417985, 0x4179ce, 0x413ff1, 0x415d98, 0x40dfb8] |
| `SC_SHOWMENU` | settings block 0x1d0 | Esc opens the menu, else quits |
| `SC_DREAM` | 0x4df8dfc | the right click swaps to the previous state instead of opening the inventory |
| `SC_WAIT` | 0x4df8de4, default 1 | lets `scWait` show the wait cursor and block scene clicks |
| `SC_HINTS` | settings block 0x1d4; F1 | whether the tutorial texts can show (see [`scHints`](api.md#schints)) |

**Pointer recentring.** With alternate control on, the engine pulls the system pointer back
toward the window centre in node view, at most every 30 ms [exe DC 0x416a62–0x416ca7]. The
conditions and step size are in [camera.md](camera.md#alternate-control-dc-only).

---

## 2006 differences

The 2006 exe has the same message handling: the same window procedure, the same three hot
keys and the same Esc check in blocking calls [exe 2006 0x417390, 0x417052–0x417082,
0x417330–0x417367].

**Keys** (handler 0x414a00, registered at 0x40e5be):

- The same key codes and actions, including the aliases and the arrow-key mismatch
  [exe 2006 0x414a09–0x41506d; constants ±1.57 at 0x46aae4].
- Esc has no book branch, and closing the inventory with Esc does not centre the pointer
  [exe 2006 0x414d91–0x414e1e].
- The F10 FAST message is spelled "test speed FAST" [data 2006 scream.exe @0x69067].
- The menu's option entries forward F1, F2, F9, F10 and F12 the same way. The options page
  has slideshow mode but no alternate control [exe 2006 0x411720–0x4117d7; string search].

**Mouse** (handler 0x4147f0):

- The pointer position is not clamped to the window [exe 2006 0x4147fd–0x414803].
- No book state. Closing with a right click does not centre the pointer
  [exe 2006 0x4148e2–0x414995].
- The right-click rules are otherwise the same, with the `SC_DREAM` flag at 0x2f0bdc8
  [exe 2006 0x41491a].

**Picking:**

- The node/zoom band is 64 < y < 536 [exe 2006 0x411c53–0x411c5e].
- The node pick works the same way, with near plane 0.1 as a double
  [exe 2006 0x40ad90–0x40ae3d].
- The zoom hit test is not scaled: `x ≤ px ≤ x + w` and `y ≤ py ≤ y + h` on the stored
  rectangle, in integers [exe 2006 0x415147–0x415161]. `scPlace` at zoom level also adds 64
  to y [exe 2006 0x412e8b–0x412e92].
- The inventory grid has columns 1…8 and rows 0…8, the first row starting at y = 96, and the
  slot number is c + 8·r [exe 2006 0x4150e3, 0x4151c7–0x41522b]. Slots of 52 and above are
  ignored [exe 2006 0x4119d8]. There is no `journal` item check [exe 2006 0x4119c0–0x411a8f].
- The edge zones use the same formulas, without the alternate-control case
  [exe 2006 0x40c370–0x40c410].

**Cursors:** the same set-cursor routine, the same highlight states, the same 64×64 quad and
the same pointer positions [exe 2006 0x40ad60, 0x40aa10–0x40ac81; jump table data scream.exe
VA 0x40a9f8; start values VA 0x4722a4 = 0x13, 0xc9]. The node cursor is always drawn at the
pointer [exe 2006 0x41428a–0x4142a7]. The only `SetCursorPos` call is a centring helper
[exe 2006 0x417180–0x4171a0, called from 0x413207 and 0x413622]; there is no pointer
recentring.

---

## Open questions

- [unverified: what the camera keys visibly do outside node view]
- [unverified: that cursor images are drawn upright (texture-loader row order)]
- [unverified: the glow left after Esc while an item glows, seen only in the code]
