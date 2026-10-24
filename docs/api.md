# sc* API reference

The game script of *Scratches* drives the SCream engine (`scream.exe`) through global Lua
functions whose names start with `sc`. This page describes what each one does in the original
engine. It does not describe any re-implementation. File formats are in
[`formats.md`](formats.md).

> **AI-assisted.** Claude (Anthropic's AI model) wrote this page. The sources were Ghidra
> decompilation and disassembly of the two executables below, the shipped scripts decompiled
> with unluac, and measurements of the shipped data. A person has not yet checked it. A
> source tag says where a claim can be checked; it is not a guarantee. Check a claim against
> its source before you build on it, and treat `[unverified]` claims as open questions.

## Sources

| Tag | Means | File (MD5) |
|---|---|---|
| `[exe DC 0x…]` | code or data at that address in the DC update's `scream.exe` (May 2007, 649728 bytes) | `f696d90b1f1fe5eb050beda06781dcc8` |
| `[exe 2006 0x…]` | the same in the patched 2006 `scream.exe` (April 2006, 543744 bytes) | `5de7958817eecb3815d816d123ab9217` |
| `[script DC: name]` | the Lua function or top-level code of that name in the DC `scratches.sc` (June 2007) or `scream.sc` | `d697dcb6491e30ce54c38e2f42fdd2c8`, `5f4ee3922cd75d658c6818b0bdc917fb` |
| `[script 2006: name]` | the same in the 2006 `scratches.sc` / `scream.sc` | `6b15f443a27f4ea6ea70bf67455d4480`, `87c8a33c4739548a1f510b49fa07d6ba` |
| `[data FILE @0x…]` | bytes at that offset of a shipped file. For `data scream.exe`, a value below 0x400000 is a file offset and a value from 0x400000 up is a virtual address | |
| `[unverified: …]` | not confirmed in any of the above | |

Addresses are virtual addresses (image base 0x400000). Both executables were built with Open
Watcom, which passes arguments in registers (EAX, EDX, EBX, ECX), so a decompiler often hides
or misplaces them; read the disassembly next to the decompiled code. Script function names are
cited rather than line numbers, which depend on the decompiler.

## Registered functions

The DC exe registers 87 `sc*` functions as Lua globals [exe DC 0x406280–0x406f34; Lua 5.1
`lua_pushcclosure` 0x43d950 + `lua_setfield` 0x43dc90]. The 2006 exe registers 79 [exe 2006
0x405e10–0x406d8b; Lua 5.0 `lua_pushstring` 0x41cdf0, `lua_pushcclosure` 0x41ce90,
`lua_settable` 0x41d0d0]. The 2006 exe lacks `scFeedbackEx`, `scJournalAdd`, `scJournalClear`,
`scJournalSet`, `scJournalShow`, `scLoadSubtitle`, `scSetAmbience` and `scUnloadSubtitle`.

Each `sc*` function is a small wrapper. It reads its Lua arguments and calls an implementing
function. Each entry below gives both addresses on its **Source** line.

**Calls** are the number of call sites in each shipped script, counted in the decompiled
source [script DC] [script 2006]. Scratches calls 64 of the 87 functions (DC script) and 55
(2006 script). The 23 marked *not used by Scratches* are never called by either script.

## Index

| Function | Calls (DC / 2006 script) | DC wrapper | 2006 wrapper |
|---|---|---|---|
| [`scAbort`](#scabort) | 3 / 1 | `0x404f70` | `0x404e20` |
| [`scAttach`](#scattach) | *not used by Scratches* | `0x404f30` | `0x404de0` |
| [`scAudio`](#scaudio) | 301 / 299 | `0x404f80` | `0x404e30` |
| [`scBegin`](#scbegin--scend) | 647 / 557 | `0x404fc0` | `0x404e70` |
| [`scBind`](#scbind) | 549 / 512 | `0x404ff0` | `0x404ea0` |
| [`scChoiceAdd`](#scchoiceadd) | 24 / 20 | `0x405e70` | `0x405a40` |
| [`scChoiceClear`](#scchoiceclear) | *not used by Scratches* | `0x405ec0` | `0x405a90` |
| [`scChoiceGo`](#scchoicego) | 13 / 11 | `0x405e50` | `0x405a20` |
| [`scChoiceRemove`](#scchoiceremove) | *not used by Scratches* | `0x405eb0` | `0x405a80` |
| [`scCreate`](#sccreate) | 3104 / 2733 | `0x405030` | `0x404ee0` |
| [`scCursor`](#sccursor) | 11 / 11 | `0x405070` | `0x404f20` |
| [`scCutscene`](#sccutscene) | 145 / 114 | `0x4050b0` | `0x404f60` |
| [`scDisable`](#scenable--scdisable) | 229 / 218 | `0x4050d0` | `0x404f80` |
| [`scEnable`](#scenable--scdisable) | 102 / 99 | `0x405100` | `0x404fb0` |
| [`scEnd`](#scbegin--scend) | 647 / 557 | `0x405130` | `0x404fe0` |
| [`scFeedback`](#scfeedback) | 414 / 382 | `0x405160` | `0x405010` |
| [`scFeedbackEx`](#scfeedbackex) | 34 / 0 | `0x405180` | not registered |
| [`scFont`](#scfont) | 3 / 1 | `0x4051b0` | `0x405030` |
| [`scFootstep`](#scfootstep) | 201 / 142 | `0x4051d0` | `0x405050` |
| [`scFXPlay`](#scfxplay) | 369 / 313 | `0x4051f0` | `0x405070` |
| [`scFXPlayEx`](#scfxplayex) | 275 / 249 | `0x405210` | `0x405090` |
| [`scFXSet`](#scfxset) | 134 / 117 | `0x405290` | `0x405100` |
| [`scFXStop`](#scfxstop) | 44 / 40 | `0x4052f0` | `0x405160` |
| [`scGet`](#scget) | 43 / 42 | `0x405330` | `0x4051a0` |
| [`scGo`](#scwait--scgo) | 1 / 0 | `0x405320` | `0x405190` |
| [`scHints`](#schints) | 4 / 4 | `0x4053b0` | `0x4052a0` |
| [`scHintsSet`](#schintsset) | 4 / 4 | `0x4053e0` | `0x4052d0` |
| [`scInit`](#scinit) | 1 / 1 | `0x405420` | `0x405310` |
| [`scInventoryAdd`](#scinventoryadd) | 64 / 55 | `0x4054e0` | `0x405360` |
| [`scInventoryReaction`](#scinventoryreaction) | 78 / 69 | `0x4055d0` | `0x405380` |
| [`scInventoryRemove`](#scinventoryremove) | 33 / 30 | `0x4054f0` | `0x405370` |
| [`scInventorySet`](#scinventoryset) | 69 / 61 | `0x405430` | `0x405320` |
| [`scJournalAdd`](#scjournaladd) | 80 / 0 | `0x405580` | not registered |
| [`scJournalClear`](#scjournalclear) | 1 / 0 | `0x405500` | not registered |
| [`scJournalSet`](#scjournalset) | 1 / 0 | `0x405510` | not registered |
| [`scJournalShow`](#scjournalshow) | *not used by Scratches* | `0x4055c0` | not registered |
| [`scLoadSettings`](#scloadsettings) | *not used by Scratches* | `0x4056a0` | `0x4053d0` |
| [`scLoadSubtitle`](#scloadsubtitle) | 1 / 0 | `0x4061a0` | not registered |
| [`scLock`](#sclock--scunlock) | 51 / 45 | `0x4056c0` | `0x4053f0` |
| [`scLookAt`](#sclookat) | 141 / 108 | `0x4056e0` | `0x405410` |
| [`scMenuSet`](#scmenuset) | 1 / 1 | `0x405730` | `0x405460` |
| [`scMenuSetEx`](#scmenusetex) | 4 / 4 | `0x4057e0` | `0x4054f0` |
| [`scMenuSetSaveFunc`](#scmenusetsavefunc) | 1 / 1 | `0x405820` | `0x405530` |
| [`scMenuShow`](#scmenushow) | *not used by Scratches* | `0x405720` | `0x405450` |
| [`scMotion`](#scmotion--scstill) | *not used by Scratches* | `0x4056d0` | `0x405400` |
| [`scMusicFadeIn`](#scmusic) | *not used by Scratches* | `0x405840` | `0x405550` |
| [`scMusicFadeOut`](#scmusic) | *not used by Scratches* | `0x405830` | `0x405540` |
| [`scMusicPlay`](#scmusic) | *not used by Scratches* | `0x405850` | `0x405560` |
| [`scMusicStop`](#scmusic) | *not used by Scratches* | `0x405860` | `0x405570` |
| [`scOnClick`](#sconclick) | 2297 / 2032 | `0x405c30` | `0x405850` |
| [`scOnEnter`](#sconenter) | *not used by Scratches* | `0x405d20` | `0x405900` |
| [`scOnExit`](#sconexit) | *not used by Scratches* | `0x405d40` | `0x405920` |
| [`scOnHover`](#sconhover) | 2298 / 2033 | `0x405d60` | `0x405940` |
| [`scOverlay`](#scoverlay) | *not used by Scratches* | `0x405d90` | `0x405970` |
| [`scPlace`](#scplace) | 2298 / 2033 | `0x405940` | `0x4055d0` |
| [`scPlaylistClear`](#scplaylistclear) | *not used by Scratches* | `0x4059d0` | `0x405660` |
| [`scPlaylistFirst`](#scplaylistnext--previous--first--last) | *not used by Scratches* | `0x405a40` | `0x4056d0` |
| [`scPlaylistLast`](#scplaylistnext--previous--first--last) | *not used by Scratches* | `0x405a50` | `0x4056e0` |
| [`scPlaylistNext`](#scplaylistnext--previous--first--last) | *not used by Scratches* | `0x4059f0` | `0x405680` |
| [`scPlaylistPrevious`](#scplaylistnext--previous--first--last) | *not used by Scratches* | `0x4059e0` | `0x405670` |
| [`scPlaylistQueue`](#scplaylistqueue) | *not used by Scratches* | `0x405a60` | `0x4056f0` |
| [`scPlaylistStart`](#scplayliststart) | *not used by Scratches* | `0x405a00` | `0x405690` |
| [`scPlaylistStop`](#scplayliststop) | *not used by Scratches* | `0x405a10` | `0x4056a0` |
| [`scReaction`](#screaction) | 188 / 180 | `0x405870` | `0x405580` |
| [`scRun`](#scrun) | 1 / 1 | `0x405de0` | `0x4059b0` |
| [`scScriptAdd`](#scscriptadd) | 395 / 395 | `0x405e00` | `0x4059d0` |
| [`scScriptGo`](#scscriptgo) | 59 / 53 | `0x405ed0` | `0x405aa0` |
| [`scSet`](#scset) | 7 / 4 | `0x405ef0` | `0x405ac0` |
| [`scSetAmbience`](#scsetambience) | 17 / 0 | `0x405a80` | not registered |
| [`scSetLastVisited`](#scsetlastvisited) | 36 / 30 | `0x405ff0` | `0x405bc0` |
| [`scSleep`](#scsleep) | 98 / 87 | `0x405f80` | `0x405b50` |
| [`scStill`](#scmotion--scstill) | *not used by Scratches* | `0x405df0` | `0x4059c0` |
| [`scSwitch`](#scswitch) | 409 / 339 | `0x405fb0` | `0x405b80` |
| [`scTerminate`](#scterminate) | 1 / 1 | `0x406010` | `0x405be0` |
| [`scTexture`](#sctexture) | 1659 / 1282 | `0x406020` | `0x405bf0` |
| [`scTextureUpdate`](#sctextureupdate) | 466 / 455 | `0x406060` | `0x405c30` |
| [`scTimerCreate`](#sctimercreate) | 3 / 3 | `0x4060c0` | `0x405c90` |
| [`scTimerCreateGlobal`](#sctimercreateglobal) | 27 / 26 | `0x406120` | `0x405cf0` |
| [`scTrackFadeOut`](#sctrackfadeout) | 8 / 7 | `0x405b10` | `0x405740` |
| [`scTrackPlay`](#sctrackplay) | 35 / 29 | `0x405ae0` | `0x405710` |
| [`scTrackSet`](#sctrackset) | 10 / 5 | `0x405b40` | `0x405770` |
| [`scTrackSet3D`](#sctrackset3d) | 28 / 27 | `0x405b80` | `0x4057b0` |
| [`scTrackSetEx`](#sctracksetex) | 108 / 90 | `0x405bf0` | `0x405810` |
| [`scTrackStop`](#sctrackstop) | 11 / 9 | `0x405b00` | `0x405730` |
| [`scUnloadSubtitle`](#scunloadsubtitle) | 1 / 0 | `0x4061c0` | not registered |
| [`scUnlock`](#sclock--scunlock) | 45 / 43 | `0x406190` | `0x405d60` |
| [`scWait`](#scwait--scgo) | 1 / 0 | `0x406180` | `0x405d50` |

---

## Conventions

- **Arguments are not checked.** The wrappers read numbers with `lua_tonumber` and strings
  with `lua_tostring`, and never raise a Lua error [exe DC 0x43d5a0, 0x43d650].
  - A `nil` or other non-number reads as 0 [exe DC 0x43d5d7].
  - A `nil` string reads as a NULL pointer, not as `""` [exe DC 0x43d687].
  - Only `scReaction`'s optional fourth argument is tested for NULL and replaced with `""`
    [exe DC 0x405881; data scream.exe @0x7fc0c].
  - The shipped scripts pass `nil` once: the misspelt `SC_HITNS` in `scGet(SC_HITNS)`
    [script DC: dropNotice] [script 2006: dropNotice].
  - `SC_STEREO_PATCH` is a script global, defined as 1 at the top level of both scripts
    [script DC: top level] [script 2006: top level].
- **Bad names are fatal.** Looking up a name that doesn't exist logs
  `ERROR | RetrieveID : ID not found for '%s'!`. The engine then dumps its state, shows a
  message box and exits [exe DC 0x403fd0, 0x402340]. Using an empty binding is fatal in the
  same way (`ERROR | GetBind : target not bound`) [exe DC 0x4036c0].
- **Constants** (`SC_*`) are Lua globals set by the shipped `scream.sc` [script DC: scream.sc
  top level]. The 2006 `scream.sc` lacks the following [script 2006: scream.sc top level]:
  - `SC_LANGUAGE`, `SC_IMG_SIZE`, `SC_DEBUG`, `SC_FPS_STYLE`;
  - the three `SC_*_COLOR` keys and the four ambience presets;
  - `SC_STBOOK` and `SC_FEEDBACK_EX`.
- **Handlers** are Lua global functions named by a string (`"exitHouse_dc"`). The engine
  calls a handler by name in protected mode and waits for it to return [exe DC 0x404d80]. A
  runtime error is written to the log ("Runtime error", "Error while running handler"), and
  the game carries on [exe DC 0x404d80].
- **Blocking calls** run their own frame loop inside the call, and the script continues when
  the call returns:
  - `scSleep` [exe DC 0x4124f0];
  - `scLookAt(…, SC_SMOOTH)` [exe DC 0x41280c];
  - `scCutscene` [exe DC 0x412449];
  - `scScriptGo` [exe DC 0x415773];
  - `scFXPlayEx` with `wait` [exe DC 0x41615b].

  Timers and subtitles keep running during all of these [exe DC 0x412010 called at 0x412449,
  0x4124f0, 0x41280c, 0x415773, 0x41615b].
- **Bindings.**
  - Each kind (room, node, zoom, object, overlay, item, script, choice, track) has its own
    binding slot [exe DC 0x402ce0, 0x4036c0].
  - `scCreate` and `scBind` set a slot [exe DC 0x402f50, 0x4148a0]. `scSwitch` binds its
    target [exe DC 0x4158ee].
  - The engine also changes the object binding itself. In node and zoom view, the object
    under the pointer is bound every frame, so a click's handler starts with the clicked
    object bound [exe DC 0x41433c–0x414343, 0x412047]. The zoom hit test binds each object it
    tries, so after a miss the zoom's last object is bound [exe DC 0x417cfa–0x417d05].
  - Binding one kind leaves the other slots alone [exe DC 0x402d06].
  - Separately from the bindings, the engine tracks a *current* room and a current node per
    room. `scCreate(SC_ROOM)`, `scSwitch` and loading a game set these [exe DC 0x402fdf,
    0x403950].

### Object kinds

`SC_ROOM` 2000, `SC_NODE` 2001, `SC_ZOOM` 2002, `SC_OBJECT` 2003, `SC_OVERLAY` 2004,
`SC_ITEM` 2005, `SC_SCRIPT` 2006 (a dialogue), `SC_CHOICE` 2007, `SC_TRACK` 2008
[script DC: scream.sc top level].

### Actions

`scOnClick` takes an action `(kind, arg[, arg2])`, and a click performs it [exe DC 0x41468e]:

| Kind | Effect |
|---|---|
| `SC_NODE, name` | `scSwitch(SC_NODE, name)`, which plays the node's footstep [exe DC 0x4146e4, 0x415c01] |
| `SC_ROOM, name` | `scSwitch(SC_ROOM, name)` [exe DC 0x4146e4] |
| `SC_ZOOM, name` | `scSwitch(SC_ZOOM, name)` [exe DC 0x4146e4] |
| `SC_OVERLAY, name` | `scSwitch(SC_OVERLAY, name)` [exe DC 0x4146e4] |
| `SC_ZOOMOUT` | `scSwitch(SC_ZOOMOUT)` [exe DC 0x414711] |
| `SC_ZOOMBACK` | `scSwitch(SC_ZOOMBACK)` [exe DC 0x414711] |
| `SC_FEEDBACK, text` | shows the text [exe DC 0x4146b9] |
| `SC_FEEDBACK_EX, text, voice` | shows the text and plays the voice on channel 13 (`SC_DIALOGUE`) [exe DC 0x414734, 0x414744] |
| `SC_CUSTOM, handler` | calls the Lua handler [exe DC 0x414715] |

`scReaction` stores any kind. When an item is used on an object, only `SC_FEEDBACK`,
`SC_FEEDBACK_EX` and `SC_CUSTOM` reactions do anything [exe DC 0x414412, 0x4145fb].

---

## Engine and settings

### scInit

`scInit()` · Calls 1 / 1

Source: DC wrapper 0x405420 → 0x4102e0. 2006 wrapper 0x405310.

Starts the engine:

- It opens the window with the width, height, depth and fullscreen settings, and starts
  sound [exe DC 0x41061f–0x410637, 0x4104cd].
- It loads the built-in logo, font, cursors, wait cursor and rain textures from `scream.res`
  [exe DC 0x410364–0x41040e].
- It creates a built-in zoom named `black`, with the image name `black.jpg` [exe DC 0x410418].
- It opens the game archive given by `SC_RES_PATH` / `SC_RES_FILE`. If that fails, it shows
  "Please insert the game CD" and exits [exe DC 0x410445–0x410462].
- If `SC_SPLASH` names a file, that image is shown instead of the built-in logo while loading
  [exe DC 0x410473, 0x410641].
- All three ambience presets start as `SC_MORNING` (311) [exe DC 0x4105b7].

The shipped scripts call things in this order [script DC: top level] [script 2006: top level]:

- before `scInit`: `scSet` for the resource path (DC only), the resource file and the splash;
- after it: cursors, fonts, menu and hints, then every `create*()` builder;
- at the end: `scRun()`, then `scTerminate()`.

### scRun

`scRun()` · Calls 1 / 1

Source: DC wrapper 0x405de0 → 0x410660. 2006 wrapper 0x4059b0.

- Centres the mouse [exe DC 0x410661].
- If `SC_SHOWMENU` is set, it shows the main menu. Otherwise it goes straight to node view
  [exe DC 0x410689]. `scMenuSet` sets `SC_SHOWMENU` [exe DC 0x41349d].
- Logs "Entering main loop..." and runs the main loop. It returns when the loop ends
  [exe DC 0x4106a6, 0x4106c7].

### scTerminate

`scTerminate()` · Calls 1 / 1

Source: DC wrapper 0x406010 → 0x410740. 2006 wrapper 0x405be0.

Shuts the game down: the screen fades to the `black` zoom with input locked and no wait
cursor, holds for one second, the playlist stops, and the process exits. It logs
"Terminating..." [exe DC 0x410741–0x4107c6].

### scAbort

`scAbort()` · Calls 3 / 1

Source: DC wrapper 0x404f70 → 0x4106d0. 2006 wrapper 0x404e20.

Ends the current game and returns to the main menu:

- The game counts as no longer in progress, and from then on the `SC_INTRO` and `SC_OUTRO`
  handlers no longer run [exe DC 0x4106db, 0x4106e5, 0x412f31].
- The playlist stops, as with `scPlaylistStop(0)`, and every playing sound channel from 1 to
  17 stops [exe DC 0x4106f6, 0x410700–0x41072a].
- The menu opens; opening it fades to the `black` zoom and pauses one second
  [exe DC 0x410711, 0x412eac–0x412ec2].

No wait for a key or click was found. The DC script calls it at the endings [script DC:
callers of scAbort].

### scAttach

`scAttach(n, s)` · *not used by Scratches*

Source: DC wrapper 0x404f30 → 0x414790. 2006 wrapper 0x404de0 → 0x412040.

Does nothing. It reads a number (argument 1) and a string (argument 2), then calls an empty
function [exe DC 0x404f3f–0x404f5f, 0x414790] [exe 2006 0x404de0, 0x412040].

### scSet

`scSet(key, value)` · Calls 7 / 4

Source: DC wrapper 0x405ef0 → 0x410220 (strings) / 0x40fde0 (numbers). 2006 wrapper 0x405ac0.

Sets an engine setting.

- Four keys take a string: `SC_RES_PATH`, `SC_RES_FILE`, `SC_SPLASH` and `SC_LANGUAGE`
  [exe DC 0x405f0f–0x405f4f, 0x410220]. Every other key takes a number [exe DC 0x405f51].
- For `SC_CAM_SPEED`, `SC_TXT_SPEED`, `SC_MOV_SPEED` and `SC_SKY_SPEED`, the values `SC_LO`,
  `SC_MED` and `SC_HI` stand for 25, 50 and 75. Any other value is stored as given
  [exe DC 0x40fe13–0x410105].
- `SC_QUALITY` accepts only `SC_LO`, `SC_MED` and `SC_HI`, stored as 0, 1 and 2
  [exe DC 0x40ff96].
- The size keys map LO/MED/HI to sizes. For example, `SC_IMG_SIZE` maps them to 512, 1024 and
  2048 [exe DC 0x40fefd, 0x41015b].
- `scSet` ignores `SC_SPEED`, `SC_LOG`, `SC_QUICKMODE`, `SC_FULLSCREEN` and `SC_RESOURCE`
  [exe DC 0x40fde0].

In the shipped scripts:

- `SC_RES_PATH` (DC only), `SC_RES_FILE` and `SC_SPLASH` are set before `scInit`
  [script DC: top level] [script 2006: top level].
- `SC_TXT_SPEED` is set to 15 during a phone call [script DC: callBlake].
- The DC sets `SC_MOV_SPEED` to 2 around its intro images [script DC: newGameTLV].

`SC_MOV_SPEED` defaults to 25 [exe DC 0x40f366]. It is the crossfade rate: each frame a
crossfade advances by `SC_MOV_SPEED` × 0.001, scaled for the frame rate [exe DC 0x409467,
0x409638, 0x40a865] (see the [rendering spec](rendering.md)).

### scGet

`scGet(key)` → string or number · Calls 43 / 42

Source: DC wrapper 0x405330 → 0x4125d0 (strings) / 0x412610 (numbers). 2006 wrapper 0x4051a0
→ 0x410340 / 0x410380.

| Key | Returns |
|---|---|
| `SC_ROOM` | name of the current room [exe DC 0x4038f9] |
| `SC_NODE` | name of the current room's current node [exe DC 0x40390e] |
| `SC_LANGUAGE` | the language string: `"en"` by default, or the `LANGUAGE` value from `scream.cfg` [exe DC 0x4125de, 0x40f260; data scream.exe @0x805a5] |
| `SC_TXT_SPEED`, `SC_HINTS` | the setting's number [exe DC 0x41261e, 0x41262d] |
| `SC_MOV_SPEED` | DC: the setting's number [exe DC 0x412633]. 2006: `nil` [exe 2006 0x4051c4] |
| anything else | DC: 0 [exe DC 0x412639]. 2006: no value (`nil`) [exe 2006 0x4051da] |

### scEnable / scDisable

`scEnable(key)`, `scDisable(key)` · Calls 102 / 99 and 229 / 218

Source: DC wrappers 0x405100 → 0x40fa60 and 0x4050d0 → 0x40fbf0. 2006 wrappers 0x404fb0 →
0x40db70 and 0x404f80.

With `SC_OBJECT`, these set the enabled flag of the **bound object**. Only that object
changes, even if others share its name [exe DC 0x40fae6]. In node view they then rebuild the
node's list of clickable objects. The rebuild binds each object in turn, so afterwards the
node's last object is bound [exe DC 0x40faf2–0x40fafe, 0x4168f0, 0x416928–0x41693d]. With no object bound, `GetBind` stops the engine with a
fatal error [exe DC 0x4036c0].

Any other key sets a flag to 1 or 0:

| Key | Effect |
|---|---|
| `SC_SKY` | draws the sky behind the view (see the [rendering spec](rendering.md)). On at start [exe DC 0x409480, 0x40cb00, 0x40d990, 0x40f260] |
| `SC_FOG` | draws a fog layer, only while `SC_SKY` is off [exe DC 0x40948d] |
| `SC_RAIN` | draws a rain layer while `SC_SKY` and `SC_FOG` are off [exe DC 0x40954c]. Unless `SC_NIGHT` or `SC_CANDLE` is on, the lighting pass also runs a timed lightning cycle [exe DC 0x409b50, 0x409b62] |
| `SC_NIGHT` | a dark blue tint over the view [exe DC 0x409b62] |
| `SC_CANDLE`, `SC_LAMP` | a timed warm flicker. The `SC_LAMP` value is used as a number that shifts the timing and strength [exe DC 0x409bc3, 0x409bf2] |
| `SC_DREAM` | makes the field of view swing between two limits. When off, the FOV resets to 45 [exe DC 0x407c68]. While it is on, right-clicking in node or zoom view doesn't open the inventory; it switches back to the previous state instead and raises the `SC_HINTS_CLOSE` event [exe DC 0x4174b5–0x417538] [exe 2006 0x41491a–0x414922] |
| `SC_WAIT` | lets `scWait` / `scGo` change the cursor and the wait flag. On at start [exe DC 0x412590, 0x4125b0, 0x40f260] |
| `SC_HINTS`, `SC_SHOW_HOTSPOTS`, `SC_SURROUND`, `SC_SHOWMENU` | set the setting of the same name [exe DC 0x40fb3e, 0x40fb17, 0x40fbc4, 0x40fbcf] |
| `SC_FULLSCREEN`, `SC_RESOURCE` | set the flag `scInit` reads, so they only matter before `scInit` [exe DC 0x40fb56, 0x40fb6c, 0x41061f, 0x410445] |
| `SC_LOG` | turns logging on or off [exe DC 0x40fb61, 0x404a45] |
| `SC_DEBUG` | sets the debug flag, which the audio start-up reads [exe DC 0x40fbe5, 0x401bd4]. The 2006 exe has no `SC_DEBUG` key [exe 2006 0x40db70] |
| `SC_GLOW` | stored, never read [exe DC 0x40fa60; no reader of 0x4df8df0 in the listing] |

### scSetAmbience

`scSetAmbience(target, preset, now)` · Calls 17 / 0

Source: DC wrapper 0x405a80 → 0x418640 → 0x40ca50. Not registered by the 2006 exe.

Sets the colour preset for `SC_SKY_COLOR`, `SC_CLOUD_COLOR` or `SC_SCREEN_COLOR`. Other
targets are ignored [exe DC 0x418640]. The scripts pass `SC_DAWN`, `SC_MORNING`, `SC_DUSK` or
`SC_STORM` [script DC: newGameTLV].

- With `now` ≠ 0, the current colour is set at once as well [exe DC 0x40ca64–0x40ca94].
- Otherwise only the target changes, and the drawing code moves the current colour toward it
  over time:
  - the sky colour, in the sky pass, which runs only while `SC_SKY` is on [exe DC 0x40cb00,
    0x409480];
  - the cloud colour [exe DC 0x40d120];
  - the screen colour [exe DC 0x40d4e0].

The sky and cloud colours move by 0.0001 per colour channel per frame, not scaled for the
frame rate [exe DC 0x40cba6–0x40cdeb, 0x40d195–0x40d2cd]. The screen tint reaches its target
in about 1000 frames, whatever the distance [exe DC 0x40d576–0x40d60b]. Details are in the
[rendering spec](rendering.md).

### scLoadSettings

`scLoadSettings(file)` · *not used by Scratches*

Source: DC wrapper 0x4056a0 → 0x40f4a0. 2006 wrapper 0x4053d0.

Runs the `scream.cfg` parser on `file` (format in [formats.md](formats.md#screamcfg)). At
start-up the engine loads its defaults and then parses `scream.cfg` itself, before it loads
the script [exe DC 0x4061d7, 0x4061e1]. The window-mode values are used when `scInit` starts
the media layer [exe DC 0x41061f].

---

## Building the world

Rooms are declared at world level. Nodes and zooms are declared inside a room, side by side.
Objects go inside a node or a zoom [exe DC 0x4147a0, 0x41562b]:

```lua
scCreate(SC_ROOM, "garden")  scBegin(SC_ROOM)
  scCreate(SC_NODE, "n1")    scBegin(SC_NODE)
    scTexture(SC_FRONT, "garden_n1f.jpg")  -- … one per face
    scFootstep("fx-foot_grass1.ogg")
    scCreate(SC_OBJECT, "to_n2")
      scPlace(400, 300, 200, 400, SC_FRONT)
      scOnHover(SC_FORWARD)
      scOnClick(SC_NODE, "n2")
  scEnd(SC_NODE)
  scCreate(SC_ZOOM, "mailbox")  scBegin(SC_ZOOM)
    scTexture(SC_ZOOM, "mailbox.jpg")
    scAudio(SC_ZOOM, "fx-mailbox_open.ogg")
    -- objects placed here belong to the zoom
  scEnd(SC_ZOOM)
scEnd(SC_ROOM)
```

`scBegin(SC_ZOOM)` is accepted only directly inside a room block [exe DC 0x4147bc]. No shipped
script begins a zoom inside a node [script DC: all create* builders] [script 2006: all
create* builders].

### scCreate

`scCreate(kind, name)` · Calls 3104 / 2733

Source: DC wrapper 0x405030 → 0x414890 → 0x402f50. 2006 wrapper 0x404ee0.

Creates an entity with that name and binds it [exe DC 0x402f50, 0x402ce0].

- Names must be shorter than 48 characters, or the engine stops with a fatal error
  [exe DC 0x402f6e, 0x402950].
- Creating a room also makes it the current room [exe DC 0x402fdf].
- Names are not checked for repeats [exe DC 0x402f50]. The scripts often give several
  objects in one view the same name [script DC: createLiving_dc] [script 2006: createChapel].

`scCreate` does not attach the new entity to anything. Attaching happens later:

- `scBegin(SC_NODE)` adds a node to the bound room [exe DC 0x41483e].
- `scPlace` adds an object to the bound zoom or node [exe DC 0x41563f, 0x415656].
- The first node added to a room becomes its starting node [exe DC 0x402da0].

### scBegin / scEnd

`scBegin(kind)`, `scEnd(kind)` · Calls 647 / 557 each

Source: DC wrappers 0x404fc0 → 0x4147a0 and 0x405130 → 0x414860. 2006 wrappers 0x404e70 and
0x404fe0.

Track one nesting level: world, room, node or zoom [exe DC 0x403a40, 0x403ac0, 0x403b50].

- `scBegin(SC_ROOM)` needs world level [exe DC 0x4147de].
- `scBegin(SC_NODE)` needs room level, and adds the bound node to the bound room
  [exe DC 0x414809–0x41483e].
- `scBegin(SC_ZOOM)` needs room level [exe DC 0x4147bc].
- `scEnd` must match the open level [exe DC 0x414860, 0x403ac0].

A misplaced call is logged ("scBegin: … can only be created …", "scEnd: invalid call") and
ignored [exe DC 0x4147cc, 0x4147ea, 0x414815; data scream.exe @0x80b4c–0x80be1]. `scPlace`
uses the level to decide where the object goes [exe DC 0x41562b].

### scBind

`scBind(kind, name)` · Calls 549 / 512

Source: DC wrapper 0x404ff0 → 0x4148a0 → 0x403fd0. 2006 wrapper 0x404ea0.

Looks up an existing entity and binds it [exe DC 0x4148a0]:

- **Rooms, zooms, overlays, items, scripts, choices and tracks** are looked up by name across
  the whole game. The first one created wins [exe DC 0x403fd0].
- **Nodes:** the first node of that name that belongs to the **bound** room [exe DC 0x403fd0].
- **Objects:** in node view, the first object of that name in the bound node. In zoom view,
  the first one in the bound zoom. In any other state nothing matches [exe DC 0x403fd0].

If nothing matches, the engine logs `RetrieveID : ID not found` and stops with a fatal error
[exe DC 0x403fd0, 0x402340].

### scTexture

`scTexture(face, file)` · Calls 1659 / 1282

Source: DC wrapper 0x406020 → 0x4149c0. 2006 wrapper 0x405bf0.

- Sets the image name for one face of the bound node: `SC_FRONT` (1) to `SC_DOWN` (6). With
  `SC_ZOOM`, it sets the bound zoom's image instead. Other values are ignored
  [exe DC 0x4149c7–0x414a00].
- Also stores a mask name: the file name cut at the first `.`, plus `_msk.tga`
  [exe DC 0x414a46–0x414a62; data scream.exe @0x80bf5].
- Images are loaded when the node or zoom is entered: six faces plus six masks, or the zoom
  image plus its mask [exe DC 0x415af7–0x415bb5, 0x415c61–0x415c80].

How the mask combines with the sky, fog and rain is described in [rendering.md](rendering.md#node-frame); a missing mask counts as white (see [formats.md](formats.md#images)).

### scTextureUpdate

`scTextureUpdate(face, file, visible)` · Calls 466 / 455

Source: DC wrapper 0x406060 → 0x414eb0. 2006 wrapper 0x405c30.

Changes an image during play, for example when a light is switched on. It stores the name as
`scTexture` does [exe DC 0x414edf]. `face` may also be `SC_ZOOM` or `SC_OVERLAY`
[exe DC 0x414f18–0x414f30].

- **`visible` = 0:** only the name changes. The new image shows the next time the node is
  entered [exe DC 0x414efb, 0x415af7].
- **`visible` ≠ 0:**
  1. The engine captures the current frame, as it does before a switch, and clears `scLock`
     [exe DC 0x414ebf–0x414ed6].
  2. It loads the image and its mask straight into the on-screen texture slot
     [exe DC 0x4150f6–0x415106, 0x415520]. This happens even if the bound node is not the
     one on screen.

The capture produces a visible crossfade from the old image to the new one (see the
[rendering spec](rendering.md)).

### scPlace

`scPlace(x, y, w, h, face)` · Calls 2298 / 2033

Source: DC wrapper 0x405940 → 0x4155f0. 2006 wrapper 0x4055d0.

- Stores the bound object's rectangle and face, and enables the object
  [exe DC 0x41560f–0x415628].
- At zoom level, 64 is added to `y` [exe DC 0x4155fb–0x415602].
- The object is then added to the bound zoom (at zoom level) or to the bound node
  [exe DC 0x41562b–0x415656].
- Hotspots are drawn for picking in a space set by `SC_IMG_SIZE`, which defaults to 1024
  [exe DC 0x40b872, 0x40f260].

- Node rectangles are not clipped to the face [exe DC 0x40b860]; a rectangle that runs past
  an edge simply extends past it (see [input.md](input.md#hotspot-picking)).
- In a zoom, the rectangle is in units of 1/1.28 window pixel, with the stored +64 on y: a
  script rectangle (x, y, w, h) covers window x 1.28·x to 1.28·(x + w) and window y
  1.28·(y + 64) to 1.28·(y + 64 + h). `face` is not used in zooms [exe DC 0x417cd8–0x417db6,
  0x417d17; constant 1.28 at 0x483e20] (see [input.md](input.md#zoom-view)).

### scOnHover

`scOnHover(cursor)` · Calls 2298 / 2033

Source: DC wrapper 0x405d60 → 0x415e10. 2006 wrapper 0x405940.

Stores the cursor shown over the bound object [exe DC 0x415e1d]. Only five values change the
cursor: `SC_NORMAL`, `SC_FORWARD`, `SC_LOOK`, `SC_USE` and `SC_ZOOMOUT`. `SC_ZOOMBACK` shows
the `SC_ZOOMOUT` cursor. Any other value shows `SC_NORMAL`, which the hover sets first on
every frame [exe DC 0x414319, 0x41436d; jump table data scream.exe VA 0x414278].

### scOnClick

`scOnClick(kind, arg[, arg2])` · Calls 2297 / 2032

Source: DC wrapper 0x405c30 → 0x415e30 / 0x415e70 / 0x415ed0 / 0x415f10. 2006 wrapper
0x405850.

- Stores the [action](#actions) a click on the bound object performs [exe DC 0x405c30].
  Kinds other than those in the table are ignored [exe DC 0x405c61].
- Clicks are ignored while input is locked or the wait flag is set [exe DC 0x4142b0].
- The clicked object is bound when the action runs (see [Bindings](#conventions)), so a bare
  `scDisable(SC_OBJECT)` in the handler hides the clicked hotspot [exe DC 0x414343].

### scReaction

`scReaction(item, kind, arg[, arg2])` · Calls 188 / 180

Source: DC wrapper 0x405870 → 0x412930. 2006 wrapper 0x405580.

Adds a reaction to the bound object: what happens when the player uses item `item` on it.

- An object holds at most 63 reactions. The 64th is fatal ("Too may reactions!", sic)
  [exe DC 0x41295c, 0x4129d5].
- The item name must exist [exe DC 0x41293c].
- Reactions are checked only when the object's own click action is `SC_FEEDBACK`,
  `SC_FEEDBACK_EX` or `SC_CUSTOM` [exe DC 0x4143a1, 0x4145a9].

When the player uses the held item on such an object, the item is dropped first
[exe DC 0x4143c0]. Then:

- A matching `SC_FEEDBACK` reaction shows its text. `SC_FEEDBACK_EX` also plays the voice on
  channel 13. `SC_CUSTOM` calls the handler [exe DC 0x41441d, 0x41464c, 0x414673].
- With no match, the item's own fail text is shown and its fail voice plays on channel 13
  [exe DC 0x414621–0x41463b].

On an object whose click is a switch, a held item is just dropped [exe DC 0x4145d6]. While an
item is held over an object with a feedback or custom click, the engine sets cursor state
0xcb: the item is drawn with two extra additive copies, the second one pixel larger on each side, so it glows [exe DC 0x414582–0x414593, 0x40b529–0x40b5c0].

### scFootstep

`scFootstep(file)` · Calls 201 / 142

Source: DC wrapper 0x4051d0 → 0x415fe0. 2006 wrapper 0x405050.

Stores a sound on the bound node [exe DC 0x415fee]. Every switch to that node plays it on
channel 8 (`SC_STEP`) [exe DC 0x415be7–0x415c01]. That includes clicks and `scSwitch` calls.

### scAudio

`scAudio(when, file)` · Calls 301 / 299

Source: DC wrapper 0x404f80 → 0x415540. 2006 wrapper 0x404e30.

Sounds for the bound zoom:

- `SC_ZOOM` stores the sound played on entering the zoom [exe DC 0x415598], on channel 14
  (`SC_FX`) [exe DC 0x415cb9]. It does not play when the player returns to the zoom by
  zooming out of a child zoom [exe DC 0x415c8f].
- `SC_ZOOMOUT` and `SC_ZOOMBACK` store the leaving sound [exe DC 0x4155c5].
  - `SC_ZOOMOUT` plays it on channel 14 [exe DC 0x415d1f].
  - `SC_ZOOMBACK` plays it on a free channel from 14 to 17 [exe DC 0x415952, 0x4160d0].
- `SC_STEREO` stores a file on the current room [exe DC 0x415556]. [unverified: whether it is
  ever read; no reader was found]

### scSetLastVisited

`scSetLastVisited(node)` · Calls 36 / 30

Source: DC wrapper 0x405ff0 → 0x415de0. 2006 wrapper 0x405bc0.

Each room remembers a node:

- It starts as the room's first node [exe DC 0x402da0].
- Every switch to a node updates it [exe DC 0x415bd2].
- `scSwitch(SC_ROOM)` enters the room at that node [exe DC 0x415a4e].

This call sets it for the bound room, looking the node up in that room [exe DC 0x415dfa].

### scOnEnter

`scOnEnter(handler)` · *not used by Scratches*

Source: DC wrapper 0x405d20 → 0x415f60. 2006 wrapper 0x405900.

Sets a handler for the bound room [exe DC 0x415f73–0x415f93]. `scSwitch(SC_ROOM)` calls it
after switching to the room's node [exe DC 0x415a70]. Loading a saved game switches to the
saved room the same way, so the handler runs then too [exe DC 0x4111d8].

### scOnExit

`scOnExit(handler)` · *not used by Scratches*

Source: DC wrapper 0x405d40 → 0x415fa0. 2006 wrapper 0x405920.

Stores an exit handler on the bound room [exe DC 0x415fd3]. Nothing ever calls it. The only
code that touches the field is room creation and this function [exe DC 0x40304d, 0x415fd3].

### scOverlay

`scOverlay(image, enterSound, leaveSound)` · *not used by Scratches*

Source: DC wrapper 0x405d90 → 0x412a00. 2006 wrapper 0x405970.

Sets the image and the two sounds of the bound overlay [exe DC 0x412a13–0x412a4f].

- `scSwitch(SC_OVERLAY, name)` loads the image, switches to overlay state and plays
  `enterSound` on a free channel from 14 to 17 [exe DC 0x4159df–0x415a0b].
- A click in overlay state returns to the previous state and plays `leaveSound`
  [exe DC 0x414501–0x41454b].

- Esc also dismisses it, with a crossfade and `leaveSound`, without touching the input lock
  [exe DC 0x417a3a–0x417a5b]. A right click dismisses it with no sound.

---

## Movement, camera and control

### scSwitch

`scSwitch(kind, name)` · Calls 409 / 339

Source: DC wrapper 0x405fb0 → 0x4158d0. 2006 wrapper 0x405b80.

Moves the player. For every kind except `SC_ZOOMOUT` and `SC_ZOOMBACK`, it first binds the
target [exe DC 0x4158ec–0x4158f7].

| Kind | Effect |
|---|---|
| `SC_ROOM` | makes the room current, switches to its remembered node, then calls its `scOnEnter` handler [exe DC 0x415a37–0x415a70] |
| `SC_NODE` | the node must be in the current room, or the engine stops with a fatal error [exe DC 0x4039ee]. Shows the wait cursor while loading, loads six faces and six masks, enters node view, remembers the node in the room, and plays its footstep on channel 8 [exe DC 0x415a9d–0x415c01] |
| `SC_ZOOM` | pushes the zoom on the current room's zoom stack, loads its image and mask, enters zoom view and plays its `SC_ZOOM` sound [exe DC 0x415c1b–0x415cb9] |
| `SC_ZOOMOUT` | pops one zoom and plays the bound zoom's leaving sound. If zooms remain, it re-enters the one now on top and binds it, without pushing it again or playing its sound. Otherwise it returns to node view. `name` is ignored. Does nothing if the stack is empty [exe DC 0x415cd5–0x415d98] |
| `SC_ZOOMBACK` | pops every zoom and returns to node view, playing the bound zoom's leaving sound [exe DC 0x41591c–0x415985] |
| `SC_OVERLAY` | shows an overlay (see `scOverlay`) [exe DC 0x4159bd] |

Each view change captures the current frame with input locked, then sets `scLock` to 0
[exe DC 0x415957–0x41596b, 0x415acb–0x415add, 0x415c3f–0x415c57]. When the last zoom is
closed, the mouse pointer is centred if alternate control (`SC_FPS_STYLE`) is on, which it
is by default [exe DC 0x415d98, 0x40f260, 0x413cf6].

### scLookAt

`scLookAt(face, transition)` · Calls 141 / 108

Source: DC wrapper 0x4056e0 → 0x412670. 2006 wrapper 0x405410 → 0x4103d0.

Turns the camera to face a cube side.

- **`SC_INSTANT`** sets the angles at once [exe DC 0x412685–0x41269c].
- **`SC_SMOOTH`:**
  1. shows the wait cursor and locks input;
  2. animates the turn frame by frame, and **blocks** until it ends;
  3. sets `scLock` to 0 and restores the cursor.

  [exe DC 0x4126d4, 0x412718, 0x41280c, 0x412899]
- Other transitions do nothing [exe DC 0x4126d2].

A smooth turn to `SC_DOWN` uses the same target as `SC_FRONT`, so it ends level, facing front.
An instant `SC_DOWN` looks down [jump tables data scream.exe VA 0x41263c, 0x412654]. While
`SC_DREAM` is on, a smooth turn also moves the field of view [exe DC 0x4127f2].

### scSleep

`scSleep(seconds)` · Calls 98 / 87

Source: DC wrapper 0x405f80 → 0x4124a0. 2006 wrapper 0x405b50 → 0x410230.

**Blocks** for `seconds`, converted to an integer, drawing frames meanwhile
[exe DC 0x405f93, 0x4124c9–0x412512].

- **Start:** it calls `scWait`, so the wait cursor shows if `SC_WAIT` is on. It also freezes
  camera motion, as `scStill` does [exe DC 0x4124b4, 0x4124b9].
- **End:** it allows motion again and calls `scGo` [exe DC 0x4124da, 0x4124e4].
- **Esc (DC only):** Esc ends the sleep early, and stops any of channels 14 to 17 that are
  playing. Other channels keep playing [exe DC 0x412516–0x412544, 0x419b80].
- **2006:** the 2006 sleep also freezes camera motion and allows it again at the end, as its
  `scStill` / `scMotion` do. It has no Esc check [exe 2006 0x410247, 0x410267, 0x4102d0;
  flag 0x4723b8].

### scLock / scUnlock

`scLock()`, `scUnlock()` · Calls 51 / 45 and 45 / 43

Source: DC wrappers 0x4056c0 → 0x412550 and 0x406190 → 0x412560. 2006 wrappers 0x4053f0 →
0x4102b0 and 0x405d60 → 0x4102c0.

Set the input lock to 1 or 0. It is a plain flag, not a counter [exe DC 0x412550, 0x412560].
Clicks are ignored while it is set [exe DC 0x4142d5].

The lock is also set to 0 at the end of [exe DC 0x41596b, 0x412899, 0x414ed0, 0x41452e,
0x412e96]:

- every `scSwitch` view change;
- a smooth `scLookAt`;
- `scTextureUpdate` with `visible`;
- an overlay dismissal;
- opening the menu.

Some scripts rely on this: they call `scLock()`, end with `scSwitch(SC_ZOOMOUT, "")`, and
never call `scUnlock` [script DC: askSafe, callJerry, callBarbara, askInhabitants]. The camera
keys and opening or closing the inventory clear it too [exe DC 0x41760c–0x417634,
0x4174bf–0x417538, 0x417955–0x4179b4]. So do opening and closing the journal and the menu,
because every screen capture sets the lock and clears it again (see
[mainloop.md](mainloop.md#the-capture-step)) [exe DC 0x4185a9–0x4185c4, 0x412e8b–0x412e9c,
0x4130fb–0x413112].

### scWait / scGo

`scWait()`, `scGo()` · Calls 1 / 0 each

Source: DC wrappers 0x406180 → 0x412590 and 0x405320 → 0x4125b0. 2006 wrappers 0x405d50 →
0x4102f0 and 0x405190 → 0x410320.

These work only while `SC_WAIT` is on [exe DC 0x412590, 0x4125b0]:

- `scWait` shows the wait cursor and sets a wait flag.
- `scGo` clears the flag and restores the normal cursor.

While the flag is set, hovering and clicking in node, zoom and overlay view are ignored. The
inventory, menu, choices, the book and the right click still work [exe DC 0x4142c4–0x4142d3,
0x417280–0x417598] (see [input.md](input.md#the-mouse-handler)).

### scMotion / scStill

`scMotion()`, `scStill()` · *not used by Scratches*

Source: DC wrappers 0x4056d0 → 0x412580 and 0x405df0 → 0x412570. 2006 wrappers 0x405400 →
0x4102e0 and 0x4059c0 → 0x4102d0.

Set or clear the camera-motion flag [exe DC 0x412580, 0x412570]. The frame update reads it to
choose between the moving-camera path and the still path [exe DC 0x416a62]. `scSleep` clears
it itself and always sets it again at the end [exe DC 0x4124b9, 0x4124da].

---

## Presentation

### scCursor

`scCursor(kind, image)` · Calls 11 / 11

Source: DC wrapper 0x405070 → 0x4148c0. 2006 wrapper 0x404f20.

Loads the image for one cursor [exe DC 0x4148c0–0x414967]. Accepted kinds:

- `SC_NORMAL`, `SC_FORWARD`, `SC_LOOK`, `SC_USE`, `SC_WAIT`;
- `SC_ZOOMOUT` (`SC_ZOOMBACK` sets the same cursor);
- `SC_MENU`;
- the edge arrows `SC_LEFT`, `SC_RIGHT`, `SC_UP`, `SC_DOWN`.

Other kinds are ignored. The engine starts with default cursors from `scream.res`
[exe DC 0x4103aa–0x4103fa].

### scFont

`scFont(atlas)` · Calls 3 / 1

Source: DC wrapper 0x4051b0 → 0x414980. 2006 wrapper 0x405030.

Stores the name and loads the image into the font texture slot [exe DC 0x414985, 0x4149ab].
The DC switches fonts for the menu, the main game and *The Last Visit* [script DC: top level,
newGameTLV]. The atlas layout is in [formats.md](formats.md#fonts).

### scCutscene

`scCutscene(video)` · Calls 145 / 114

Source: DC wrapper 0x4050b0 → 0x412420. 2006 wrapper 0x404f60.

Opens the video, enters video state, and **blocks** while that state lasts
[exe DC 0x412420–0x412455].

- Esc stops it: the engine draws a frame and returns to the previous state. Esc also stops
  channel 14 (`SC_FX`) [exe DC 0x41244e, 0x412464–0x41248c].
- The scripts start the matching sound with `scFXPlay` just before the call [script DC:
  newGameTLV].

A click or Space does not skip: blocking calls read and discard every other message
[exe DC 0x419b80].

---

## Text

### scFeedback

`scFeedback(text)` · Calls 414 / 382

Source: DC wrapper 0x405160 → 0x40e150 (text setter), drawn by 0x40aca0. 2006 wrapper
0x405010 → 0x40c500.

**Length limit.** Text of 400 or more characters is ignored, and the old text stays
[exe DC 0x40e16e]. The 2006 limit is 256 [exe 2006 0x40c51e].

**Line splitting.** The text is split into one to four lines by its length
[exe DC 0x40e1ae–0x40e367]:

| Length | Lines |
|---|---|
| up to 100 | 1 |
| 101–200 | 2 |
| 201–300 | 3 |
| over 300 | 4 |

- A line breaks at the first space after it reaches its share of the text (100 characters, or
  the length divided by the line count) [exe DC 0x40e150].
- The 2006 thresholds are 70, 140 and 210 [exe 2006 0x40c561–0x40c703].
- `|` becomes a space after line breaking, so it acts as a non-breaking space
  [exe DC 0x40e36c] [exe 2006 0x40c73b].

**Duration.** The text lasts `100 / SC_TXT_SPEED + length / 16` seconds, in integer
arithmetic. Texts of 16 characters or fewer count as 32 [exe DC 0x40e26d–0x40e2f0]
[exe 2006 0x40c500].

**Drawing.**
- Each line is centred horizontally and drawn near the bottom of the screen. The bottom line
  is the last line of the text [exe DC 0x40aca0].
- The text fades in and holds until its time is up. It then fades out more slowly and
  disappears once fully faded [exe DC 0x40aca0].
- A four-line text shows only three lines at a time: its first three for the first half of
  its time, then its last three [exe DC 0x40aca0, case 4; constant 0.5 at 0x48343c].
- `scFeedback("")` replaces the current text with an empty one, so the old text disappears
  [exe DC 0x40e150].

### scFeedbackEx

`scFeedbackEx(text, voice)` · Calls 34 / 0

Source: DC wrapper 0x405180 → 0x415670, which calls 0x40e150 and then 0x416120. Not
registered by the 2006 exe.

- Shows `text` exactly as `scFeedback` does [exe DC 0x415672].
- Plays `voice` on channel 13 (`SC_DIALOGUE`), first stopping whatever that channel was
  playing [exe DC 0x415677, 0x416120].
- Does not wait for the voice to finish [exe DC 0x41567e, wait flag 0].

### scLoadSubtitle

`scLoadSubtitle(file)` · Calls 1 / 0

Source: DC wrapper 0x4061a0 → 0x418080. Cues are advanced by 0x4180b0, which the per-frame
routine 0x412010 calls. Not registered by the 2006 exe.

Opens `file` and turns subtitles on [exe DC 0x418080; mode string "r" at 0x4835a8]. The file
format and the player's timing quirks are in [formats.md](formats.md#srt-subtitles). In
brief:

- Subtitles use a seconds counter. It goes up by one about once a second, but only while
  subtitles are on [exe DC 0x41811c–0x418166].
- `scLoadSubtitle` does not reset the counter. The counter starts at zero when the program
  starts, so with the single load in Scratches, its time counts from about the moment of
  loading [exe DC 0x418080; no other writer of 0x48bc58].
- A cue's text replaces the feedback text on the first tick at or after its start time
  [exe DC 0x41812e–0x418158]. When the counter reaches the cue's end time, the text is cleared
  and the next cue is read [exe DC 0x41817e–0x418326].
- Reading a cue numbered 13 turns subtitles off [exe DC 0x4181ae].
- The per-frame routine also runs inside `scCutscene`'s loop, so subtitles keep running
  during cutscenes [exe DC 0x412449 in 0x412420]. The DC script loads the file just before
  the intro cutscene [script DC: newGameTLV].

### scUnloadSubtitle

`scUnloadSubtitle()` · Calls 1 / 0

Source: DC wrapper 0x4061c0 → 0x418470. Not registered by the 2006 exe.

Turns subtitles off and closes the file [exe DC 0x418470]. It does not clear cue text already
on screen [exe DC 0x418470].

---

## Dialogue and choices

### scScriptAdd

`scScriptAdd(voice, text, hint)` · Calls 395 / 395

Source: DC wrapper 0x405e00 → 0x415690 (thunk) → 0x403d50. 2006 wrapper 0x4059d0.

Appends a line (voice file, text, hint flag) to the bound script (`SC_SCRIPT`)
[exe DC 0x403d5e, 0x403d96–0x403de5].

- If the text is 400 characters or longer, it logs "ScriptAdd : message for %s is too long"
  [exe DC 0x403d76, 0x403df0].
- If the script already has 21 lines, it logs "ScriptAdd : too many lines"
  [exe DC 0x403d81, 0x403e0a].
- In both cases it also writes the engine's error dump, then adds the line anyway
  [exe DC 0x403e00, 0x403e05, 0x402340].

Lines with `hint` set play only while the `SC_HINTS` setting is on
[exe DC 0x415728–0x415738; SC_HINTS read from 0x4df8d38 as in 0x412610].

### scScriptGo

`scScriptGo(name)` · Calls 59 / 53

Source: DC wrapper 0x405ed0 → 0x4156a0. 2006 wrapper 0x405aa0.

Plays the named script (and binds it) and **blocks** until it ends [exe DC 0x4156a0,
0x4156ae–0x4156bf].

- The lines play one after another. Each line's voice plays on channel 13 (`SC_DIALOGUE`)
  while its text shows as feedback (an empty text shows nothing). The next line starts when
  the voice ends [exe DC 0x41573a, 0x41574e, 0x4157ef, 0x415763–0x41577f].
- While the script plays, sound channels 1 to 7 are 0.25 quieter; afterwards they go back up
  by 0.25 [exe DC 0x4156e6–0x41570a, 0x4157a1–0x4157c8; constants −0.25 at 0x483c00, +0.25 at
  0x483c04].
- The feedback text is cleared at the end [exe DC 0x4157dd].

Esc stops the current line's voice, and the next line starts [exe DC 0x415778; 0x419b80
checks for a WM_KEYDOWN of VK_ESCAPE].

### scChoiceAdd

`scChoiceAdd(text, handler)` · Calls 24 / 20

Source: DC wrapper 0x405e70 → 0x415800 → 0x403e30. 2006 wrapper 0x405a40.

- Adds an option (text and handler name) to the bound choice (`SC_CHOICE`)
  [exe DC 0x403e38, 0x403e51–0x403ead].
- A choice holds at most 16 options. Further adds are silently ignored [exe DC 0x403e47].
- The handler is later called by name [exe DC 0x405e92, 0x415819].

### scChoiceGo

`scChoiceGo(name)` · Calls 13 / 11

Source: DC wrapper 0x405e50 → 0x415850. Picking is handled by 0x413e70. 2006 wrapper 0x405a20.

1. Finds the choice by name and binds it. Does nothing if the choice has no options
   [exe DC 0x415855–0x41587b].
2. Clears the feedback text, and puts count + 1 rows in the on-screen list: the options,
   plus an empty row below them [exe DC 0x415881–0x4158b5]. The list is drawn at the bottom
   left (see the [screens spec](screens.md)).
3. Switches to the choice state (`SC_STCHOICE` 4007) [exe DC 0x4158b7].

It does **not** block: the call returns at once [exe DC 0x415850].

- Picking an option leaves the choice state (state 4006) and records the option's index. It
  then calls the option's handler by name [exe DC 0x413f29–0x413f63].
- In the choice state, Esc only drops an item held in hand, as it does in every state [exe DC 0x417934, 0x417a07 in 0x4175b0].

### scChoiceClear

`scChoiceClear()` · *not used by Scratches*

Source: DC wrapper 0x405ec0 → 0x415840 (thunk) → 0x403f90. 2006 wrapper 0x405a90.

Sets the bound choice's option count to zero [exe DC 0x403f90].

### scChoiceRemove

`scChoiceRemove()` · *not used by Scratches*

Source: DC wrapper 0x405eb0 → 0x415830 (thunk) → 0x403ec0. 2006 wrapper 0x405a80.

Removes from the bound choice the option whose index was recorded at the last pick. The later
options move up [exe DC 0x403ec9–0x403f70; index written at 0x413f4c]. The count goes down by
one even when that index is past the end [exe DC 0x403f75].

---

## Inventory and hints

### scInventorySet

`scInventorySet(icon, label, failText[, failVoice])` · Calls 69 / 61

Source: DC wrapper 0x405430 → 0x412a80 → 0x403b60 and 0x407880. 2006 wrapper 0x405320.

- Stores the four strings on the bound item (`SC_ITEM`). A missing `failVoice` is stored as
  `""` [exe DC 0x405446, 0x405476 (empty string at 0x482c0c), 0x403b60].
- Loads the icon as the item's texture [exe DC 0x412a9f].
- If no item is bound, logs "SetItem : target not bound" [exe DC 0x403c05].

`failText` and `failVoice` are used when the player uses the held item on something with no
reaction for it. The held item's `failText` is shown, and its `failVoice` plays on channel
13. This applies both in the inventory [exe DC 0x4141d6–0x4141fe in 0x414030] and in the
scene [exe DC 0x414621–0x41463b in 0x4142b0].

### scInventoryAdd

`scInventoryAdd()` · Calls 64 / 55

Source: DC wrapper 0x4054e0 → 0x412ab0 → 0x403c70. 2006 wrapper 0x405360.

Appends the bound item to the end of the inventory and puts its icon in the new slot
[exe DC 0x403c7b–0x403c86, 0x412ad5]. There is no duplicate check, so adding an item twice
lists it twice [exe DC 0x403c70].

### scInventoryRemove

`scInventoryRemove()` · Calls 33 / 30

Source: DC wrapper 0x4054f0 → 0x412ae0 → 0x403cc0. 2006 wrapper 0x405370.

Finds the bound item's position in the inventory, then moves the later items and their icons
up one place [exe DC 0x412ae3–0x412b17, 0x403cc0]. If the item is not in the inventory, the
position lookup returns 0, and the *first* item is dropped instead [exe DC 0x4037d0 case
0xbbc; 0x403cc0].

[unverified: what happens to the item if the player is holding it; nothing in 0x412ae0 or
0x403cc0 touches a held item]

### scInventoryReaction

`scInventoryReaction(item, kind, arg[, arg2])` · Calls 78 / 69

Source: DC wrapper 0x4055d0 → 0x412b20. 2006 wrapper 0x405380.

Adds an [action](#actions) to **both** items, so the reaction works in either direction
[exe DC 0x412b2e–0x412bcc, 0x412bd7–0x412c6a]:

- to the bound item, for when `item` is used on it;
- to `item`, for when the bound item is used on it.

Further details:

- The bound item is re-bound afterwards [exe DC 0x412c79].
- A missing `arg2` is stored as `""`. `SC_CUSTOM` handlers are called by name
  [exe DC 0x4055ea, 0x405622].
- The inventory click routine checks these reactions [exe DC 0x4141b4–0x4141ce in 0x414030].
- If `item` already holds 63 reactions, the next add shows "Too many reactions!" and the
  engine quits [exe DC 0x412bf7, 0x412c87–0x412ca2]. The bound item's own count is not
  checked [exe DC 0x412b4f].

### scHintsSet

`scHintsSet(key, value)` · Calls 4 / 4

Source: DC wrapper 0x4053e0 → 0x417010. 2006 wrapper 0x4052d0.

Configures the inventory tutorial [exe DC 0x417010]:

- `SC_HINTS_OPEN` (2), `SC_HINTS_GRAB` (3) and `SC_HINTS_CLOSE` (4) store the texts for the
  tutorial's open, grab and close events.
- `SC_HINTS_ITEM` (5) stores the name of the tutorial item.
- Other keys are ignored.

The DC script sets the item to `"house-key"` [script DC: top level, after createInventory].

### scHints

`scHints(state)` · Calls 4 / 4

Source: DC wrapper 0x4053b0 → 0x416ff0 → 0x4170e0. 2006 wrapper 0x4052a0.

- `SC_HINTS_INACTIVE` (0) turns the tutorial off. `SC_HINTS_ACTIVE` (1) turns it on
  [exe DC 0x4170e4–0x417126].
- Every other value acts only while the tutorial is on *and* the `SC_HINTS` setting is on
  [exe DC 0x4170ed–0x4170fd]:
  - `SC_HINTS_OPEN` shows the open text, once [exe DC 0x41710e–0x41714c];
  - `SC_HINTS_GRAB` and `SC_HINTS_CLOSE` show their text, once, and only when the current
    item is the `SC_HINTS_ITEM` [exe DC 0x417157–0x417224];
  - `SC_HINTS_RESET` (6) re-arms the three one-time texts [exe DC 0x417225];
  - other values do nothing.
- An empty hint text is not shown [exe DC 0x417181–0x417192].
- The three texts start armed [data scream.exe @0x8bc48–0x8bc50 = 1].

The engine raises the events itself too:

- the grab event, when an item is picked up in the inventory [exe DC 0x414133];
- the close event, when the inventory is closed with a right click [exe DC 0x417533];
- the open event, on the right click that opens the inventory [exe DC 0x4174bf–0x4174d3,
  0x4172f4].

The DC script turns the tutorial on in [script DC: goUp], off in [script DC: goDown], and
resets it in [script DC: newGameSDC].

---

## Journal (Director's Cut)

None of these four functions is registered by the 2006 exe.

### scJournalSet

`scJournalSet(image, font, openSound, turnSound, closeSound)` · Calls 1 / 0

Source: DC wrapper 0x405510 → 0x4184a0.

Stores the five file names in engine globals. It does not touch any bound entity
[exe DC 0x4184a0].

**Opening.** The journal is tied to an item by the item's name. Clicking the inventory item
named `"journal"` with nothing in hand opens the book [exe DC 0x4140cf–0x4140df in 0x414030;
string "journal" at 0x483b36]. The DC script creates that item just before it calls
`scJournalSet` [script DC: createInventory]. On opening, the book shows `image`, its text uses
`font`, and `openSound` plays [exe DC 0x418580].

**Using the book.**
- Clicking the left or right half of the book turns the page and plays `turnSound`; a
  click lower than y = 604 closes the book [exe DC 0x413f83, 0x413f99–0x413fdc, 0x413fec in
  0x413f80].
- Esc closes the book [exe DC 0x4175b0, key 0x1b in state 4008 → 0x4185f0].
- On closing, `closeSound` plays, and the game font set by `scFont` is used again
  [exe DC 0x4185f0, 0x414980].

### scJournalAdd

`scJournalAdd(index, text)` · Calls 80 / 0

Source: DC wrapper 0x405580 → 0x418550 → 0x40e380.

- Adds the entry only if `index` hasn't been used before, then marks the index as used
  [exe DC 0x418558–0x418568]. `index` is not range-checked [exe DC 0x418550].
- A blank line separates each entry from the one before [exe DC 0x40e380; empty string at
  0x483131].
- Lines break at the first space after more than 30 characters. `|` becomes a space, so it
  acts as a non-breaking space [exe DC 0x40e380].
- The journal holds at most 1000 lines [exe DC 0x40e380].
- A page turn moves 76 lines [exe DC 0x40e490].

### scJournalClear

`scJournalClear()` · Calls 1 / 0

Source: DC wrapper 0x405500 → 0x418490.

Empties the journal's text and goes back to the first page [exe DC 0x418490]. The used-index
flags are not cleared, so those entries can't be added again [exe DC 0x418490 does not write
0x4df7bb8]. The DC script calls it at the start of a new Director's Cut game [script DC:
newGameSDC].

### scJournalShow

`scJournalShow()` · *not used by Scratches*

Source: DC wrapper 0x4055c0 → 0x418580.

Opens the journal [exe DC 0x418580]:

1. clears the feedback text;
2. loads the journal image and font;
3. enters the book state (`SC_STBOOK` 4008);
4. plays the open sound.

The inventory "journal" click uses the same routine [exe DC 0x4140df].

---

## Menu

### scMenuSet

`scMenuSet(logo, back, new, setup, load, save, quit, newGame1, newGame2)` · Calls 1 / 1

Source: DC wrapper 0x405730 → 0x4131c0. 2006 wrapper 0x405460 → 0x410e00.

**Arguments.** The DC exe reads exactly nine strings: seven images, then two new-game handler
names [exe DC 0x405738–0x4057ca, 0x4131c0]. The 2006 exe reads eight: seven images and one
handler [exe 2006 0x405460]. Handlers are called by name.

**What it does.**
- Loads the seven images [exe DC 0x413442–0x413498].
- Clears the save-slot names and resets the menu's settings, including the `scMenuSetEx`
  values and the save function [exe DC 0x41330a–0x41343d].
- Marks the menu as defined, which lets Esc open it [exe DC 0x41349d; checked at 0x417a23].

**Starting a new game (DC).**
- With no game running, the DC exe always asks "Which game do you want to play?", offering
  *Scratches: Director's Cut* and *Scratches: The Last Visit* [exe DC 0x413031–0x41304e in
  0x412fa0]. Picking runs `newGame1` or `newGame2` [exe DC 0x4137c4, 0x413849 in 0x413640].
- With a game running, it first asks "Start a new game?" [exe DC 0x413079].

The 2006 exe has none of the which-game strings [data 2006-patch scream.exe: string search].

### scMenuSetEx

`scMenuSetEx(key, value)` · Calls 4 / 4

Source: DC wrapper 0x4057e0 → 0x4134b0. 2006 wrapper 0x4054f0.

| Key | Value |
|---|---|
| `SC_INTRO` | handler run when the menu opens while no game is in progress, unless the flag `scAbort` sets is set [exe DC 0x412cdf–0x412f3e in 0x412cb0; flag set at 0x4106e5] |
| `SC_OUTRO` | handler run when the player confirms "Quit the game?", unless that same flag is set [exe DC 0x413e3d–0x413e4f in 0x413640] |
| `SC_STEREO` | menu music file, played on channel 7 when the menu opens [exe DC 0x413581, 0x412ee3–0x412f06]. While the DC menu is open, the cloud colour is set to `SC_STORM` [exe DC 0x412f10–0x412f1a]; the 2006 menu doesn't change it [exe 2006 0x4109c0–0x410c3a] |
| `SC_IMAGE` | menu background image, loaded at once [exe DC 0x413519–0x41354a] |

Other keys are ignored [exe DC 0x4134b0].

### scMenuSetSaveFunc

`scMenuSetSaveFunc()` · Calls 1 / 1

Source: DC wrapper 0x405820 → 0x4135b0. 2006 wrapper 0x405530.

Makes the menu's save use the script's global `save()`: when the player saves, the engine
calls `save()`, and the returned string becomes the slot's description [exe DC 0x405820,
0x4135bd, 0x413bcf; helper 0x404e30 with "save" at 0x482c07] [script DC: save] [script 2006:
save]. The save path calls it without checking that `scMenuSetSaveFunc` was called
[exe DC 0x413bcf]. The save file itself is described in
[formats.md](formats.md#appendix-original-save-files-scream0nn).

### scMenuShow

`scMenuShow()` · *not used by Scratches*

Source: DC wrapper 0x405720 → 0x412cb0. 2006 wrapper 0x405450.

Opens the main menu. Opening the menu checks save slots 1–11 for existing files
[exe DC 0x412cb0].

- In node or zoom view, Esc opens the menu the same way if `SC_SHOWMENU` is set; otherwise
  Esc shuts the game down as `scTerminate` does [exe DC 0x417a23–0x417a69, 0x410740].
  `scMenuSet` and `scEnable(SC_SHOWMENU)` set it, `scDisable(SC_SHOWMENU)` clears it
  [exe DC 0x41349d, 0x40fbcf, 0x40fdb2].
- `scAbort` opens the menu the same way, after the extra steps listed under
  [`scAbort`](#scabort) [exe DC 0x4106d0].

---

## Timers

**Ticking.** Timers advance at most once a second, checked every frame [exe DC 0x412010,
0x412019–0x412079; 0.001 at 0x48397c; registered at 0x4105e1]. They also advance during the
blocking calls (see [Conventions](#conventions)).

**Firing.** On each tick an active timer counts up. When the count reaches its seconds, the
timer becomes inactive and its handler is called by name, so it fires once
[exe DC 0x4120bf–0x41210f, 0x41214a–0x4121ae].

**Re-creating.** Creating a timer with an id already in the list resets its count, seconds and
handler, and makes it active again [exe DC 0x412220, 0x412330]. Two quirks:

- The id search skips the most recently created entry, so re-using that id adds a second
  entry instead [exe DC 0x41225d, 0x41237b].
- Id 0 matches the unused slot 0, so it always adds a new entry. Such timers still run,
  because ticking starts at slot 1 [exe DC 0x412277, 0x41208f].

**Limits.**
- No `sc*` function stops a timer [exe DC: registration table].
- Each list holds at most 31 timers: one list per room, plus one global list
  [exe DC 0x41228a, 0x412396].
- Creating a 32nd shows "Too many timers!" and the engine quits [exe DC 0x41228f–0x41229b;
  string at 0x483984]. The 2006 exe has the same check [exe 2006 0x40ffb0, 0x4100c0].

Saved games include the global list and every room's list [exe DC 0x411370].

### scTimerCreate

`scTimerCreate(id, seconds, handler)` · Calls 3 / 3

Source: DC wrapper 0x4060c0 → 0x412220. 2006 wrapper 0x405c90 → 0x40ffb0.

Creates the timer in the **bound room** [exe DC 0x412240]. It counts only while that room is
the current room, in any state, so it pauses while the player is elsewhere
[exe DC 0x412121–0x412138: the current room, no state check].

### scTimerCreateGlobal

`scTimerCreateGlobal(id, seconds, handler)` · Calls 27 / 26

Source: DC wrapper 0x406120 → 0x412330. 2006 wrapper 0x405cf0 → 0x4100c0.

Creates a world timer and logs "Starting timer %d..." [exe DC 0x412343]. It counts only in
node view (`SC_STNODE` 4000): not in zooms, menus, choices, the inventory or the journal
[exe DC 0x41207f–0x412089].

---

## Sound effects

**Channels.** The engine has 17 sound channels, numbered 1–17 [exe DC 0x401af0]. The script
constants name some of them [script DC: scream.sc constants] [script 2006: scream.sc
constants]:

| Constant | Channel |
|---|---|
| `SC_STEP` | 8 |
| `SC_3DA`–`SC_3DD` | 9–12 |
| `SC_DIALOGUE` | 13 |
| `SC_FX` | 14 |

Channels 1–6 carry music (see [tracks](#music-tracks) and the [playlist](#music-playlist)).
Channels 15–17 have no constant, and only `scFXPlay` and the zoom/overlay sounds use them
[exe DC 0x4160d0].

**Volume.** A volume value V sets the channel's gain to V × 0.1, so 10 is full gain
[exe DC 0x4161ae; data scream.exe (DC) @0x80cdc = 0.1] [exe 2006 0x4139ae].
- The exe does not clamp V.
- Every channel starts at gain 1.0 (volume 10) when audio starts [exe DC 0x401f09].
- A channel keeps its volume when a new file is loaded onto it. The new file starts at that
  volume, and any fade in progress is cancelled [exe DC 0x4017db, 0x401814].

**Fades.** A fade value F is a whole number, not a time [exe DC 0x401366; data scream.exe
(DC) @0x7f0a4 = 0.0025] [exe 2006 0x40136c]:
- On each audio tick, a fade moves the gain by 0.0025 / F.
- A fade from gain 1.0 to 0 therefore takes 400 × F ticks. A fade from a lower volume takes
  proportionally fewer.
- F = 0 means no fade [exe DC 0x40135d].

**The audio tick.** The audio tick runs on its own thread. The thread updates channels 1–17,
calls `Sleep(1)`, and repeats [exe DC 0x401af0, 0x419540] [exe 2006 0x416ce0]. The exe
imports no call that changes the Windows timer resolution [exe DC: import table]. So the
length of a tick, and with it the real length of a fade, depends on the PC.
[unverified: the tick length in milliseconds; it is set by Windows, not by the exe]

**Kinds of fade.**
- A fade-in sets the gain to 0, then raises it each tick until it reaches the channel's
  volume [exe DC 0x401b70, 0x4013ce]. Setting a fade-in cancels any fade-out
  [exe DC 0x401b8c].
- A fade-out starts from the channel's volume, or from the current gain if a fade-in is still
  running. Setting a fade-out cancels the fade-in [exe DC 0x401b20]. The gain drops each tick
  until it is below one step, and then the fade ends. The sound keeps playing
  [exe DC 0x401372, 0x4014b7].

When a stream reaches its end and does not loop, the channel stops by itself
[exe DC 0x40147b].

### scFXPlay

`scFXPlay(file)` · Calls 369 / 313

Source: DC wrapper 0x4051f0 → 0x4160d0. 2006 wrapper 0x405070 → 0x4138d0.

- Reads argument 1 as a file name [exe DC 0x4051f2].
- Plays it once on the first of channels 14–17 that is not playing, after unloading whatever
  that channel held [exe DC 0x4160d0] [exe 2006 0x4138d0].
- If all four channels are playing, the sound is dropped [exe DC 0x4160ee].
- The sound never loops [exe DC 0x416112].
- It plays at that channel's volume, which is 10 unless a script changed it
  [exe DC 0x401f09].

Two calls in the DC script pass a channel first, in the `scFXPlayEx` form [script DC:
walkHallway_dc] [script DC: finale]. The 2006 script has one such call [script 2006: finale].
The exe still reads argument 1 as the file name, so these calls ask for a file named after
the channel number [exe DC 0x4051f2]. The load function logs a warning when a file is not
found [exe DC 0x401650]. The archive has no such name, so the lookup returns no file and
nothing plays [exe DC 0x40eb9f–0x40ec39] (see [formats.md](formats.md#lookup)).

### scFXPlayEx

`scFXPlayEx(channel, file, wait, loop)` · Calls 275 / 249

Source: DC wrapper 0x405210 → 0x416120. 2006 wrapper 0x405090 → 0x413920.

Arguments are read as channel = 1, file = 2, wait = 3, loop = 4 [exe DC 0x405219]
[exe 2006 0x405090].

- Stops and unloads whatever the channel held, then loads the file and plays it at the
  channel's volume [exe DC 0x416120, 0x401814].
- A non-zero `loop` makes the stream rewind at its end [exe DC 0x4014f0, 0x4011f0].

**With a non-zero `wait`,** the call doesn't return while the channel is playing. During the
wait the engine keeps running frames, timers included [exe DC 0x41615b, 0x412010].
- Each frame it reads one window message. If that message is an Esc key-down, the channel is
  stopped and unloaded and the call returns [exe DC 0x416160, 0x419b80].
- A looping sound with `wait` therefore blocks until Esc.
- A sound that ends normally is left loaded [exe DC 0x416159].

The scripts also use this call to play music files on channel 1, through the script global
`SC_STEREO_PATCH = 1` [script DC: musicEnterCave, enterFurnace] [script 2006: enterFurnace].

### scFXSet

`scFXSet(channel, what, value)` · Calls 134 / 117

Source: DC wrapper 0x405290 → 0x416180. 2006 wrapper 0x405100 → 0x413980.

Arguments are read as channel = 1, what = 2, value = 3 [exe DC 0x405298].

| `what` | Effect |
|---|---|
| `SC_VOLUME` (41) | sets the channel's volume and applies it at once [exe DC 0x4161a8, 0x401aa0]. It does not cancel a running fade [exe DC 0x401aa0] |
| `SC_FADEIN` (42) | starts a fade-in at rate `value` [exe DC 0x4161c4] |
| `SC_FADEOUT` (43) | starts a fade-out at rate `value` [exe DC 0x41619e]. A fade-out doesn't stop the sound [exe DC 0x401310] |
| anything else | does nothing [exe DC 0x416192] |

The 2006 exe does the same [exe 2006 0x413980].

### scFXStop

`scFXStop(channel)` · Calls 44 / 40

Source: DC wrapper 0x4052f0 → 0x4161d0. 2006 wrapper 0x405160 → 0x4139d0.

If the channel has a file loaded, stops and unloads it. The channel keeps its volume
[exe DC 0x4161d0, 0x401570, 0x4019d0].

---

## Music: tracks

**Channels.** A track plays on channels 1–3:
- Channel 1 gets the front file.
- Channels 2 and 3 get the second and third files, but only while surround music is on
  [exe DC 0x41628a, 0x416297]. With surround off, only the front file plays.
- Channel 1 sits at the listener. Channel 2 sits at (−10, 10, −10), and channel 3 at
  (10, 10, −10) [exe DC 0x401650].

**The surround setting.**
- It is on by default [exe DC 0x40f3d2], and is then read from the `SURROUND` key of
  `scream.cfg` at start-up [exe DC 0x4061e1, 0x40f6d6].
- The shipped configs set it to 0 [data dc/scream.cfg @0x213] [data 2006/scream.cfg @0x216].
- `scEnable(SC_SURROUND)` and `scDisable(SC_SURROUND)` also set it [exe DC 0x40fbc4,
  0x40fda3].
- Key code 0x71 toggles it, shows "surround music ON/OFF", and restarts a playing playlist
  [exe DC 0x4177d5, 0x417811].

**Starting and stopping.** Starting a track stops and unloads channels 1–3 first
[exe DC 0x416224]. While the [playlist](#music-playlist) is playing, `scTrackPlay`,
`scTrackStop` and `scTrackFadeOut` do nothing [exe DC 0x4161f3, 0x416390, 0x416405].

### scTrackSet

`scTrackSet(file, loop)` · Calls 10 / 5

Source: DC wrapper 0x405b40 → 0x4164c0. 2006 wrapper 0x405770 → 0x413cd0.

Arguments are read as file = 1, loop = 2 [exe DC 0x405b48].

- Stores the front file and `loop` on the bound track (made with
  `scCreate(SC_TRACK, name)`) [exe DC 0x4164c0].
- Clears the track's 3D flag. No reader of that flag was found in the track or playlist code
  [exe DC 0x4161f0, 0x40ee00].
- Does not clear the rear file names [exe DC 0x4164c0].

What `loop` means depends on where the track plays:
- Outside the playlist, a non-zero `loop` makes the stream rewind at its end
  [exe DC 0x416347, 0x4011f0].
- In the playlist, `loop` is the number of plays, and 0 means for ever [exe DC 0x40efed].

### scTrackSet3D

`scTrackSet3D(front, rearLeft, rearRight, loop)` · Calls 28 / 27

Source: DC wrapper 0x405b80 → 0x416510. 2006 wrapper 0x4057b0 → 0x413d20.

The three-file form of `scTrackSet` [exe DC 0x405b8a, 0x416510]:
- Arguments 1–3 are stored as the files for channels 1, 2 and 3.
- Argument 4 is stored as `loop`.
- It sets the track's 3D flag.

The shipped scripts name the files `-F`, `-RL` and `-RR` [script DC: musicExitGeneric_dc].

### scTrackSetEx

`scTrackSetEx(what, value)` · Calls 108 / 90

Source: DC wrapper 0x405bf0 → 0x4165a0. 2006 wrapper 0x405810 → 0x413db0.

Arguments are read as what = 1, value = 2 [exe DC 0x405bf7].

- Stores `SC_VOLUME`, `SC_FADEIN` or `SC_FADEOUT` on the bound track [exe DC 0x4165a0].
- Nothing is applied to a track that is already playing. The values are used the next time
  the track starts [exe DC 0x4162bd, 0x40f17f].
- While it plays, the playlist rebinds the track slot to its current track
  [exe DC 0x40efb3]. During the playlist, this call therefore edits the playlist's current
  track.

[unverified: a new track's defaults; track creation was not traced]

### scTrackPlay

`scTrackPlay(name)` · Calls 35 / 29

Source: DC wrapper 0x405ae0 → 0x4161f0. 2006 wrapper 0x405710 → 0x4139f0.

1. Binds the named track [exe DC 0x416207].
2. Stops and unloads channels 1–3.
3. Loads the track's files, the rear ones only while surround is on.
4. Sets channels 1–3 to the track's `SC_VOLUME` × 0.1 [exe DC 0x4162bd; data scream.exe (DC)
   @0x80ce0 = 0.1].
5. Starts a fade-in on all three channels if the track's `SC_FADEIN` is non-zero
   [exe DC 0x416311].
6. Plays the track from the beginning [exe DC 0x416347].

The track's `SC_FADEOUT` is not used here [exe DC 0x4161f0].

### scTrackStop

`scTrackStop()` · Calls 11 / 9

Source: DC wrapper 0x405b00 → 0x416390. 2006 wrapper 0x405730 → 0x413b90.

Stops and unloads channels 1–3 at once [exe DC 0x416390].

### scTrackFadeOut

`scTrackFadeOut(n)` · Calls 8 / 7

Source: DC wrapper 0x405b10 → 0x416400. 2006 wrapper 0x405740 → 0x413c00.

Starts a fade-out at rate `n` on channels 1–3 [exe DC 0x416413, 0x401600].

- If channel 1 has less than `n` left to play, the rate is 1 instead. [unverified: the unit of
  "time left"; it is computed from 0x43a680 and 0x43b7c0, which were not traced]
- With `n` = 0, the fade-out is switched off and the channels go back to the full track
  volume [exe DC 0x401b20, 0x40135d].
- With a negative `n`, it binds the last track queued in the playlist and uses that track's
  `SC_FADEOUT` in place of `n` [exe DC 0x416456]. This also changes the bound track
  [exe DC 0x416468].

The call does not stop the track [exe DC 0x401310]. The scripts mostly leave the track playing
after it [script DC: musicExitGeneric_dc, finale].

---

## Music: playlist

*Not used by Scratches.* A queue of tracks that play in turn. Each new play goes on the other
bank of channels, 1–3 or 4–6 [exe DC 0x40f0e2]. A separate thread updates the playlist,
calling `Sleep(1)` between updates [exe DC 0x419570, 0x410613].

- **Capacity.** `scPlaylistClear` clears 38 slots in the DC exe [exe DC 0x4165f0] and 32 in
  the 2006 exe [exe 2006 0x413dfc]. `scPlaylistQueue` does not check the limit
  [exe DC 0x416890].
- **Plays.** A track plays `loop` times, and 0 means for ever [exe DC 0x40efed]. Each repeat
  reloads the track on the other bank [exe DC 0x40f067]. Streams do not loop inside the
  playlist [exe DC 0x40f218].
- **Change-over.**
  - On a track's last play, the change starts when the time left drops under the track's
    `SC_FADEOUT` [exe DC 0x40f076]. The next track then starts on the other bank.
  - The old bank fades at rate `SC_FADEOUT` and is cut at the next one-second mark, within
    about a second [exe DC 0x40f094, 0x40ee62]. One-second marks come from `GetTickCount` ×
    0.001 [exe DC 0x40ee4f, 0x41c1f0].
  - With `SC_FADEOUT` 0, the track plays to its end before the next one starts
    [exe DC 0x40efe8].
- **Fade-in.** A track's `SC_FADEIN` applies only to its first play [exe DC 0x40f1ec].
- **Dialogue.** `scScriptGo` lowers channels 1–7 by 0.25 for the length of a dialogue (see
  [scScriptGo](#scscriptgo)). A playlist track that starts during dialogue starts 0.25 lower
  [exe DC 0x40f18e].
- **Binding.** The playlist rebinds the track slot to its current track on every update
  while playing [exe DC 0x40efb3].

[unverified: the 2006 playlist update; only its wrappers and their direct callees were checked
(exe 2006 0x413df0–0x4140a0)]

### scPlaylistClear

`scPlaylistClear()` · *not used by Scratches*

Source: DC wrapper 0x4059d0 → 0x4165e0. 2006 wrapper 0x405660 → 0x413df0.

If the playlist is playing, it first stops it, as `scPlaylistStop(0)` would
[exe DC 0x416616]. Then it empties the queue and resets the position [exe DC 0x4165ea].

### scPlaylistQueue

`scPlaylistQueue(track)` · *not used by Scratches*

Source: DC wrapper 0x405a60 → 0x416890. 2006 wrapper 0x4056f0 → 0x4140a0.

Looks up a track by name and appends it. The first track queued becomes the current one
[exe DC 0x416890].

### scPlaylistStart

`scPlaylistStart()` · *not used by Scratches*

Source: DC wrapper 0x405a00 → 0x416690. 2006 wrapper 0x405690 → 0x413e60.

Does nothing if the queue is empty. Otherwise it starts the playlist from the current
position, or from track 1 if there is none [exe DC 0x416690]. `scPlaylistStop` clears the
position, so a start after a stop begins at track 1 [exe DC 0x416771].

### scPlaylistStop

`scPlaylistStop(n)` · *not used by Scratches*

Source: DC wrapper 0x405a10 → 0x4166c0. 2006 wrapper 0x4056a0 → 0x413ec0.

Does nothing unless the playlist is playing [exe DC 0x4166dc]. It stops the playlist and
clears the position [exe DC 0x4166f1, 0x416771]. What happens to the bank now playing depends
on `n`:

- **`n` > 0:** the bank fades at rate `n` and is cut after `n` one-second marks. If less than
  `n` is left, it fades at rate 1 and is cut at the next mark [exe DC 0x41678c].
- **`n` = 0:** no fade and no cut, so the current track plays to its end [exe DC 0x4167ca].
- **`n` < 0:** the last queued track's `SC_FADEOUT` is used, and that track is bound
  [exe DC 0x416700].

### scPlaylistNext / Previous / First / Last

`scPlaylistNext()`, `scPlaylistPrevious()`, `scPlaylistFirst()`, `scPlaylistLast()` ·
*not used by Scratches*

Source: DC wrappers 0x4059f0 / 0x4059e0 / 0x405a40 / 0x405a50 → 0x416650 / 0x416620 /
0x416850 / 0x416870. 2006 wrappers 0x405680 / 0x405670 / 0x4056d0 / 0x4056e0.

Each moves the current position, and does nothing if the queue is empty [exe DC 0x416650,
0x416620, 0x416850, 0x416870]:

| Function | Moves to |
|---|---|
| `scPlaylistNext` | one forward, no further than the last track |
| `scPlaylistPrevious` | one back, no further than track 1 |
| `scPlaylistFirst` | track 1 |
| `scPlaylistLast` | the last track |

If the playlist is playing and the position changed, the new track starts on the other bank.
The old track is neither faded nor cut, so it plays to its end and the two overlap
[exe DC 0x40f0dc]. If the position didn't change, nothing happens.

---

## Music: legacy

### scMusic*

`scMusicPlay`, `scMusicStop`, `scMusicFadeIn`, `scMusicFadeOut` · *not used by Scratches*

Source: DC wrappers 0x405850 / 0x405860 / 0x405840 / 0x405830. 2006 wrappers 0x405560 /
0x405570 / 0x405550 / 0x405540.

No-ops. In both exes these functions return at once, without reading their arguments
[exe DC 0x405830–0x405860] [exe 2006 0x405540–0x405570].
