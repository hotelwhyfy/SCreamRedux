#pragma once

// Engine enums, as defined by the game's scream.sc (see extracted/decompiled/dc/scream.lua).
// The scripts pass these numbers to the sc* API, so the values must match exactly.
namespace sc {

// Quality levels
constexpr int LO = 101, MED = 102, HI = 103;

// Settings (scSet / scGet / scEnable / scDisable)
constexpr int WIDTH = 1000, HEIGHT = 1001, DEPTH = 1002, RATIOX = 1003, RATIOY = 1004,
              VIEWX = 1005, VIEWY = 1006, QUALITY = 1007, FOV = 1008, SPEED = 1009, LOG = 1010,
              QUICKMODE = 1011, FULLSCREEN = 1012, RESOURCE = 1013, RES_PATH = 1014,
              RES_FILE = 1015, CAM_SPEED = 1016, TXT_SPEED = 1017, MOV_SPEED = 1018,
              SKY_SPEED = 1019, TEX_SIZE = 1020, MSK_SIZE = 1021, SKY_SIZE = 1022,
              INV_SIZE = 1023, CUR_SIZE = 1024, SHOW_HOTSPOTS = 1025, SURROUND = 1026,
              SHOWMENU = 1027, SPLASH = 1028, HINTS = 1029, SKY = 1030, NIGHT = 1031,
              RAIN = 1032, FOG = 1033, GLOW = 1034, DREAM = 1035, CANDLE = 1036, LAMP = 1037,
              LANGUAGE = 1038, IMG_SIZE = 1039, DEBUG = 1040, FPS_STYLE = 1041,
              SKY_COLOR = 1042, CLOUD_COLOR = 1043, SCREEN_COLOR = 1044;

// Ambience presets (scSetAmbience)
constexpr int DAWN = 309, MORNING = 311, DUSK = 317, STORM = 319;

// Cube faces
constexpr int FRONT = 1, BACK = 2, LEFT = 3, RIGHT = 4, UP = 5, DOWN = 6;

// Object kinds (scCreate / scBind / scBegin / scEnd / scSwitch / scOnClick)
constexpr int ROOM = 2000, NODE = 2001, ZOOM = 2002, OBJECT = 2003, OVERLAY = 2004,
              ITEM = 2005, SCRIPT = 2006, CHOICE = 2007, TRACK = 2008;

// Save/state keys
constexpr int WORLD = 3000, STATE = 3001, BIND = 3002, NAME = 3003, SLOT = 3004, MENU = 3005,
              INTRO = 3006, TAKEN = 3007, IMAGE = 3008, INVENTORY = 3009, TAG = 3010,
              TIMER = 3011, OUTRO = 3012;
constexpr int STNODE = 4000, STVIDEO = 4001, STZOOM = 4002, STINV = 4003, STOVERLAY = 4004,
              STMENU = 4005, STLAST = 4006, STCHOICE = 4007, STBOOK = 4008;

// Click actions (scOnClick, scReaction, scInventoryReaction)
constexpr int FEEDBACK = 30, CUSTOM = 31, ZOOMOUT = 32, ZOOMBACK = 33, FEEDBACK_EX = 34;

// Track options (scTrackSetEx / scFXSet / scMenuSetEx)
constexpr int STEREO = 40, VOLUME = 41, FADEIN = 42, FADEOUT = 43;

// Cursors (scOnHover / scCursor)
constexpr int NORMAL = 21, FORWARD = 22, LOOK = 23, USE = 24, WAIT = 25;
constexpr int MENU_CURSOR = 3005;  // SC_MENU, as scCursor's key for the menu cursor

// Audio channels (scFXPlayEx / scFXSet / scFXStop)
constexpr int STEP = 8, A3DA = 9, A3DB = 10, A3DC = 11, A3DD = 12, DIALOGUE = 13, FX = 14;

// Transitions (scLookAt)
constexpr int INSTANT = 1, SMOOTH = 2;

// Hint states (scHints / scHintsSet)
constexpr int HINTS_INACTIVE = 0, HINTS_ACTIVE = 1, HINTS_OPEN = 2, HINTS_GRAB = 3,
              HINTS_CLOSE = 4, HINTS_ITEM = 5, HINTS_RESET = 6;

// Engine defaults (SC_DEF_* in scream.sc)
constexpr int DEF_TEX_SIZE = 1024;
constexpr float DEF_FOV = 53.0f;
constexpr int SCREEN_W = 800, SCREEN_H = 600;  // virtual 2D canvas used by zooms and UI

const char* faceName(int face);

}  // namespace sc
