# Main loop and state machine

This page describes how the original SCream engine (`scream.exe`, the engine of *Scratches*)
runs from frame to frame: its threads, the main loop, the view states and the moves between
them, what each state draws and what input does in it, timing, and start-up and shut-down as
the player sees them. It does not describe any re-implementation.

The `sc*` functions are in [`api.md`](api.md) and file formats in [`formats.md`](formats.md).
This page links to them rather than repeating them. Camera motion, drawing effects, screen
layouts and key bindings are in the [camera spec](camera.md), the [rendering spec](rendering.md), the [screens spec](screens.md) and
the [input spec](input.md).

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
| `[data FILE …]` | a constant or table in a shipped file; "data scream.exe" is the exe image | |
| `[unverified: …]` | not confirmed in any of the above | |

Addresses are virtual addresses (image base 0x400000). Both executables were built with Open
Watcom, which passes arguments in registers (EAX, EDX, EBX, ECX). A claim marked "derived"
follows from the cited code but was not watched in the running game.

---

## Threads

The engine runs three threads [exe DC 0x419850]:

| Thread | Runs | Stops |
|---|---|---|
| **Main** | the message loop, every frame, every Lua handler and every blocking `sc*` call | when the process exits |
| **Audio** | updates sound channels 1–17 over and over, calling `Sleep(1)` between updates [exe DC 0x419540, 0x401af0] | when the audio layer is shut down or fails to start [exe DC 0x401b0d; flag written at 0x40208e, 0x401e94, 0x4021c1] |
| **Playlist** | runs the playlist update over and over, calling `Sleep(1)` between updates [exe DC 0x419570, 0x40ee00] | when `scTerminate` runs [exe DC 0x41078b, 0x40f01c] |

- Both worker threads are created when the window is created, inside `scInit`. If either
  can't be created, the engine shows "Could Not Create Audio Thread" or "Could Not Create
  Playlist Thread" and asks the main loop to quit [exe DC 0x41993f–0x4199af].
- A critical section guards the channel data shared by the threads [exe DC 0x4199b4,
  0x419dc0, 0x419dd0, 0x401b25].
- The worker threads don't depend on frames. Fades and track change-overs go on during
  loading, blocking calls and long frames (derived).
- The playlist update does nothing while a view change (`scSwitch`) is in progress
  [exe DC 0x40ee2e; flag set at 0x4158d8 and cleared at the ends of 0x4158d0]. The same flag
  is also set during parts of `scScriptGo` [exe DC 0x4156df, 0x415713, 0x415799, 0x4157d7].
  [unverified: which parts of a dialogue hold the playlist]
- The playlist's own timing is in [api.md](api.md#music-playlist).
  [unverified: the real update rate; `Sleep(1)` usually waits longer than 1 ms on Windows]

## The main loop

`scRun` runs a standard Windows message loop until a `WM_QUIT` message arrives
[exe DC 0x419b00]:

- If a message is waiting, the loop removes it and dispatches it to the window procedure.
  Mouse and key handling, clicks, and the Lua handlers they call all run inside that
  dispatch [exe DC 0x419b29–0x419b34, 0x419bd0].
- If no message is waiting, the loop runs one frame: the per-frame routine below
  [exe DC 0x419b3b; routine registered at 0x4105e1].
- So frames run only when the message queue is empty, one per pass.

There is no sleep and no frame cap in the loop [exe DC 0x419b00–0x419b41]. The only limit is
the buffer swap:

- If `VSYNC` in `scream.cfg` is non-zero (default 1) and the driver offers
  `WGL_EXT_swap_control`, the engine sets a swap interval of 1 [exe DC 0x419917–0x41992e,
  0x4194b0, 0x419526].
- Otherwise the frame rate is not limited (derived).

When the loop sees `WM_QUIT`, it returns, `scRun` returns, and the script goes on to its next
statement. In both shipped scripts that is `scTerminate()` [exe DC 0x419b1b; script DC: top
level; script 2006: top level].

Only one thing in the engine asks the loop to quit: a `WM_CLOSE`, which the window procedure
turns into `PostQuitMessage` [exe DC 0x419d8a]. The media layer's start-up errors do the same
(see [Start-up](#start-up-as-the-player-sees-it)). Every other way out ends the process
directly (see [Quitting](#quitting)).

## The per-frame routine

Each frame does the following, in this order [exe DC 0x412010]:

1. **Hover.** Tests what is under the pointer and sets the cursor shape. This is skipped while
   input is locked or the wait flag is set. It acts only in node and zoom view ([input spec](input.md))
   [exe DC 0x412047, 0x4142b0–0x4142e0].
2. **Draw.** Draws the frame for the current state (see [What each state draws](#what-each-state-draws))
   [exe DC 0x41204c, 0x416a10].
3. **Present.** Swaps the buffers [exe DC 0x412051–0x41205a, 0x419bc0].
4. **Subtitles (DC).** Advances the `.srt` subtitle player, which runs on its own clock (see
   [formats.md](formats.md#the-player)) [exe DC 0x41205f, 0x4180b0].
5. **Once-a-second tick.** At least one second after the last tick, the frame takes a new tick
   [exe DC 0x412064–0x412079; 0.001 at 0x48397c]:
   - global timers count, but only if the current state is node view (4000)
     [exe DC 0x41207f–0x412089];
   - the timers of the current room count, in any state [exe DC 0x412121–0x412138];
   - a timer that reaches its seconds is called by name (see
     [api.md](api.md#timers)) [exe DC 0x41210f, 0x4121ae].

The tick time is reset to "now", not advanced by one second [exe DC 0x412079]. So each tick
comes on the first frame at least 1000 ms after the one before. Ticks drift late, and a long
frame (a node load, say) gives one tick, not several (derived).

A timer handler runs inside this routine. Blocking calls also run this routine (see
[Blocking calls](#blocking-calls-and-nested-loops)). So a timer can fire, and its handler
can run, while another handler is still blocked in `scSleep` or `scScriptGo` (derived from
[exe DC 0x4124f0, 0x415773, 0x41210f]).

## Frame timing

**There is no fixed time step.** Most animation advances by a fixed amount per frame, times a
**speed factor** that tries to make it independent of the frame rate. The factor is computed
in the frame-begin step that every state runs [exe DC 0x416a42, 0x407c30, 0x40e7f0]:

- The engine counts frames per second, sampling once a second
  [exe DC 0x40e80c–0x40e83b; 0.001 at 0x483554].
- The factor is 60 divided by the **running mean of every per-second count since start-up**.
  It starts from one sample of 60 fps, so the factor starts at 1.0
  [exe DC 0x40e85e–0x40e890; data scream.exe @0x48bb74 = 60.0, 1.0; 60.0 at 0x483560;
  default 1.0 at 0x40f2a7].
- Once that mean is above 300 fps, sampling stops and the factor freezes
  [exe DC 0x40e841–0x40e85c; 300.0 at 0x48355c].
- Every `scSwitch` to a node or zoom, and every zoom-out, restarts the current one-second
  window without taking a sample, so loading time doesn't count [exe DC 0x415971, 0x415ae3,
  0x415c5c, 0x415d7a → 0x407300; 0x40e8d2–0x40e8e5].
- The factor is field 0x020 of the settings block, so it is saved with a game and restored by
  loading one ([formats.md](formats.md#3-settings-block)) [exe DC 0x40e890; block base
  0x4df8b64].

Because it is a mean over the whole session, the factor follows changes in frame rate only
slowly. A sudden slowdown makes scaled animation slower in real time (derived).

| Timing | What uses it |
|---|---|
| per frame × speed factor | camera turning [exe DC 0x408055, 0x408161, 0x4081ee]; smooth `scLookAt` [exe DC 0x412760]; crossfades and fades in node, zoom, inventory, overlay, menu and journal drawing [exe DC 0x40963b, 0x40aa97, 0x4098d9, 0x40ac48, 0x40941c, 0x40b1fd]; feedback text [exe DC 0x40ad4c, 0x40adfe, 0x40afd7]; the scrolling of the clouds, fog and rain [exe DC 0x40d9b1, 0x40dc9e, 0x40dda4]. The sky colour fades, the cloud-layer crossfade and the gradient slide are not scaled ([rendering.md](rendering.md#frame-time-factor)) |
| per frame, **not** scaled | the `SC_DREAM` field-of-view swing: ±0.005 per frame between 35 and 55 [exe DC 0x407c40–0x407c97; 0.005 at 0x483210, −0.005 at 0x48320c] |
| wall clock (`GetTickCount`) | timer ticks (above); `scSleep` [exe DC 0x4124bf–0x412512]; subtitles [exe DC 0x4180c7]; video frames [exe DC 0x419f50]; playlist change-overs [exe DC 0x40ee1a]; the pointer pull in alternate control (below) [exe DC 0x416a19–0x416ab3] |
| the audio thread's own loop | channel fades [exe DC 0x401310, 0x419540] |

[unverified: how video frames are dropped or held when the frame rate is below the video's]

## States

### State values

The current state is one of these values. Setting any other value is fatal ("invalid state")
[exe DC 0x4029ec–0x402a1b; message at 0x4826cd]. The constants are defined in `scream.sc`, but
neither game script uses them [script DC: scream.sc top level; script DC: no use of `SC_ST*`].

| Value | Name | Meaning |
|---|---|---|
| 4000 | `SC_STNODE` | node view: the 360° scene |
| 4001 | `SC_STVIDEO` | a video (`scCutscene`) is playing |
| 4002 | `SC_STZOOM` | zoom view (includes the built-in `black` zoom) |
| 4003 | `SC_STINV` | the inventory is open |
| 4004 | `SC_STOVERLAY` | an overlay is shown over the previous view |
| 4005 | `SC_STMENU` | the main menu |
| 4006 | `SC_STLAST` | not a real state: a request to go back (see below) |
| 4007 | `SC_STCHOICE` | a choice list (dialogue choices, or a yes/no prompt in the menu) |
| 4008 | `SC_STBOOK` | the journal (DC only) |

Before `scRun` the current and previous states are both 0. Both live in zero-filled data
[data scream.exe: VA 0x49fc70 lies past the raw data of the DGROUP section, which ends at
0x49c000]. No frame is drawn before `scRun`.

### Current and previous state

The engine keeps a **current** and a **previous** state. Setting a state follows three rules
[exe DC 0x402d20]:

| Requested | Effect |
|---|---|
| 4006 (back) | swaps current and previous [exe DC 0x402d5a–0x402d6b] |
| 4004 (overlay) | current becomes 4004; previous is **left as it was** [exe DC 0x402d3e–0x402d50] |
| anything else | previous becomes the old current; current becomes the new value [exe DC 0x402d46–0x402d50] |

Consequences (derived from the rules above):

- **Going back is a swap.** After a "back", the previous state is the one just left. Going
  back again returns to it.
- **Overlay.** An overlay does not record the view it was opened from. Leaving it (a
  "back") returns to the state that was previous *before* the overlay opened, and the
  overlay draws that state behind itself (see the table below). Opened from a zoom that was
  reached from node view, an overlay shows the node behind it and returns to the node.
  Scratches never uses overlays [script DC, script 2006: no use of `SC_OVERLAY`].
- **Journal.** Opening it from the inventory goes back first, then enters 4008
  [exe DC 0x4185ca, 0x4185d4]. So the previous state is the view the inventory was opened
  from. Closing the journal (a "back") returns to that view, not to the inventory
  [exe DC 0x418612].
- **Menu.** The menu keeps its own copy of the state to return to, separate from "previous"
  [exe DC 0x412ec7–0x412ed3, 0x413194–0x41319a].

### What each state draws

The draw step picks its work from a table indexed by state − 4000 [exe DC 0x416a47–0x416a56;
table data scream.exe @0x4169d0]. 4006 and any value outside 4000–4008 use the node entry
[data scream.exe @0x4169d0: entries 0 and 6, and the range check at 0x416a51].

Every state first runs the **frame-begin** step. It updates the frame counter and speed
factor, clears the screen, moves the `SC_DREAM` field of view, and sets up the 3-D view
[exe DC 0x416a42, 0x407c30]. Then:

| State | Drawn, back to front | Cursor | Source |
|---|---|---|---|
| node (4000; also 4006 and invalid values) | camera update from the pointer ([camera spec](camera.md)), then the node scene with sky, objects and effects ([rendering spec](rendering.md)), feedback text | yes; at the screen centre when alternate control is on, otherwise at the pointer | [exe DC 0x416a5d–0x416bdc] |
| video 4001 | the current video frame and feedback text (subtitles show over video). On the frame where the video has ended, the last frame is drawn and captured, and the state goes back | **never** | [exe DC 0x416d00–0x416d40] |
| zoom 4002 | the zoom scene, feedback text | at the pointer | [exe DC 0x416d94–0x416ddf] |
| inventory 4003 | the inventory screen ([screens spec](screens.md)), feedback text | at the pointer | [exe DC 0x416be6–0x416c27] |
| overlay 4004 | the *previous* state's view: node (no camera update), zoom or inventory; nothing for other previous states. Then the overlay image, and the crossfade from the captured frame. Then feedback text | at the pointer | [exe DC 0x416ec6–0x416f43, 0x40aae0] |
| menu 4005 | the menu background with the sky pass, if a background is set; the current menu page ([screens spec](screens.md)); feedback text | at the pointer | [exe DC 0x416f48–0x416fe7, 0x4083c0, 0x408550] |
| choice 4007 | the *previous* state's view, then the option list. Node is drawn without a camera update. A menu prompt draws the menu, then the list in its menu style. With video or overlay as previous, neither view nor list is drawn. **No feedback text** | at the pointer, **always**, even when locked | [exe DC 0x416de4–0x416e25, 0x416e42–0x416ec1; table data scream.exe @0x4169f4] |
| book 4008 | the journal image as a backdrop with the sky pass, the journal text, feedback text | at the pointer | [exe DC 0x416d45–0x416d92, 0x4083c0, 0x40b0a0] |

- **Cursor rule.** Except in choice and video states, the cursor is drawn only when input is
  unlocked or the wait flag is set (the wait cursor) [exe DC 0x416b71–0x416b8b, 0x416cde,
  0x416dbd–0x416ddd].
  - So `scLock` without `SC_WAIT` hides the cursor.
  - So every capture taken with input locked (below) leaves the cursor out (derived).
- **Alternate control (DC).** "Alternate control" is a menu toggle, on by default
  [exe DC 0x40f451, 0x413cf6–0x413d3a; strings "alternate control ENABLED/DISABLED" at
  0x483b0b, 0x483af0]. It is field 0x2ac of the settings block
  ([formats.md](formats.md#3-settings-block)). While it is on, in node view, the system pointer
  is pulled back toward the window centre and the cursor is drawn at the centre
  [exe DC 0x416a8c–0x416b5d, 0x416b91–0x416bbe]. The conditions, the step and how it drives
  the camera are in [camera.md](camera.md#alternate-control-dc-only).
- **Debug text.** The frame also has a debug-text pass. Its flag is cleared at render
  start-up and nothing sets it again, so it never draws [exe DC 0x40b030, 0x40725c → 0x40e0f0
  with 0].
- **Pointer memory.** At the end of every frame the pointer position is stored as "last
  frame's position", for the [camera spec](camera.md) [exe DC 0x416bc8–0x416bd7].
- **Sky.** The sky and cloud pass runs only inside the node, zoom and backdrop drawing. So
  the sky moves only while one of those is on screen [exe DC 0x409542, 0x40a929, 0x408466 →
  0x40d990].

### The capture step

Almost every change of state first **captures** the screen:

1. It sets the input lock, which hides the cursor.
2. It draws one frame of the old state without presenting it.
3. It copies the frame to a texture.
4. It clears the lock.

The new state then crossfades from that picture ([rendering spec](rendering.md)) [exe DC 0x407ad0; e.g.
0x415957–0x41596b, 0x4174d8–0x4174f1, 0x4185a9–0x4185c4].

The view changes clear the lock as part of this, which is why they end a script's `scLock`
(see [api.md](api.md#sclock--scunlock)). The menu's open/close, the inventory and the
journal use the same steps [exe DC 0x412e8b–0x412e9c, 0x4130fb–0x413112, 0x417955–0x41796c].

### Transitions

| From | Trigger | To | Source |
|---|---|---|---|
| start | `scRun`, no menu defined | node 4000 | [exe DC 0x410689–0x410697] |
| start | `scRun`, menu defined (`scMenuSet`) | menu-open sequence → 4005 | [exe DC 0x4106c0, 0x412cb0] |
| any | `scSwitch(SC_NODE)`, `scSwitch(SC_ROOM)` | node 4000 | [exe DC 0x415bd8–0x415be2] |
| any | `scSwitch(SC_ZOOM)` | zoom 4002 | [exe DC 0x415c85–0x415c8a] |
| zoom | `scSwitch(SC_ZOOMOUT)` on the last zoom, `scSwitch(SC_ZOOMBACK)` | node 4000 | [exe DC 0x415d7f–0x415d84, 0x415976–0x41597b] |
| any | `scSwitch(SC_OVERLAY)` | overlay 4004 (previous kept) | [exe DC 0x4159f8–0x415a03] |
| any | `scCutscene` | video 4001 | [exe DC 0x412425–0x41242a] |
| video | video ends; or Esc while it plays | back | [exe DC 0x416d18, 0x412478] |
| any | `scChoiceGo` with at least one option | choice 4007 | [exe DC 0x4158b7–0x4158bc] |
| choice | clicking an option | back, then the option's handler | [exe DC 0x413f32–0x413f37]; see [api.md](api.md#scchoicego) |
| menu | a menu yes/no prompt | choice 4007 over the menu | [exe DC 0x413000–0x413005] |
| menu prompt | answered | back to the menu | [exe DC 0x4130a1–0x4130ae] |
| node, zoom | right-button release, `SC_DREAM` off, an item in inventory slot 1 | inventory 4003 | [exe DC 0x4174b5–0x4174f7; slot test 0x403d20] |
| inventory | right-button release, or Esc | back | [exe DC 0x417503–0x417524, 0x417955–0x417976] |
| inventory | clicking the item named `"journal"` with nothing in hand (DC) | book 4008 | [exe DC 0x4140df → 0x418580] |
| book | right-button release, or Esc | back | [exe DC 0x417461, 0x4179c9 → 0x4185f0] |
| overlay | left click, right-button release, or Esc | back. A left click and Esc play the overlay's leave sound; the right-button release doesn't | [exe DC 0x414501–0x41454b, 0x417503, 0x417a3a–0x417a5b] |
| node, zoom, other | Esc with a menu defined | menu-open sequence → 4005 | [exe DC 0x417a23–0x417a2c] |
| node, zoom, other | Esc with no menu defined | the process ends (`scTerminate`'s routine) | [exe DC 0x417a69] |
| any (in game) | `scAbort`, `scMenuShow` | menu-open sequence → 4005 | [exe DC 0x410711, 0x405720 → 0x412cb0] |
| menu | "back to the game" with a game in progress | the state saved when the menu opened | [exe DC 0x41315b, 0x413185–0x41319a] |
| menu | Quit, confirmed | outro handler, then the process ends | [exe DC 0x413e3d–0x413e5b] |

**The right-button quirk under `SC_DREAM`.** With `SC_DREAM` on, a right-button release in
node or zoom view doesn't open the inventory. It takes the inventory's *close* path instead:
capture, then "back" [exe DC 0x4174b5–0x4174bd → 0x417451 → 0x417503–0x417524]. So it swaps
to whatever state was previous (derived). Scratches turns `SC_DREAM` on in a dream sequence [script DC: enterDreamOne] [script 2006:
one call of `scEnable(SC_DREAM)`].
[unverified: whether this was seen in play; which states can be previous at those moments]

**Menu-open sequence (DC)** [exe DC 0x412cb0] ([screens spec](screens.md) for what it shows):

1. Loads the menu background.
2. If no game is in progress, the intro handler is defined, and `scAbort` has not set its
   flag, it runs the `SC_INTRO` handler first [exe DC 0x412cdf–0x412cef, 0x412f31–0x412f46].
3. Checks the save slots.
4. If a game is in progress, fades out sound channels 1–18 [exe DC 0x412e4a–0x412e7d,
   0x401b20]. This is an off-by-one in the original: there are only 17 channels, and the loop also passes the non-existent channel 18 [exe DC 0x401af0: channels 1–17] [unverified: what the call for channel 18 touches].
5. Captures the screen.
6. `scSwitch(SC_ZOOM, "black")`, `scSleep(1)`, `scSwitch(SC_ZOOMOUT)` [exe DC 0x412e9c–0x412ec2].
7. Remembers the now-current state (normally the view the player left), and sets the menu
   cursor [exe DC 0x412ec7–0x412ede].
8. Enters 4005.
9. Loads and plays the `scMenuSetEx(SC_STEREO)` file on channel 7 [exe DC 0x412ee3–0x412f06;
   file stored at menu+0x38c by 0x413581].
10. Sets the cloud colour to `SC_STORM` at once, keeping the old preset to restore on close
    [exe DC 0x412f0b–0x412f1f, 0x418678].

**Menu close (resume)** [exe DC 0x4130d0]:

1. Fades out channel 7.
2. Captures the screen.
3. `scSwitch(SC_ZOOM, "black")`, `scSleep(2)`, `scSwitch(SC_ZOOMOUT)`.
4. Stops channel 7.
5. If a game is in progress: sets the normal cursor, returns to the remembered state, and
   fades channels 1–18 back in, except 7 [exe DC 0x41319f–0x4131bb, 0x401b70]. The same
   off-by-one as on opening.
6. Restores the cloud colour preset [exe DC 0x413165–0x41317b].

With no game in progress it skips step 5. [unverified: how the menu's new-game and load
paths then set the state; [screens spec](screens.md)]

## Input by state

Mouse and key events reach the engine only through the window procedure, so only while the
main loop is dispatching (see [Blocking calls](#blocking-calls-and-nested-loops)). The details
are in the [input spec](input.md). The state-dependent routing is:

- **Mouse, any state.** While input is locked, every mouse event is ignored, including
  movement. The stored pointer position is not updated, so a cursor drawn in wait mode stays
  where it was [exe DC 0x41728f–0x417296].
- **Left-button release:**
  - inventory, menu, choice and book each have their own click routine
    [exe DC 0x417354–0x4173bd; table data scream.exe @0x417244];
  - all other states use the world click (node, zoom, overlay dismissal) [exe DC 0x417410–0x417419].
- **Right-button release:** see [Transitions](#transitions). It does nothing in menu and
  choice states [exe DC 0x417425–0x41743f].
- **Other mouse events** (movement, button presses) give hover feedback in inventory, menu,
  choice and book [exe DC 0x41753d–0x41759e; table data scream.exe @0x41725c]. In node and
  zoom, hover is done every frame instead (see [The per-frame routine](#the-per-frame-routine)).
- **Esc:**
  - in every state, first drops the item in hand [exe DC 0x417934, 0x403c50];
  - then by state: nothing in menu and choice; back from inventory, overlay and book; in any
    other state, opens the menu, or ends the process if there is no menu
    [exe DC 0x417939–0x417a69].

  The Esc branch doesn't test the input lock, so Esc works in the main loop even while a
  script holds `scLock` [exe DC 0x417934–0x417a69].
- **Closing the inventory or journal with alternate control on** puts the pointer back at the
  window centre [exe DC 0x417466–0x417491, 0x417985–0x4179af, 0x4179ce–0x4179f9].

## Blocking calls and nested loops

The blocking calls listed in [api.md](api.md#conventions) don't return to the main loop. Each
runs its own loop on the main thread, inside whatever handler called it. While one runs:

- **Frames go on.** Each pass runs the full per-frame routine: hover, draw, present,
  subtitles and timer ticks [exe DC 0x412449, 0x4124f0, 0x41280c, 0x415773, 0x41615b].
- **The window procedure doesn't run.** These loops never dispatch messages, so no click,
  mouse move or key reaches the engine [exe DC 0x419b80].
- **Esc is the only input.** `scCutscene`, `scSleep`, `scScriptGo` and `scFXPlayEx` with
  `wait` check for Esc once per pass. The check takes one message off the queue and returns
  true only if it was an Esc key-down. **Any other message is thrown away**
  [exe DC 0x419b80–0x419bb7; callers 0x41244e, 0x412516, 0x415778, 0x416160]. So clicks and
  keys made during these calls are lost (derived).
- **No input check at all.** A smooth `scLookAt` doesn't touch the queue
  [exe DC 0x412670: no call to 0x419b80 or 0x419b00]. Input made during the turn waits in the
  queue and is handled after the turn, once the main loop runs again (derived).
- **Closing the window during a blocking call.** The window procedure turns `WM_CLOSE` into a
  `WM_QUIT`. The next Esc check then removes that `WM_QUIT` and throws it away, so the main
  loop never sees it (derived).
  [unverified: Windows delivery of `WM_CLOSE` during these loops; not tested]
- **Nesting.** Handlers can nest. A timer handler called from inside a blocking loop can
  itself call a blocking function, which starts another loop (derived from
  [exe DC 0x4124f0 → 0x412010 → 0x41210f]).
- **The state can change under a blocking call.** `scCutscene` loops only while the state
  is 4001 [exe DC 0x412439–0x412443]. `scSleep` loops for a time, whatever the state
  [exe DC 0x4124c9–0x41251d].

`scChoiceGo` does **not** block. The script continues, and the choice state is handled by the
main loop [exe DC 0x415850].

## Window, focus and quitting

### Window

- **Window.** The window has the title "Scratches" and window class "Nucleosys"
  [exe DC 0x4102f7, 0x410618; 0x419680, string at 0x483f1c].
  - Full-screen: a pop-up window, after a display-mode change to the configured size, depth
    and refresh rate [exe DC 0x4196fd–0x419702, 0x4195a0].
  - Windowed: an ordinary overlapped window [exe DC 0x419776].
  - If the mode change fails, the engine shows "WARNING: Could not switch to fullscreen mode"
    and carries on [exe DC 0x419653].
- **System pointer.** The system pointer is hidden once the window exists
  [exe DC 0x419785]. It is shown only around error message boxes [exe DC 0x419b50].
- **Messages.** The window procedure and the hot keys (F10, Print Screen, Alt+Print Screen)
  are described in [input.md](input.md#windows-messages) [exe DC 0x419bd0, 0x4198a2–0x4198d2].
  A `WM_SIZE` stores the new client size as the screen size and rebuilds the projection
  [exe DC 0x419c01–0x419c17, 0x4170b0].
  [unverified: Windows hot keys are system-wide, so these may be captured while another
  program has focus]

### Focus

The window procedure handles no activation or focus message [exe DC 0x419bd0: no case for
`WM_ACTIVATE`, `WM_ACTIVATEAPP`, `WM_SETFOCUS` or `WM_KILLFOCUS`]. So nothing pauses when the
game loses focus:

- frames keep drawing while the queue is empty;
- timers keep counting;
- sound and the playlist keep playing.

(Derived.) [unverified: what Windows does with a full-screen OpenGL window on Alt+Tab]

### Quitting

| How | What the player sees | Source |
|---|---|---|
| Esc with no menu defined | The `scTerminate` routine runs at once, from the key callback: input locks, there is a crossfade to the `black` zoom, about a second of black, then the process exits. `scRun` never returns | [exe DC 0x417a69, 0x410740] |
| Menu Quit, confirmed | Channel 7 (menu music) fades. The screen crossfades to black over `scSleep(2)`. Channel 7 stops. The `SC_OUTRO` handler runs (the credits video in Scratches), unless `scAbort`'s flag is set. Then the `scTerminate` routine as above | [exe DC 0x413df0–0x413e5b] [script DC: outro] [script 2006: outro] |
| Closing the window (`WM_CLOSE`, e.g. Alt+F4) | `scRun` returns and the script calls `scTerminate()`, with the same fade and exit | [exe DC 0x419d8a, 0x419b1b] [script DC: top level] |
| Fatal errors (bad names, "invalid state", "Too many timers!") | a message box, then exit | see [api.md](api.md#conventions) |

The `scTerminate` routine itself is described in [api.md](api.md#scterminate). After the black
fade it:

1. stops the playlist thread and the playlist;
2. shuts down audio, which ends the audio thread;
3. frees textures and closes the archive;
4. releases the GL context, restores the display mode if it was full-screen, and shows the
   system pointer again;
5. exits with code 0.

[exe DC 0x410778–0x4107bf, 0x402110, 0x419a80]

## Start-up as the player sees it

1. **Window.** `scInit` opens the window (full-screen by default) and hides the pointer. The
   media layer starts only if the media-layer flag (settings field 0x030, default 1) is set.
   Nothing changes that flag except loading a saved settings block [exe DC 0x410320,
   0x40f2f8].
2. **Loading image.** After the built-in textures, the game archive and the splash are
   loaded, one image is drawn full-screen and presented: the `SC_SPLASH` file if it exists
   (both scripts use `back-loading.tga`), otherwise the built-in `sc_logo.tga`
   [exe DC 0x410473–0x41047d, 0x410641–0x41064b, 0x410499–0x4104a3, 0x407a20, 0x419bc0]
   [script DC: top level] [script 2006: top level]. That image is also captured, so the
   first crossfade starts from it [exe DC 0x4104d2].
3. **World building.** The script then sets cursors, fonts and the menu, and builds the whole
   world. No frame is drawn and no message is handled during this, so the loading image
   stays on screen (derived from [script DC: top level]; no frame or message call in
   `scInit`'s return path [exe DC 0x4104d2–0x4105d1]).
4. **`scRun`.** Puts the pointer position at the centre [exe DC 0x410661–0x410684].
   - With a menu defined (both games): runs the menu-open sequence. With no game in progress
     it first runs the `SC_INTRO` handler. In Scratches that plays the logo videos
     (`vid-logo_us.ogg`, then `vid-logo.ogg` with `fx-logo.ogg`) as blocking cutscenes,
     which Esc can skip one at a time. Then the screen crossfades through black, the menu
     appears and the menu music starts [exe DC 0x4106c0, 0x412cb0] [script DC: intro]
     [script 2006: intro].
   - Without a menu: node view, with whatever room and node the script made current.
   - Then the main loop starts [exe DC 0x4106b5–0x4106c7].

Start-up failures:

- "Please insert the game CD" if the archive can't be opened (see [api.md](api.md#scinit)).
- "ChoosePixelFormat failed" or "SetPixelFormat failed" in a message box, after which the main
  loop quits at once [exe DC 0x4197b0, 0x4198ef–0x4198f4].
- The thread errors above.

## 2006 differences

The 2006 engine has the same structure. Callbacks are registered the same way: the per-frame
routine 0x40fda0, resize 0x414650, mouse 0x4147f0, keys 0x414a00, audio 0x401ae0 and playlist
0x40cf70 [exe 2006 0x40e5a0–0x40e5d7]. It also has the same message loop, window procedure,
hot keys and threads [exe 2006 0x4172b0, 0x417390, 0x417000–0x417174]. The differences:

| Area | 2006 | Source |
|---|---|---|
| States | 4000–4007 only; 4008 is "invalid state" (no journal) | [exe 2006 0x4029ac–0x4029bf] |
| State rules | same three rules for back, overlay and others | [exe 2006 0x402ce0] |
| Draw table | the same entries for 4000–4007 | [exe 2006 0x414220–0x414237; tables data scream.exe @0x4141e4, @0x414204] |
| Alternate control | absent: no pointer pull and no centred cursor in node view | [exe 2006 0x41423e–0x4142b4] |
| Closing the inventory | doesn't re-centre the pointer | [exe 2006 0x414de6–0x414e1e] |
| Pointer memory | not stored at the end of the frame | [exe 2006 0x4142a7–0x4142b4] |
| Subtitles | no subtitle step in the per-frame routine | [exe 2006 0x40fdda–0x40fe06] |
| Speed factor | the same scheme: 60 / running mean, frozen above 300 fps, starts at 60 / 1 | [exe 2006 0x40ca40–0x40cb3f; data scream.exe @0x46a354 = 300.0, 60.0; @0x472344 = 60.0, 1.0] |
| `SC_DREAM` field of view | swings ±0.005 per frame between 43 and 63; when off it is set to 53 (the DC uses 35–55 and 45) | [exe 2006 0x407878–0x407908; 0.005 / −0.005 at 0x46a070–0x46a074] |
| `scSleep` | **no Esc check and no message call** (see [api.md](api.md#scsleep)). Input during a 2006 sleep is queued and handled afterwards, not thrown away (derived) | [exe 2006 0x41027e; Esc check 0x417330 called only from 0x4101de, 0x412feb, 0x413960] |
| Esc | same per-state behaviour, without the book | [exe 2006 0x414d91–0x414e4f] |
| `SC_DREAM` right-button quirk | present | [exe 2006 0x41491a–0x414922] |
| Menu open | same order: intro handler when no game is running, channel fade, capture, black zoom, `scSleep(1)`, zoom out, remember the state, menu cursor, 4005, music on channel 7 | [exe 2006 0x4109c0–0x410bf1, 0x410c03–0x410c1c] |
| Menu cloud colour | no change: the 2006 menu-open routine has no ambience call | [exe 2006 0x4109c0–0x410c3a] |
| Start-up | same: splash drawn, presented and captured | [exe 2006 0x40e49c–0x40e4c1] |
| `scRun`, `scTerminate`, `scAbort` | same sequences | [exe 2006 0x40e620–0x40e692, 0x40e700–0x40e790, 0x40e6a0–0x40e6fd] |

[unverified: the 2006 hover, click and journal-free inventory routines beyond the
state-change sites cited above]
