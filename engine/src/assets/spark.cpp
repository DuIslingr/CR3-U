#include "assets/spark.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace cr3 {

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
    auto range = [&](size_t p) { return SparkRange{f32(p), f32(p + 4)}; };
    auto text = [&](size_t p, size_t n) {
        std::string s;
        for (size_t k = 0; k < n && p + k < d.size() && d[p + k]; ++k) s += char(d[p + k]);
        return s;
    };
    struct Raw {
        size_t at;
        uint32_t children, childCount, emits, emitCount;
    };
    std::vector<Raw> raw;
    std::map<uint32_t, std::string> textures; // (texture nodes: id -> image)
    std::vector<uint32_t> list;               // NLST: the ids the nodes' lists index
    for (size_t o = 0x18; o + 8 <= d.size();) {
        const uint32_t size = u32(o + 4);
        if (size < 8 || o + size > d.size()) return fail("bad chunk");
        if (std::memcmp(&d[o], "NODE", 4) == 0) {
            const size_t nd = o + 0x10; // (the node's data: type, size, id, name ...)
            const int type = i16(nd);
            if (type == 9) {
                textures[u32(nd + 4)] = text(nd + 0x48, 0x20);
            } else if (type == 10) {
                SparkMaterial m;
                m.id = u32(nd + 4), m.name = text(nd + 8, 0x40);
                for (int c = 0; c < 4; ++c) m.color[size_t(c)] = range(nd + 0x48 + size_t(c) * 8);
                const uint32_t f = u32(nd + 0x170);
                m.blend = f >> 15 & 1, m.depthTest = f >> 17 & 1, m.depthWrite = f >> 18 & 1, m.alphaTest = f >> 19 & 1;
                m.alphaRef = f32(nd + 0x180), m.blendMode = d[nd + 0x186];
                m.shaderKey = text(nd + 0x188, 0x40);
                m.texture = std::to_string(u32(nd + 0xe0)); // (an id until the textures are known)
                out.materials.push_back(m);
            } else {
                SparkNode n;
                n.type = type, n.id = u32(nd + 4), n.name = text(nd + 8, 0x40);
                for (int a = 0; a < 3; ++a) {
                    n.position[size_t(a)] = range(nd + 0x48 + size_t(a) * 8);
                    n.rotation[size_t(a)] = range(nd + 0x60 + size_t(a) * 8);
                    n.scale[size_t(a)] = range(nd + 0x78 + size_t(a) * 8);
                    n.velocity[size_t(a)] = range(nd + 0xd8 + size_t(a) * 8);
                }
                n.link = u32(nd + 0x168);
                n.lifeMin = i16(nd + 0x178), n.lifeMax = i16(nd + 0x17a);
                Raw r{nd, u32(nd + 0x188), u32(nd + 0x190), 0, 0};
                if (type == 2) {
                    n.order = d[nd + 0x1a8], n.mode = d[nd + 0x1a9];
                    n.countMin = i16(nd + 0x1ac), n.countMax = i16(nd + 0x1ae);
                    n.intervalMin = std::max(i16(nd + 0x1b0), 1), n.intervalMax = std::max(i16(nd + 0x1b2), 1);
                    r.emits = u32(nd + 0x1b8), r.emitCount = u32(nd + 0x1c0);
                    n.shape = d[nd + 0x1c4];
                    for (int a = 0; a < 3; ++a) n.extent[size_t(a)] = range(nd + 0x1c8 + size_t(a) * 8);
                    n.maxParticles = int(uint16_t(i16(nd + 0x1e0)));
                } else if (type == 3) {
                    n.sizeX = range(nd + 0x1a8), n.sizeY = range(nd + 0x1b0);
                    for (int c = 0; c < 4; ++c)
                        for (int k = 0; k < 4; ++k) n.color[size_t(c)][size_t(k)] = range(nd + 0x1d8 + size_t(c) * 0x20 + size_t(k) * 8);
                    n.material = u32(nd + 0x258);
                    n.pivot = d[nd + 0x263];
                    n.flags = u32(nd + 0x268);
                    n.quadShape = d[nd + 0x274];
                }
                out.nodes.push_back(n);
                raw.push_back(r);
            }
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

// SparkNode::OnActivated (its kinematics' random values, ProcessTRSRndLink's links), OnInit (its
// life, its children)
SparkEffect::Inst SparkEffect::make(int node) {
    const SparkNode& n = asset_->nodes[size_t(node)];
    Inst in;
    in.node = node;
    for (int a = 0; a < 3; ++a) {
        in.pos[a] = random(n.position[size_t(a)]);
        in.rot[a] = random(n.rotation[size_t(a)]) * 3.14159265f / 180;
        in.scale[a] = random(n.scale[size_t(a)]);
        in.vel[a] = random(n.velocity[size_t(a)]);
    }
    auto link = [&](float* v, int bit) { // (bit: y = x, bit + 1: z = x)
        if (n.link >> bit & 1) v[1] = v[0];
        if (n.link >> (bit + 1) & 1) v[2] = v[0];
    };
    link(in.pos, 0), link(in.vel, 4), link(in.rot, 8), link(in.scale, 16);
    if (n.lifeMax >= 0) in.life = float(int(random(float(n.lifeMin), float(n.lifeMax) + 0.999f))) / asset_->fps;
    if (n.type == 3) {
        in.size[0] = random(n.sizeX), in.size[1] = random(n.sizeY);
        for (int k = 0; k < 4; ++k) in.color[k] = std::clamp(random(n.color[0][size_t(k)]), 0.0f, 1.0f);
    }
    if (n.type == 2) {
        in.spawned.resize(n.emits.size());
        in.interval = int(random(float(n.intervalMin), float(n.intervalMax) + 0.999f));
    }
    for (const int c : n.children) in.children.push_back(make(c));
    return in;
}

// an emitter's new nodes (SparkEmitter::UpdateWork): `count` of its kinds in turn (or at random),
// each where its shape puts it (SparkUtils::GetGenPos: a box - each axis -e .. +e)
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
                p.pos[a] += random(-e, e);
            }
        group.push_back(std::move(p));
    }
}

void SparkEffect::stepInst(Inst& in, float dt) {
    const SparkNode& n = asset_->nodes[size_t(in.node)];
    const int before = int(std::floor(in.age * asset_->fps + 1e-4f));
    in.age += dt;
    if (n.type == 2) {
        // the frames this step began (an emitter's work runs once a frame: SparkEmitter::Update)
        const int now = int(std::floor(in.age * asset_->fps + 1e-4f));
        for (int f = in.age - dt <= 0 ? 0 : before + 1; f <= now; ++f) {
            if (n.mode == 2) {
                if (f >= in.lastFrame) {
                    emit(in, int(random(float(n.countMin), float(n.countMax) + 0.999f)));
                    in.lastFrame = f + in.interval;
                    in.interval = int(random(float(n.intervalMin), float(n.intervalMax) + 0.999f));
                }
            } else if (n.mode == 1) {
                if (in.emitted == 0) emit(in, n.countMin), in.emitted = 1;
            } else if (n.mode == 0) {
                const int target = int(float(f) / asset_->fps * float(n.countMin)) + 1;
                if (target > in.emitted) emit(in, target - in.emitted), in.emitted = target;
            }
        }
        for (std::vector<Inst>& group : in.spawned) {
            for (Inst& p : group) stepInst(p, dt);
            group.erase(std::remove_if(group.begin(), group.end(), [](const Inst& p) { return p.life >= 0 && p.age >= p.life; }),
                        group.end());
        }
    }
    for (Inst& c : in.children) stepInst(c, dt);
    in.children.erase(std::remove_if(in.children.begin(), in.children.end(), [](const Inst& p) { return p.life >= 0 && p.age >= p.life; }),
                      in.children.end());
}

void SparkEffect::step(float dt) {
    if (dt <= 0) return;
    age_ += dt;
    for (Inst& r : roots_) stepInst(r, dt);
}

// a node's place in its parent's: moved by its velocity, turned (x, then y, then z), scaled
Mat34 SparkEffect::local(const Inst& in) const {
    const float cx = std::cos(in.rot[0]), sx = std::sin(in.rot[0]);
    const float cy = std::cos(in.rot[1]), sy = std::sin(in.rot[1]);
    const float cz = std::cos(in.rot[2]), sz = std::sin(in.rot[2]);
    const float r[9] = {cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz, //
                        cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz, //
                        -sy, sx * cy, cx * cy};
    Mat34 m;
    for (int row = 0; row < 3; ++row) {
        for (int c = 0; c < 3; ++c) m.m[row * 4 + c] = r[row * 3 + c] * in.scale[c];
        m.m[row * 4 + 3] = in.pos[row] + in.vel[row] * in.age;
    }
    return m;
}

// SparkSprite::DrawSprite: the quad (BuildBox: +-0.5) scaled by its size in the node's place, or
// (+0x268 bit 18) turned to the camera at the node's position, its scale kept
void SparkEffect::collect(const Inst& in, const Mat34& parent, const Mat34& camera, std::vector<Quad>& out) const {
    const SparkNode& n = asset_->nodes[size_t(in.node)];
    const Mat34 w = parent * local(in);
    if (n.type == 3 && n.quadShape == 0) {
        Quad q;
        q.material = asset_->materialById(n.material);
        float ax[3], ay[3];
        if (n.flags >> 18 & 1) {
            float sx = 0, sy = 0;
            for (int r = 0; r < 3; ++r) sx += w.m[r * 4] * w.m[r * 4], sy += w.m[r * 4 + 1] * w.m[r * 4 + 1];
            sx = std::sqrt(sx), sy = std::sqrt(sy);
            for (int r = 0; r < 3; ++r) ax[r] = camera.m[r * 4] * sx, ay[r] = camera.m[r * 4 + 1] * sy;
        } else {
            for (int r = 0; r < 3; ++r) ax[r] = w.m[r * 4], ay[r] = w.m[r * 4 + 1];
        }
        static const float kCorner[4][2] = {{-0.5f, 0.5f}, {0.5f, 0.5f}, {-0.5f, -0.5f}, {0.5f, -0.5f}};
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 3; ++r)
                q.corner[c][r] = w.m[r * 4 + 3] + ax[r] * kCorner[c][0] * in.size[0] + ay[r] * kCorner[c][1] * in.size[1];
        for (int k = 0; k < 4; ++k) q.color[k] = in.color[k] * (q.material ? q.material->color[size_t(k)].min : 1.0f);
        out.push_back(q);
    }
    for (const std::vector<Inst>& group : in.spawned)
        for (const Inst& p : group) collect(p, w, camera, out);
    for (const Inst& c : in.children) collect(c, w, camera, out);
}

void SparkEffect::quads(const Mat34& root, const Mat34& camera, std::vector<Quad>& out) const {
    for (const Inst& r : roots_) collect(r, root, camera, out);
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
