#include "assets/spark.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace cr3 {

float SparkCurve::eval(int f, float fallback) const {
    // SparkFCurve::Eval / EvalInterpolation / EvalInner- and EvalOuterExtrapolation
    if (keys.empty()) return fallback;
    if (keys.size() == 1) return keys[0].value;
    const int first = keys.front().frame, last = keys.back().frame, span = last - first;
    float shift = 0;
    if (f < first || f >= last) {
        const int mode = f < first ? pre : post;
        if (mode == 0 || span <= 0) return f < first ? keys.front().value : keys.back().value;
        const int laps = f < first ? (first - f + span - 1) / span : (f - first) / span;
        if (mode == 2) shift = (keys.back().value - keys.front().value) * float(f < first ? -laps : laps);
        f += f < first ? laps * span : -laps * span;
    }
    for (size_t k = 0; k + 1 < keys.size(); ++k) {
        const Key& a = keys[k];
        const Key& b = keys[k + 1];
        if (f < a.frame || f >= b.frame) continue;
        const float t = float(f - a.frame) / float(b.frame - a.frame);
        if (a.interp == 0) return a.value + shift;
        if (a.interp == 2) { // (SparkUtils::CalcBezeir: a.value, a.value + a.out, b.value + b.in, b.value)
            const float u = 1 - t;
            return u * u * u * a.value + 3 * u * u * t * (a.value + a.out) + 3 * u * t * t * (b.value + b.in) + t * t * t * b.value + shift;
        }
        return a.value + t * (b.value - a.value) + shift;
    }
    return keys.back().value + shift;
}

float SparkAsset::animate(int anim, int channel, int frame, float fallback) const {
    if (anim < 0 || size_t(anim) >= anims.size()) return fallback;
    for (const SparkCurve& c : anims[size_t(anim)])
        if (c.channel == channel) return c.eval(frame, fallback);
    return fallback;
}

const SparkMaterial* SparkAsset::materialById(uint32_t id) const {
    for (const SparkMaterial& m : materials)
        if (m.id == id) return &m;
    return nullptr;
}

bool parseSpark(const std::vector<uint8_t>& d, SparkAsset& out, std::string* err) {
    auto fail = [&](const char* why) {
        if (err) *err = why;
        return false;
    };
    out = SparkAsset();
    if (d.size() < 0x18 || std::memcmp(d.data(), "SPAK", 4) != 0) return fail("not a Spark file");
    if (d[0x10]) out.fps = float(d[0x10]);
    auto u32 = [&](size_t p) { uint32_t v = 0; if (p + 4 <= d.size()) std::memcpy(&v, &d[p], 4); return v; };
    auto i16 = [&](size_t p) { int16_t v = 0; if (p + 2 <= d.size()) std::memcpy(&v, &d[p], 2); return int(v); };
    auto f32 = [&](size_t p) { float v = 0; if (p + 4 <= d.size()) std::memcpy(&v, &d[p], 4); return v; };
    // Vita v103 assets use smaller pointers and store a base and random radius.
    const bool vita = i16(8) == 103;
    out.centeredRandom = vita;
    auto range = [&](size_t p) {
        const float base = f32(p), other = f32(p + 4);
        const float radius = std::abs(other);
        return vita ? SparkRange{base - radius, base + radius} : SparkRange{base, other};
    };
    auto text = [&](size_t p, size_t n) {
        std::string s;
        for (size_t k = 0; k < n && p + k < d.size() && d[p + k]; ++k) s += char(d[p + k]);
        return s;
    };
    struct Raw {
        uint32_t children, childCount, emits, emitCount;
    };
    std::vector<Raw> raw;
    std::map<uint32_t, std::string> textures; // (texture nodes: id -> image)
    std::vector<uint32_t> list;               // NLST: the ids the nodes' lists index
    for (size_t o = vita ? 0x14 : 0x18; o + 8 <= d.size();) {
        const uint32_t size = u32(o + 4);
        if (size < 8 || o + size > d.size()) return fail("bad chunk");
        if (std::memcmp(&d[o], "NODE", 4) == 0) {
            const size_t nd = o + 0x10; // (the node's data: type, size, id, name ...)
            if (size < 0x18) return fail("short node chunk");
            const int type = i16(nd);
            if (type == 9) {
                if (size < 0x78) return fail("short texture node");
                textures[u32(nd + 4)] = text(nd + 0x48, 0x20);
            } else if (type == 10) {
                if (size - 0x10 < (vita ? 0x1b4u : 0x244u)) return fail("short material node");
                SparkMaterial m;
                m.id = u32(nd + 4), m.name = text(nd + 8, 0x40);
                for (int c = 0; c < 4; ++c) m.color[size_t(c)] = range(nd + 0x48 + size_t(c) * 8);
                const uint32_t f = u32(nd + (vita ? 0x11c : 0x170));
                m.blend = f >> 15 & 1, m.depthTest = f >> 17 & 1, m.depthWrite = f >> 18 & 1, m.alphaTest = f >> 19 & 1;
                m.alphaRef = f32(nd + (vita ? 0x12c : 0x180)), m.blendMode = d[nd + (vita ? 0x132 : 0x186)];
                m.spriteColor = f >> 7 & 1, m.square = f >> 11 & 1;
                m.anim = int(int32_t(u32(nd + (vita ? 0x1b0 : 0x240))));
                m.shaderKey = text(nd + (vita ? 0x134 : 0x188), 0x40);
                m.texture = std::to_string(u32(nd + (vita ? 0xd4 : 0xe0))); // (an id until the textures are known)
                out.materials.push_back(m);
            } else {
                const size_t minimum = type == 2 ? (vita ? 0x1c0 : 0x1e2) : type == 3 ? (vita ? 0x258 : 0x275) : (vita ? 0x194 : 0x19c);
                if (size - 0x10 < minimum) return fail("short Spark node");
                SparkNode n;
                n.type = type, n.id = u32(nd + 4), n.name = text(nd + 8, 0x40);
                for (int a = 0; a < 3; ++a) {
                    n.position[size_t(a)] = range(nd + 0x48 + size_t(a) * 8);
                    n.rotation[size_t(a)] = range(nd + 0x60 + size_t(a) * 8);
                    n.scale[size_t(a)] = range(nd + 0x78 + size_t(a) * 8);
                    n.velocity[size_t(a)] = range(nd + 0xd8 + size_t(a) * 8);
                    n.accel[size_t(a)] = range(nd + 0x90 + size_t(a) * 8);
                    n.spin[size_t(a)] = range(nd + 0xf0 + size_t(a) * 8);
                    n.grow[size_t(a)] = range(nd + 0x108 + size_t(a) * 8);
                    n.globalPosition[size_t(a)] = range(nd + 0x120 + size_t(a) * 8);
                    n.globalRotation[size_t(a)] = range(nd + 0x138 + size_t(a) * 8);
                    n.globalScale[size_t(a)] = range(nd + 0x150 + size_t(a) * 8);
                }
                n.link = u32(nd + 0x168), n.follow = u32(nd + 0x16c), n.endMode = d[nd + 0x172];
                n.anim = int(int32_t(u32(nd + (vita ? 0x190 : 0x198))));
                n.lifeMin = i16(nd + 0x178), n.lifeMax = i16(nd + 0x17a);
                if (vita) { // GetRandomValueI: base + (rand % spread) - spread / 2.
                    const int base = n.lifeMin, spread = std::max(n.lifeMax, 0);
                    n.lifeMin = base - spread / 2, n.lifeMax = n.lifeMin + std::max(spread - 1, 0);
                }
                Raw r{u32(nd + 0x188), u32(nd + (vita ? 0x18c : 0x190)), 0, 0};
                if (type == 2 && vita) {
                    n.order = d[nd + 0x198], n.mode = d[nd + 0x199];
                    n.countMin = n.countMax = i16(nd + 0x19a);
                    n.intervalMin = n.intervalMax = std::max(i16(nd + 0x19c), 1);
                    r.emits = u32(nd + 0x1a8), r.emitCount = u32(nd + 0x1ac);
                    n.shape = d[nd + 0x1b0];
                    for (int a = 0; a < 3; ++a) {
                        const float extent = f32(nd + 0x1b4 + size_t(a) * 4);
                        n.extent[size_t(a)] = {extent, extent};
                    }
                } else if (type == 2) {
                    n.order = d[nd + 0x1a8], n.mode = d[nd + 0x1a9];
                    n.countMin = i16(nd + 0x1ac), n.countMax = i16(nd + 0x1ae);
                    n.intervalMin = std::max(i16(nd + 0x1b0), 1), n.intervalMax = std::max(i16(nd + 0x1b2), 1);
                    r.emits = u32(nd + 0x1b8), r.emitCount = u32(nd + 0x1c0);
                    n.shape = d[nd + 0x1c4];
                    for (int a = 0; a < 3; ++a) n.extent[size_t(a)] = range(nd + 0x1c8 + size_t(a) * 8);
                    n.maxParticles = int(uint16_t(i16(nd + 0x1e0)));
                } else if (type == 3) {
                    n.sizeX = range(nd + (vita ? 0x198 : 0x1a8)), n.sizeY = range(nd + (vita ? 0x1a0 : 0x1b0));
                    n.pivotX = range(nd + (vita ? 0x1b0 : 0x1c0)), n.pivotY = range(nd + (vita ? 0x1b8 : 0x1c8));
                    for (int c = 0; c < 4; ++c)
                        for (int k = 0; k < 4; ++k) n.color[size_t(c)][size_t(k)] = range(nd + (vita ? 0x1c8 : 0x1d8) + size_t(c) * 0x20 + size_t(k) * 8);
                    n.material = u32(nd + (vita ? 0x248 : 0x258));
                    n.pivot = d[nd + (vita ? 0x24f : 0x263)];
                    n.flags = u32(nd + (vita ? 0x254 : 0x268));
                    n.quadShape = vita ? 0 : d[nd + 0x274];
                }
                out.nodes.push_back(n);
                raw.push_back(r);
            }
        } else if (std::memcmp(&d[o], "ANIM", 4) == 0) {
            // Vita packs its curves consecutively. The wider format puts an offset table before them.
            const size_t a = o + 0x10;
            const size_t end = o + size, table = a + 0x10;
            if (a + (vita ? 8 : 0x10) > end) return fail("short animation chunk");
            const size_t n = uint16_t(i16(a));
            const size_t curveHeader = vita ? 0xc : 0x10;
            size_t next = a + 8;
            if (!vita && table + n * 8 > end) return fail("short animation table");
            SparkAnim anim;
            for (size_t k = 0; k < n; ++k) {
                const size_t c = vita ? next : table + n * 8 + u32(table + k * 8);
                if (c + curveHeader > end) break;
                SparkCurve cv;
                cv.channel = uint16_t(i16(c)), cv.pre = d[c + 2], cv.post = d[c + 3];
                if (vita) {
                    if (cv.channel >= 0x2b && cv.channel <= 0x2c) cv.channel += 2; // Sprite size
                    else if (cv.channel >= 0x33 && cv.channel <= 0x36) cv.channel += 2; // Material diffuse
                    else if (cv.channel >= 0x60 && cv.channel <= 0x6f) cv.channel += 0xf; // Sprite colors
                }
                const size_t count = uint16_t(i16(c + 4)), keys = c + curveHeader;
                next = keys + count * 0x18;
                for (size_t j = 0; j < count && keys + (j + 1) * 0x18 <= end; ++j) {
                    const size_t q = keys + j * 0x18;
                    cv.keys.push_back({i16(q), int(int8_t(d[q + 2])), f32(q + 4), f32(q + 0xc), f32(q + 0x14)});
                }
                anim.push_back(cv);
            }
            out.anims.push_back(anim);
        } else if (std::memcmp(&d[o], "NLST", 4) == 0) {
            for (size_t p = o + 0x10; p + 4 <= o + size; p += 4) list.push_back(u32(p));
        } else if (std::memcmp(&d[o], "TERM", 4) == 0) {
            break;
        }
        o += size;
    }
    for (SparkMaterial& m : out.materials) {
        const auto it = textures.find(uint32_t(std::stoul(m.texture)));
        m.texture = it != textures.end() ? it->second : std::string();
    }
    auto byListed = [&](uint32_t from, uint32_t count, std::vector<int>& to) {
        if (from == 0xffffffffu) return;
        for (uint32_t k = 0; k < count && from + k < list.size(); ++k)
            for (size_t j = 0; j < out.nodes.size(); ++j)
                if (out.nodes[j].id == list[from + k]) to.push_back(int(j));
    };
    for (size_t j = 0; j < out.nodes.size(); ++j) {
        byListed(raw[j].children, raw[j].childCount, out.nodes[j].children);
        byListed(raw[j].emits, raw[j].emitCount, out.nodes[j].emits);
    }
    for (size_t j = 0; j < out.nodes.size() && out.root < 0; ++j)
        if (out.nodes[j].type == 0) out.root = int(j);
    if (out.root < 0) return fail("no root node");
    return true;
}

SparkEffect::SparkEffect(const SparkAsset* asset, uint32_t seed) : asset_(asset), rng_(seed ? seed : 1) {
    if (asset_ && asset_->root >= 0) roots_.push_back(make(asset_->root));
}

float SparkEffect::random(float a, float b) {
    rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
    return a + (b - a) * float(rng_ >> 8) / float(1u << 24);
}

namespace {

constexpr float kDeg = 3.14159265f / 180;

// y / z taking x's value (SparkNode::ProcessLink): `bit` for y, `bit + 1` for z
void linkXyz(uint32_t link, int bit, float* v) {
    if (link >> bit & 1) v[1] = v[0];
    if (link >> (bit + 1) & 1) v[2] = v[0];
}

// a translation, Euler turn (x, then y, then z - radians) and scale as a matrix (T R S)
Mat34 trs(const float* t, const float* r, const float* s) {
    const float cx = std::cos(r[0]), sx = std::sin(r[0]);
    const float cy = std::cos(r[1]), sy = std::sin(r[1]);
    const float cz = std::cos(r[2]), sz = std::sin(r[2]);
    const float m[9] = {cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz, //
                        cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz, //
                        -sy, sx * cy, cx * cy};
    Mat34 out;
    for (int row = 0; row < 3; ++row) {
        for (int c = 0; c < 3; ++c) out.m[row * 4 + c] = m[row * 3 + c] * s[c];
        out.m[row * 4 + 3] = t[row];
    }
    return out;
}

Mat34 translation(float x, float y, float z) {
    Mat34 m;
    m.m[3] = x, m.m[7] = y, m.m[11] = z;
    return m;
}

// SparkSprite::UpdatePivot's places by +0x263 (DAT_00f7bd7c / DAT_00f7bda0: 0 top left .. 8 bottom
// right, the quad moved so that corner or edge sits on the node; 4 the centre, not moved)
constexpr float kPivotX[9] = {0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, -0.5f};
constexpr float kPivotY[9] = {-0.5f, -0.5f, -0.5f, 0, 0, 0, 0.5f, 0.5f, 0.5f};

} // namespace

// SparkNode::OnActivated (its kinematics' random values, ProcessTRSRndLink's links), OnInit (its
// life, its children)
SparkEffect::Inst SparkEffect::make(int node) {
    const SparkNode& n = asset_->nodes[size_t(node)];
    Inst in;
    in.node = node;
    for (int a = 0; a < 3; ++a) {
        const size_t k = size_t(a);
        in.pos[a] = random(n.position[k]);
        in.rot[a] = random(n.rotation[k]);
        in.scale[a] = random(n.scale[k]);
        in.vel[a] = random(n.velocity[k]);
        in.accel[a] = random(n.accel[k]);
        in.spin[a] = random(n.spin[k]);
        in.grow[a] = random(n.grow[k]);
        in.gpos[a] = random(n.globalPosition[k]);
        in.grot[a] = random(n.globalRotation[k]);
        in.gscale[a] = random(n.globalScale[k]);
    }
    linkXyz(n.link, 0, in.pos), linkXyz(n.link, 4, in.vel), linkXyz(n.link, 6, in.gpos), linkXyz(n.link, 8, in.rot);
    linkXyz(n.link, 12, in.spin), linkXyz(n.link, 14, in.grot), linkXyz(n.link, 16, in.scale), linkXyz(n.link, 20, in.grow);
    linkXyz(n.link, 22, in.gscale);
    if (n.lifeMax >= 0) {
        const int frames = int(random(float(n.lifeMin), float(n.lifeMax) + 0.999f));
        in.life = float(frames) / asset_->fps;
        const int base = asset_->centeredRandom ? n.lifeMin + (n.lifeMax - n.lifeMin + 1) / 2 : n.lifeMin;
        if (base > 0 && n.lifeMax > 0 && frames > 0) in.lifeScale = float(base) / float(frames); // (SparkNode::OnInit +0x474)
    }
    if (n.type == 3) { // (SparkSprite::OnActivated: the size's and the pivot's links, +0x268 bits 0 / 1 and 2 / 3)
        in.size[0] = random(n.sizeX), in.size[1] = n.flags & 1 ? in.size[0] : random(n.sizeY);
        in.pivot[0] = random(n.pivotX), in.pivot[1] = n.flags >> 2 & 1 ? in.pivot[0] : random(n.pivotY);
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k) in.color[c][k] = random(n.color[size_t(c)][size_t(k)]);
        if (n.flags >> 16 & 1) // (+0x26a bit 0: one colour - corner 0's)
            for (int c = 1; c < 4; ++c)
                for (int k = 0; k < 4; ++k) in.color[c][k] = in.color[0][k];
        if (const SparkMaterial* m = asset_->materialById(n.material))
            for (int k = 0; k < 4; ++k) in.matColor[k] = random(m->color[size_t(k)]);
    }
    if (n.type == 2) {
        in.spawned.resize(n.emits.size());
        in.interval = int(random(float(n.intervalMin), float(n.intervalMax) + 0.999f));
    }
    for (const int c : n.children) in.children.push_back(make(c));
    return in;
}

// an emitter's new nodes (SparkEmitter::ControlParticles): `count` of its kinds in turn (or at
// random), each where its shape puts it (SparkUtils::GetGenPos: a box - each axis -e .. +e; +0x2a0)
void SparkEffect::emit(Inst& in, int count) {
    const SparkNode& n = asset_->nodes[size_t(in.node)];
    if (n.emits.empty()) return;
    for (int k = 0; k < count; ++k) {
        size_t kind = n.order == 1 ? size_t(random(0, float(n.emits.size()) - 0.001f)) : size_t(in.next++) % n.emits.size();
        std::vector<Inst>& group = in.spawned[kind];
        if (n.maxParticles > 0 && int(group.size()) >= n.maxParticles) continue;
        Inst p = make(n.emits[kind]);
        if (n.shape == 1)
            for (int a = 0; a < 3; ++a) {
                const float e = std::fabs(random(n.extent[size_t(a)]));
                p.gen[a] = random(-e, e);
            }
        group.push_back(std::move(p));
    }
}

// past its life (SparkNode::Update: +0x172 1 waits for its particles and children - CheckCompleted)
bool SparkEffect::done(const Inst& in) const {
    if (in.life < 0 || in.age < in.life) return false;
    if (asset_->nodes[size_t(in.node)].endMode != 1) return true;
    if (!in.children.empty()) return false;
    for (const std::vector<Inst>& group : in.spawned)
        if (!group.empty()) return false;
    return true;
}

void SparkEffect::stepInst(Inst& in, float dt) {
    const SparkNode& n = asset_->nodes[size_t(in.node)];
    const int before = int(std::floor(in.age * asset_->fps + 1e-4f));
    in.age += dt;
    // SparkNode::UpdateKineLocalPos / Rot / Scl: at the node's frame, each rate its curve's value
    // (else its least) plus its random part, its curves' links (the random parts' were made at
    // the start); acceleration into velocity, then velocity, turning and scale's change into the
    // place, a step at a time
    const int f = frame(in);
    auto rate = [&](int channel, const std::array<SparkRange, 3>& r, const float* value, int bit, float* out) {
        for (int a = 0; a < 3; ++a) {
            const float base = asset_->rangeBase(r[size_t(a)]);
            out[a] = asset_->animate(n.anim, channel + a, f, base) - base;
        }
        if (bit >= 0) linkXyz(n.link, bit, out);
        for (int a = 0; a < 3; ++a) out[a] += value[a];
    };
    float accel[3], vel[3], spin[3], grow[3];
    rate(0x13, n.accel, in.accel, -1, accel);
    for (int a = 0; a < 3; ++a) in.accVel[a] += accel[a] * dt;
    rate(10, n.velocity, in.vel, 4, vel);
    rate(13, n.spin, in.spin, 12, spin);
    rate(16, n.grow, in.grow, 20, grow);
    for (int a = 0; a < 3; ++a) {
        in.move[a] += (vel[a] + in.accVel[a]) * dt;
        in.turn[a] += spin[a] * dt;
        in.growth[a] += std::max(grow[a], 0.0f) * dt; // (SparkUtils::LowerBounds(.., 0))
    }
    const bool ending = in.life >= 0 && in.age >= in.life; // (+0x34 bit 2: no more particles)
    if (n.type == 2 && !ending) {
        // the frames this step began (an emitter's work runs once a frame: SparkEmitter::Update)
        const int now = int(std::floor(in.age * asset_->fps + 1e-4f));
        for (int fr = in.age - dt <= 0 ? 0 : before + 1; fr <= now; ++fr) {
            if (asset_->centeredRandom && n.mode == 2) {
                // Vita's ControlParticles uses the one-based frame to compute a cumulative count.
                const int target = std::max(n.countMin * (fr + 1) / in.interval, 1);
                if (target > in.emitted) emit(in, target - in.emitted), in.emitted = target;
            } else if (n.mode == 2) {
                if (fr >= in.lastFrame) {
                    emit(in, int(random(float(n.countMin), float(n.countMax) + 0.999f)));
                    in.lastFrame = fr + in.interval;
                    in.interval = int(random(float(n.intervalMin), float(n.intervalMax) + 0.999f));
                }
            } else if (n.mode == 1) {
                if (in.emitted == 0) emit(in, n.countMin), in.emitted = 1;
            } else if (n.mode == 0) {
                const int target = int(float(fr) / asset_->fps * float(n.countMin)) + 1;
                if (target > in.emitted) emit(in, target - in.emitted), in.emitted = target;
            }
        }
    }
    auto gone = [this](const Inst& p) { return done(p); };
    for (std::vector<Inst>& group : in.spawned) {
        for (Inst& p : group) stepInst(p, dt);
        group.erase(std::remove_if(group.begin(), group.end(), gone), group.end());
    }
    for (Inst& c : in.children) stepInst(c, dt);
    in.children.erase(std::remove_if(in.children.begin(), in.children.end(), gone), in.children.end());
}

void SparkEffect::step(float dt) {
    if (dt <= 0) return;
    age_ += dt;
    for (Inst& r : roots_) stepInst(r, dt);
}

// a node's frame for its curves (SparkNode::Update +0x478: its age's frames, scaled to its life's
// shortest, + 1)
int SparkEffect::frame(const Inst& in) const {
    return int(in.age * asset_->fps * in.lifeScale + 0.001f) + 1;
}

// a node's place in its parent's (SparkNode::UpdateKineLocal* / UpdateKineGlobal / UpdateMatrix):
// the global transform (+0x120 / +0x138 / +0x150 and channels 0x1c .. 0x24) times the local one -
// each its curve's value (channels 1..3 position, 4..6 rotation, 7..9 scale) - else its least -,
// its random part and what velocity, turning and scale's change have added. Turned x, then y, then
// z; a billboard's local turn is left out (+0x34 bit 4: DrawSprite rolls it instead)
Mat34 SparkEffect::local(const Inst& in) const {
    const SparkNode& n = asset_->nodes[size_t(in.node)];
    const int f = frame(in);
    auto value = [&](int ch, const std::array<SparkRange, 3>& r, const float* random, int bit, float* out) {
        for (int a = 0; a < 3; ++a) {
            const float base = asset_->rangeBase(r[size_t(a)]);
            out[a] = asset_->animate(n.anim, ch + a, f, base) - base;
        }
        linkXyz(n.link, bit, out);
        for (int a = 0; a < 3; ++a) out[a] += random[a];
    };
    float pos[3], rot[3], scale[3], gpos[3], grot[3], gscale[3];
    value(1, n.position, in.pos, 0, pos), value(4, n.rotation, in.rot, 8, rot), value(7, n.scale, in.scale, 16, scale);
    value(0x1c, n.globalPosition, in.gpos, 6, gpos), value(0x1f, n.globalRotation, in.grot, 14, grot);
    value(0x22, n.globalScale, in.gscale, 22, gscale);
    const bool billboard = n.type == 3 && (n.flags >> 18 & 3) != 0;
    for (int a = 0; a < 3; ++a) {
        pos[a] += in.move[a];
        rot[a] = billboard ? 0 : (rot[a] + in.turn[a]) * kDeg;
        scale[a] = std::max(scale[a] + in.growth[a], 0.0f);
        grot[a] *= kDeg;
    }
    return trs(gpos, grot, gscale) * trs(pos, rot, scale);
}

// SparkSprite::DrawSprite: the quad (BuildBox: +-0.5) moved by its pivot and scaled by its size in
// the node's place, or (+0x268 bit 18) turned to the camera at the node's position, rolled by its
// turn about z, its scale kept. Where the node is: SparkNode::UpdateMatrix by +0x16c - following
// its parent, the parent's place, then where its emitter's shape put it, then its own; not
// following, the root and the parent's position (with that shape's offset) it was made under, never
// moved after, then its own (the original's partial modes keep only the position rule here)
void SparkEffect::collect(const Inst& in, const Mat34& root, const Mat34& parent, const Mat34& camera, std::vector<Quad>& out) const {
    const SparkNode& n = asset_->nodes[size_t(in.node)];
    const uint32_t follow = n.follow & 0x70;
    Mat34 placed; // (in the effect's space - +0x3f0)
    const Mat34* nodeRoot = &root;
    if (follow == 0x70) {
        placed = parent * translation(in.gen[0], in.gen[1], in.gen[2]) * local(in);
    } else if (follow & 0x10) {
        placed = translation(parent.m[3] + in.gen[0], parent.m[7] + in.gen[1], parent.m[11] + in.gen[2]) * local(in);
    } else if (follow) {
        placed = local(in);
    } else {
        if (!in.anchored) {
            in.anchored = true, in.anchorRoot = root;
            in.anchorPos[0] = parent.m[3] + in.gen[0], in.anchorPos[1] = parent.m[7] + in.gen[1], in.anchorPos[2] = parent.m[11] + in.gen[2];
        }
        placed = translation(in.anchorPos[0], in.anchorPos[1], in.anchorPos[2]) * local(in);
        nodeRoot = &in.anchorRoot;
    }
    const Mat34 w = *nodeRoot * placed;
    if (n.type == 3 && n.quadShape == 0) {
        Quad q;
        q.material = asset_->materialById(n.material);
        const int f = frame(in);
        float ax[3], ay[3];
        if (n.flags >> 18 & 1) {
            float sx = 0, sy = 0;
            for (int r = 0; r < 3; ++r) sx += w.m[r * 4] * w.m[r * 4], sy += w.m[r * 4 + 1] * w.m[r * 4 + 1];
            sx = std::sqrt(sx), sy = std::sqrt(sy);
            // (its roll: the turn's z - degrees, passed to sincosf as they are, as the original does)
            float rc[3];
            for (int a = 0; a < 3; ++a) {
                const float base = asset_->rangeBase(n.rotation[size_t(a)]);
                rc[a] = asset_->animate(n.anim, 4 + a, f, base) - base;
            }
            linkXyz(n.link, 8, rc);
            const float roll = rc[2] + in.rot[2] + in.turn[2];
            const float c = std::cos(roll), s = std::sin(roll);
            for (int r = 0; r < 3; ++r) {
                ax[r] = (camera.m[r * 4] * c + camera.m[r * 4 + 1] * s) * sx;
                ay[r] = (camera.m[r * 4 + 1] * c - camera.m[r * 4] * s) * sy;
            }
        } else {
            for (int r = 0; r < 3; ++r) ax[r] = w.m[r * 4], ay[r] = w.m[r * 4 + 1];
        }
        // (SparkSprite::UpdateSize: channels 0x2d / 0x2e, the material's +0x170 bit 11 square;
        // UpdatePivot: 0x30 / 0x31 plus the pivot's place; UpdateColor: 0x6f + corner x 4 + r g b a,
        // +0x26a bit 0 corner 0's for all four; SparkMaterial::Update: 0x35 .. 0x38 - each at the
        // sprite's frame)
        const float sizeX = asset_->rangeBase(n.sizeX), sizeY = asset_->rangeBase(n.sizeY);
        float size[2] = {in.size[0] + asset_->animate(n.anim, 0x2d, f, sizeX) - sizeX,
                         in.size[1] + asset_->animate(n.anim, 0x2e, f, sizeY) - sizeY};
        if (n.flags & 1) size[1] = in.size[1] + asset_->animate(n.anim, 0x2d, f, sizeX) - sizeX;
        if (q.material && q.material->square) size[1] = size[0];
        const float pivotX = asset_->rangeBase(n.pivotX), pivotY = asset_->rangeBase(n.pivotY);
        float pivot[2] = {in.pivot[0] + asset_->animate(n.anim, 0x30, f, pivotX) - pivotX,
                          in.pivot[1] + asset_->animate(n.anim, 0x31, f, pivotY) - pivotY};
        if (n.flags >> 2 & 1) pivot[1] = in.pivot[1] + asset_->animate(n.anim, 0x30, f, pivotX) - pivotX;
        if (n.pivot >= 0 && n.pivot < 9) pivot[0] += kPivotX[n.pivot], pivot[1] += kPivotY[n.pivot];
        float mat[4] = {1, 1, 1, 1};
        if (q.material)
            for (int k = 0; k < 4; ++k) {
                const float lo = asset_->rangeBase(q.material->color[size_t(k)]);
                mat[k] = std::clamp(in.matColor[k] + asset_->animate(q.material->anim, 0x35 + k, f, lo) - lo, 0.0f, 1.0f);
            }
        const bool oneColor = n.flags >> 16 & 1;
        static const float kCorner[4][2] = {{-0.5f, 0.5f}, {0.5f, 0.5f}, {-0.5f, -0.5f}, {0.5f, -0.5f}};
        for (int c = 0; c < 4; ++c) {
            const float cx = (kCorner[c][0] + pivot[0]) * size[0], cy = (kCorner[c][1] + pivot[1]) * size[1];
            for (int r = 0; r < 3; ++r) q.corner[c][r] = w.m[r * 4 + 3] + ax[r] * cx + ay[r] * cy;
            const int from = oneColor ? 0 : c;
            for (int k = 0; k < 4; ++k) {
                float v = 1;
                if (q.material && q.material->spriteColor) {
                    const float lo = asset_->rangeBase(n.color[size_t(from)][size_t(k)]);
                    v = std::clamp(in.color[from][k] + asset_->animate(n.anim, 0x6f + from * 4 + k, f, lo) - lo, 0.0f, 1.0f);
                }
                q.color[c][k] = v * mat[k];
            }
        }
        out.push_back(q);
    }
    for (const std::vector<Inst>& group : in.spawned)
        for (const Inst& p : group) collect(p, root, placed, camera, out);
    for (const Inst& c : in.children) collect(c, root, placed, camera, out);
}

void SparkEffect::quads(const Mat34& root, const Mat34& camera, std::vector<Quad>& out) const {
    for (const Inst& r : roots_) collect(r, root, Mat34(), camera, out);
}

size_t SparkEffect::count(const Inst& in) {
    size_t k = 1;
    for (const std::vector<Inst>& group : in.spawned)
        for (const Inst& p : group) k += count(p);
    for (const Inst& c : in.children) k += count(c);
    return k;
}

size_t SparkEffect::alive() const {
    size_t k = 0;
    for (const Inst& r : roots_) k += count(r);
    return k;
}

} // namespace cr3
