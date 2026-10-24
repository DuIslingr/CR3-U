#pragma once
// MVGL's Spark particle systems (`.sprk`, 'SPAK'; docs/FORMATS.md): a tree of nodes - null nodes,
// emitters that spawn other nodes, sprites (a textured quad each) - with their materials and
// textures. The subset the field's effects use (fe201am10, the lab's rain): node kinematics
// (position, rotation, scale and velocity, each a random range), emitters (point and box shapes;
// once, by rate and by interval), sprites (quads, turned or billboarded) and their materials'
// blending. Not in: the curves (the ANIM chunk: every channel at its default), the other node
// kinds (trails, models, cameras ..), forces, the sprites' pivots but the centre's.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "assets/skeleton.hpp"

namespace cr3 {

struct SparkRange {
    float min = 0, max = 0;
};

struct SparkNode {
    int type = 0; // 0 null, 2 emitter, 3 sprite (8 trail, ... - not drawn)
    uint32_t id = 0;
    std::string name;
    std::array<SparkRange, 3> position, rotation, scale, velocity; // +0x48 (rotation in degrees)
    uint32_t link = 0;             // +0x168: y / z taking x's random value (bits 0/1 position, 16/17 scale ..)
    int lifeMin = -1, lifeMax = -1; // +0x178 frames (-1: for ever)
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
    SparkRange sizeX, sizeY;       // +0x1a8
    std::array<std::array<SparkRange, 4>, 4> color; // +0x1d8: the corners' r g b a
    uint32_t material = 0;         // +0x258: its material's id
    int pivot = 4;                 // +0x263 (4: the centre)
    uint32_t flags = 0;            // +0x268: bit 18 billboarded (with +0x26a bit 3: upright)
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
    float alphaRef = 0.5f;
    std::string shaderKey;         // +0x188
};

struct SparkAsset {
    float fps = 30;                // header +0x10
    std::vector<SparkNode> nodes;  // the tree's (types 0 .. 8), the root first
    std::vector<SparkMaterial> materials;
    int root = -1;
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
        float color[4];     // the sprite's (corner 0's) times its material's
        const SparkMaterial* material = nullptr;
    };
    // its sprites' quads under `root`; `camera`: the camera's place (billboards turn to it)
    void quads(const Mat34& root, const Mat34& camera, std::vector<Quad>& out) const;
    size_t alive() const;

private:
    struct Inst {
        int node = 0;
        float age = 0, life = -1;
        float pos[3]{}, rot[3]{}, scale[3]{1, 1, 1}, vel[3]{};
        float size[2]{1, 1}, color[4]{1, 1, 1, 1};
        int emitted = 0, lastFrame = 0, next = 0, interval = 1; // (an emitter's)
        std::vector<std::vector<Inst>> spawned; // (an emitter's, per kind)
        std::vector<Inst> children;
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
    void collect(const Inst& in, const Mat34& parent, const Mat34& camera, std::vector<Quad>& out) const;
    static size_t count(const Inst& in);
};

} // namespace cr3
