#pragma once
// MVGL's Spark particle systems (`.sprk`, 'SPAK'; docs/FORMATS.md): a tree of nodes - null nodes,
// emitters that spawn other nodes, sprites (a textured quad each) - with their materials and
// textures. What the 21 shipped files use: node kinematics (position, rotation, scale, velocity,
// acceleration, turning and scale's change, each a random range, integrated a step at a time as
// SparkNode::UpdateKineLocal* does), the global transform outside it (UpdateKineGlobal), the
// follow bits (+0x16c: a node that doesn't follow its parent stays where it was made - the embers a
// projectile leaves behind), emitters (point and box shapes; once, by rate and by interval; +0x172's
// end after the last particle), sprites (quads, turned or billboarded with their roll, their pivots,
// one colour or four) and their materials' blending, and the curves (ANIM chunks) on the nodes'
// channels and the materials' colours. Not in (no shipped file has them): trails (type 8), forces
// (SparkForce nodes), models and cameras, the circle / ring / point sprite shapes, the upright
// billboard (+0x26a bit 3), a start delay (+0x174), the partial follow modes' rotation and scale,
// and the textures' crop curves (every shipped one is the whole image).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "assets/skeleton.hpp"

namespace cr3 {

struct SparkRange {
    float min = 0, max = 0;
};

// An ANIM chunk's curve (SparkFCurve): keys of a frame, an interpolation (0 step, 1 linear, 2
// Bezier over the value handles) and a value; before the first and after the last key held (0),
// looped (1) or looped with the span's change added each lap (2)
struct SparkCurve {
    int channel = 0;
    int pre = 0, post = 0;
    struct Key {
        int frame = 0, interp = 1;
        float value = 0, in = 0, out = 0; // (in / out: the handles' value offsets)
    };
    std::vector<Key> keys;
    float eval(int frame, float fallback) const;
};
using SparkAnim = std::vector<SparkCurve>;

struct SparkNode {
    int type = 0; // 0 null, 2 emitter, 3 sprite (8 trail, ... - not drawn)
    uint32_t id = 0;
    std::string name;
    std::array<SparkRange, 3> position, rotation, scale, velocity; // +0x48 (rotation in degrees)
    std::array<SparkRange, 3> accel;  // +0x90 units a second a second (channels 0x13 .. 0x15)
    std::array<SparkRange, 3> spin, grow; // +0xf0 turning (degrees a second), +0x108 scale's change a second
    std::array<SparkRange, 3> globalPosition, globalRotation, globalScale; // +0x120 / +0x138 / +0x150 (UpdateKineGlobal)
    uint32_t link = 0;             // +0x168: y / z taking x's random value (bits 0/1 position, 4/5 velocity,
                                   // 6/7 global position, 8/9 rotation, 12/13 turning, 14/15 global rotation,
                                   // 16/17 scale, 20/21 scale's change, 22/23 global scale)
    uint32_t follow = 0x70;        // +0x16c bits 4 / 5 / 6: follows its parent's position / turn / scale
                                   // (0: stays where it was made - SparkNode::UpdateMatrix)
    int endMode = 0;               // +0x172: at its life's end 0 gone, 1 gone once its particles are (CheckCompleted)
    int lifeMin = -1, lifeMax = -1; // +0x178 frames (-1: for ever)
    int anim = -1;                 // +0x198: its curves (an ANIM chunk's index)
    std::vector<int> children;     // +0x188 / +0x190: the nodes it starts with (indices into nodes)
    // an emitter's
    int order = 0;                 // +0x1a8: which child kind next - 0 in turn, 1 at random
    int mode = 0;                  // +0x1a9: 0 by rate (count a second), 1 once, 2 every interval
    int countMin = 0, countMax = 0; // +0x1ac
    int intervalMin = 1, intervalMax = 1; // +0x1b0 frames
    std::vector<int> emits;        // +0x1b8 / +0x1c0: the kinds it spawns (indices into nodes)
    int shape = 0;                 // +0x1c4: 0 a point, 1 a box (else: a point)
    std::array<SparkRange, 3> extent; // +0x1c8: the box's half sizes
    int maxParticles = 0;          // +0x1e0: alive at once, per kind
    // a sprite's
    SparkRange sizeX, sizeY;       // +0x1a8 (+0x268 bits 0 / 1: y and z take x's)
    SparkRange pivotX, pivotY;     // +0x1c0 / +0x1c8: added to the pivot's place (channels 0x30 / 0x31)
    std::array<std::array<SparkRange, 4>, 4> color; // +0x1d8: the corners' r g b a
    uint32_t material = 0;         // +0x258: its material's id
    int pivot = 4;                 // +0x263 (4: the centre)
    uint32_t flags = 0;            // +0x268: bit 16 one colour (corner 0's), bit 18 billboarded (with +0x26a bit 3: upright)
    int quadShape = 0;             // +0x274: 0 a quad (1 circle, 2 ring, 3 point - not drawn)
};

struct SparkMaterial {
    uint32_t id = 0;
    std::string name, texture;     // texture: its texture node's image (images\<texture>.img)
    std::array<SparkRange, 4> color; // +0x48 r g b a (the shader's DiffuseColor)
    bool blend = false;            // +0x170 bit 15
    int blendMode = 0;             // +0x186: 0 add (SRC_ALPHA, ONE), 1 subtract (reverse), 2 alpha
    bool depthTest = true, depthWrite = false; // +0x170 bits 17, 18
    bool alphaTest = false;        // +0x170 bit 19, against alphaRef (+0x180)
    bool spriteColor = false;      // +0x170 bit 7: the sprites' corner colours drawn (SparkSprite::UpdateColor)
    bool square = false;           // +0x170 bit 11: its sprites' height is their width (SparkSprite::UpdateSize)
    int anim = -1;                 // +0x240: its colour's curves (0x35 .. 0x38 r g b a)
    float alphaRef = 0.5f;
    std::string shaderKey;         // +0x188
};

struct SparkAsset {
    float fps = 30;                // header +0x10
    bool centeredRandom = false;   // Vita v103 stores a base and signed random radius
    std::vector<SparkNode> nodes;  // the tree's (types 0 .. 8), the root first
    std::vector<SparkMaterial> materials;
    std::vector<SparkAnim> anims;  // the ANIM chunks, in order
    int root = -1;
    // a channel's value at a frame: its curve's, else `fallback` (SparkUtils::Animate)
    float animate(int anim, int channel, int frame, float fallback) const;
    float rangeBase(const SparkRange& range) const { return centeredRandom ? (range.min + range.max) * 0.5f : range.min; }
    const SparkMaterial* materialById(uint32_t id) const;
};

bool parseSpark(const std::vector<uint8_t>& data, SparkAsset& out, std::string* err = nullptr);

// A running Spark effect: its nodes' instances, stepped by time; drawn as quads in the world
// under a root place (Fld2Effect's: the player's RootLocator)
class SparkEffect {
public:
    explicit SparkEffect(const SparkAsset* asset, uint32_t seed = 1);
    void step(float dt);
    struct Quad {
        float corner[4][3]; // (-x +y, +x +y, -x -y, +x -y) as BuildBox lays them
        float color[4][4];  // each corner's: its material's colour (times the sprite's corner's - +0x170 bit 7)
        const SparkMaterial* material = nullptr;
    };
    // its sprites' quads under `root`; `camera`: the camera's place (billboards turn to it). A node
    // that doesn't follow its parent keeps the root it was first drawn under
    void quads(const Mat34& root, const Mat34& camera, std::vector<Quad>& out) const;
    size_t alive() const;

private:
    struct Inst {
        int node = 0;
        float age = 0, life = -1;
        // (their drawn values, each a random value in its range: the curves add their change from the least)
        float pos[3]{}, rot[3]{}, scale[3]{1, 1, 1}, vel[3]{}, accel[3]{}, spin[3]{}, grow[3]{};
        float gpos[3]{}, grot[3]{}, gscale[3]{1, 1, 1};
        // (integrated a step at a time: velocity's sum, acceleration's, turning's, scale's change's)
        float move[3]{}, accVel[3]{}, turn[3]{}, growth[3]{};
        float gen[3]{};                            // (where its emitter's shape put it: +0x2a0)
        float size[2]{1, 1}, pivot[2]{}, color[4][4]{}, matColor[4]{}; // (size, colours: their random parts)
        float lifeScale = 1;                       // (the curves' frame: age x fps x this + 1)
        int emitted = 0, lastFrame = 0, next = 0, interval = 1; // (an emitter's)
        std::vector<std::vector<Inst>> spawned; // (an emitter's, per kind)
        std::vector<Inst> children;
        // (a node not following its parent: the root and its parent's place when it was first drawn -
        // +0x430 and +0x2b0)
        mutable bool anchored = false;
        mutable Mat34 anchorRoot;
        mutable float anchorPos[3]{};
    };
    const SparkAsset* asset_;
    uint32_t rng_;
    float age_ = 0;
    std::vector<Inst> roots_;
    float random(float a, float b);
    float random(const SparkRange& r) { return random(r.min, r.max); }
    Inst make(int node);
    void stepInst(Inst& in, float dt);
    void emit(Inst& in, int count);
    Mat34 local(const Inst& in) const;
    int frame(const Inst& in) const;
    void collect(const Inst& in, const Mat34& root, const Mat34& parent, const Mat34& camera, std::vector<Quad>& out) const;
    bool done(const Inst& in) const;
    static size_t count(const Inst& in);
};

} // namespace cr3
