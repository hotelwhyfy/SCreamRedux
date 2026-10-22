#pragma once

// The game world as the scripts build it with scCreate/scBegin/scEnd/scBind.
// Everything is created once at boot; handlers then mutate it (textures,
// enabled flags) and the engine moves the player around inside it.

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// What happens on a click or item use: an sc action kind plus its arguments.
struct Action {
    int kind = 0;          // sc::NODE, sc::ROOM, sc::ZOOM, sc::CUSTOM, sc::FEEDBACK, ...
    std::string arg;       // target name, handler name or feedback text
    std::string arg2;      // voice file for FEEDBACK_EX
    bool empty() const { return kind == 0; }
};

// Using an inventory item on an object (scReaction) or on another item (scInventoryReaction).
struct Reaction {
    std::string item;
    Action action;
};

struct Object {
    std::string name;
    int face = 0;          // cube face, or sc::ZOOM inside a zoom (where it is ignored)
    Rect rect;
    int cursor = 0;        // scOnHover
    Action click;          // scOnClick
    std::vector<Reaction> reactions;
    bool enabled = true;
};

// A node (cube panorama) or zoom (2D close-up); both hold clickable objects.
struct View {
    std::string name;
    bool isZoom = false;
    std::array<std::string, 6> faces;  // node: indexed by face - 1
    std::string image;                 // zoom
    std::string footstep;              // node: scFootstep
    std::vector<std::pair<int, std::string>> audio;  // zoom: scAudio(SC_ZOOM|SC_ZOOMOUT, ogg)
    std::vector<std::unique_ptr<Object>> objects;

    std::vector<Object*> find(const std::string& objectName);
};

struct Room {
    std::string name;
    std::vector<std::unique_ptr<View>> nodes;
    std::vector<std::unique_ptr<View>> zooms;
    std::string lastVisited;  // scSetLastVisited; node entered by scSwitch(SC_ROOM)
    std::string onEnter;      // scOnEnter: handler run after the room is entered
    std::string onExit;       // scOnExit: stored, but scream.exe never calls it

    View* node(const std::string& n);
    View* zoom(const std::string& n);
};

struct Item {
    std::string name;
    std::string icon, label, text, voice;  // scInventorySet
    std::vector<Reaction> reactions;       // scInventoryReaction
};

struct DialogueLine {
    std::string voice, text;
    int flag = 0;  // third arg of scScriptAdd: 1 = hint line, played only with SC_HINTS on
};

struct Dialogue {
    std::string name;
    std::vector<DialogueLine> lines;
};

struct Choice {
    std::string name;
    std::vector<std::pair<std::string, std::string>> options;  // scChoiceAdd(text, handler)
    int lastSelected = -1;  // removed by scChoiceRemove
};

// SC_OVERLAY (never created by the shipped scripts): scOverlay(image, enterSound, leaveSound) (0x412a00 copies them to +0x34,
// +0x64, +0x94): the image covers the view; the first sound plays on
// scSwitch(SC_OVERLAY), the second when a click dismisses it (0x414501).
struct Overlay {
    std::string name;
    std::array<std::string, 3> args;
};

struct Track {
    std::string name;
    std::array<std::string, 3> files;  // stereo: [0]; surround: front, rear-left, rear-right
    bool surround = false;
    int loop = 0;
    int volume = 10, fadeIn = 0, fadeOut = 0;
};

class World {
public:
    std::map<std::string, std::unique_ptr<Room>> rooms;
    std::map<std::string, std::unique_ptr<Item>> items;
    std::map<std::string, std::unique_ptr<Dialogue>> dialogues;
    std::map<std::string, std::unique_ptr<Choice>> choices;
    std::map<std::string, std::unique_ptr<Track>> tracks;
    std::map<std::string, std::unique_ptr<Overlay>> overlays;

    // "Current" objects that builder calls (scPlace, scTexture, ...) act on.
    struct Bindings {
        Room* room = nullptr;
        View* node = nullptr;
        View* zoom = nullptr;
        View* view = nullptr;           // whichever of node/zoom was bound last
        std::vector<Object*> objects;   // all objects sharing the bound name
        Item* item = nullptr;
        Dialogue* dialogue = nullptr;
        Choice* choice = nullptr;
        Track* track = nullptr;
        Overlay* overlay = nullptr;
    } bound;

    // Returns false (and leaves bindings unchanged) if the parent scope is missing.
    bool create(int kind, const std::string& name);
    bool bind(int kind, const std::string& name);
    void begin(int kind);
    void end(int kind);

    // Where scBind(SC_OBJECT) looks (0x403fd0): in the bound node while the
    // engine is in node state, in the bound zoom in zoom state, nowhere in any
    // other state (inventory, journal, ...). Unset while the world is built:
    // then the last bound node or zoom.
    enum class Scope { Build, Node, Zoom, None };
    std::function<Scope()> objectScope;

    // Bind exactly what a click on `object` inside `view` of `room` refers to.
    void bindClicked(Room* room, View* view, Object* object);

    Room* room(const std::string& name);

private:
    std::vector<int> scopes_;
};
