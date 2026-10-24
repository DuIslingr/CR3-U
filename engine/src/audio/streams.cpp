// The iPad's music (AAC in CAF, FAAD2) and the Vita's (ATRAC9 in SXD, LibAtrac9) as MusicStreams
// (audio/sounddata.hpp; docs/FORMATS.md, "Sound")

#include "audio/sounddata.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef CR3_HAVE_FAAD2
#include <neaacdec.h>
#endif
#ifdef CR3_HAVE_ATRAC9
#include <libatrac9.h>
#endif

#include "hevag_coefs.h"

namespace cr3 {

namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool fail(std::string* error, const char* why) {
    if (error) *error = why;
    return false;
}

// an MPEG-4 descriptor's length: up to four 7-bit groups
size_t descLen(const uint8_t* p, const uint8_t* end, const uint8_t*& next) {
    size_t n = 0;
    for (int i = 0; i < 4 && p < end; ++i) {
        n = n << 7 | (*p & 0x7f);
        if (!(*p++ & 0x80)) break;
    }
    next = p;
    return n;
}

// the DecoderSpecificInfo (tag 5) inside an ESDS cookie
bool findAsc(const uint8_t* p, const uint8_t* end, std::vector<uint8_t>& out) {
    while (p + 2 <= end) {
        const uint8_t tag = *p++;
        const uint8_t* body;
        const size_t len = descLen(p, end, body);
        if (body + len > end) return false;
        if (tag == 5) return out.assign(body, body + len), true;
        if (tag == 3 && len >= 3) { // (ES_Descriptor: ES_ID, flags, its optional fields, then sub-descriptors)
            const uint8_t flags = body[2];
            const uint8_t* q = body + 3;
            if (flags & 0x80) q += 2;
            if (flags & 0x40) q += 1 + *q;
            if (flags & 0x20) q += 2;
            if (findAsc(q, body + len, out)) return true;
        } else if (tag == 4 && len >= 13) { // (DecoderConfigDescriptor: 13 bytes, then sub-descriptors)
            if (findAsc(body + 13, body + len, out)) return true;
        }
        p = body + len;
    }
    return false;
}

#ifdef CR3_HAVE_FAAD2
// AAC, a packet at a time: frames counted from the stream's start (the priming included), handed
// out from `priming` to `priming + validFrames`
class AacStream : public MusicStream {
public:
    ~AacStream() override {
        if (dec_) NeAACDecClose(dec_);
    }
    bool open(std::vector<uint8_t> file, std::string* error) {
        if (!parseCafAac(file, caf_, error)) return false;
        if (!reset()) return fail(error, "FAAD2 doesn't take its config");
        return seek(0);
    }
    bool open(CafAac packets, std::string* error) {
        caf_ = std::move(packets);
        if (!reset()) return fail(error, "FAAD2 doesn't take its config");
        return seek(0);
    }
    int rate() const override { return caf_.rate; }
    int channels() const override { return caf_.channels; }
    uint32_t frames() const override { return uint32_t(caf_.validFrames); }
    bool seek(uint32_t frame) override {
        // (a packet's output overlaps the one before: the decoder primed with it, its output dropped)
        const int64_t raw = int64_t(frame) + caf_.priming;
        const size_t packet = size_t(raw / caf_.framesPerPacket);
        if (!reset()) return false;
        if (packet > 0) decode(packet - 1);
        next_ = packet, pos_ = int64_t(packet) * caf_.framesPerPacket, buf_.clear(), bufAt_ = 0;
        skip_ = raw - pos_;
        return true;
    }
    int read(int16_t* out, int frames) override {
        const int ch = caf_.channels;
        const int64_t end = int64_t(caf_.priming) + caf_.validFrames;
        int done = 0;
        while (done < frames) {
            if (bufAt_ >= buf_.size() / size_t(ch)) {
                if (pos_ >= end || next_ >= caf_.size.size()) break;
                buf_ = decode(next_++);
                bufAt_ = 0;
                if (buf_.empty()) continue; // (FAAD2's first packet gives nothing: on to the next)
                const size_t drop = size_t(std::min<int64_t>(skip_, int64_t(buf_.size() / size_t(ch))));
                bufAt_ = drop, skip_ -= int64_t(drop), pos_ += int64_t(drop);
                continue;
            }
            const int64_t left = std::min<int64_t>(int64_t(buf_.size() / size_t(ch) - bufAt_), end - pos_);
            const int n = int(std::min<int64_t>(left, frames - done));
            if (n <= 0) break;
            std::memcpy(out + size_t(done) * size_t(ch), &buf_[bufAt_ * size_t(ch)], size_t(n) * size_t(ch) * 2);
            done += n, bufAt_ += size_t(n), pos_ += n;
        }
        return done;
    }

private:
    bool reset() {
        if (dec_) NeAACDecClose(dec_);
        dec_ = NeAACDecOpen();
        NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(dec_);
        cfg->outputFormat = FAAD_FMT_16BIT;
        NeAACDecSetConfiguration(dec_, cfg);
        unsigned long rate = 0;
        unsigned char ch = 0;
        return NeAACDecInit2(dec_, caf_.config.data(), (unsigned long)caf_.config.size(), &rate, &ch) >= 0;
    }
    std::vector<int16_t> decode(size_t i) {
        std::vector<int16_t> pcm;
        if (!dec_ || i >= caf_.size.size()) return pcm;
        NeAACDecFrameInfo info;
        void* p = NeAACDecDecode(dec_, &info, &caf_.data[caf_.offset[i]], caf_.size[i]);
        if (info.error || !p || info.channels != caf_.channels) return std::vector<int16_t>(size_t(caf_.framesPerPacket) * size_t(caf_.channels), 0);
        const int16_t* s = static_cast<const int16_t*>(p);
        pcm.assign(s, s + info.samples);
        return pcm;
    }
    CafAac caf_;
    NeAACDecHandle dec_ = nullptr;
    size_t next_ = 0, bufAt_ = 0; // (the next packet; the buffer's next frame)
    int64_t pos_ = 0, skip_ = 0;  // (the next frame out, from the stream's start; frames to drop)
    std::vector<int16_t> buf_;
};
#endif

#ifdef CR3_HAVE_ATRAC9
// ATRAC9, a superframe at a time (frames overlap: a seek starts a superframe early)
class At9Stream : public MusicStream {
public:
    ~At9Stream() override {
        if (h_) Atrac9ReleaseHandle(h_);
    }
    bool open(std::vector<uint8_t> file, std::string* error) { return open(file, file, error); }
    // `header`'s first wave, its stream in `data` - the file itself, or (a streamed wave: CR3's
    // .snd1 over its .snd2) the other
    bool open(const std::vector<uint8_t>& header, std::vector<uint8_t> data, std::string* error) {
        std::vector<SxdWave> waves;
        if (!parseSxdWaves(header, waves, error)) return false;
        if (waves.empty()) return fail(error, "no wave");
        file_ = std::move(data);
        w_ = waves[0];
        if (w_.at + w_.size > file_.size()) return fail(error, "the wave's stream past its file's end");
        if (!reset()) return fail(error, "LibAtrac9 doesn't take its config");
        loopStart = w_.loopStart, loopEnd = w_.loopEnd;
        return seek(0);
    }
    int rate() const override { return w_.rate; }
    int channels() const override { return w_.channels; }
    uint32_t frames() const override { return uint32_t(w_.frames); }
    bool seek(uint32_t frame) override {
        if (!reset()) return false;
        const size_t sf = size_t(frame / uint32_t(sfSamples_));
        if (sf > 0) decode(sf - 1);
        next_ = sf, pos_ = int64_t(sf) * sfSamples_, buf_.clear(), bufAt_ = 0, skip_ = int64_t(frame) - pos_;
        return true;
    }
    int read(int16_t* out, int frames) override {
        const int ch = w_.channels;
        int done = 0;
        while (done < frames) {
            if (bufAt_ >= buf_.size() / size_t(ch)) {
                if (pos_ >= w_.frames || !decodeNext()) break;
                const size_t drop = size_t(std::min<int64_t>(skip_, sfSamples_));
                bufAt_ = drop, skip_ -= int64_t(drop), pos_ += int64_t(drop);
                continue;
            }
            const int64_t left = std::min<int64_t>(int64_t(buf_.size() / size_t(ch) - bufAt_), w_.frames - pos_);
            const int n = int(std::min<int64_t>(left, frames - done));
            if (n <= 0) break;
            std::memcpy(out + size_t(done) * size_t(ch), &buf_[bufAt_ * size_t(ch)], size_t(n) * size_t(ch) * 2);
            done += n, bufAt_ += size_t(n), pos_ += n;
        }
        return done;
    }

private:
    bool reset() {
        if (h_) Atrac9ReleaseHandle(h_);
        h_ = Atrac9GetHandle();
        unsigned char config[4];
        std::memcpy(config, w_.config, 4);
        Atrac9CodecInfo info;
        if (!h_ || Atrac9InitDecoder(h_, config) != 0 || Atrac9GetCodecInfo(h_, &info) != 0 || info.channels != w_.channels) return false;
        frames_ = info.framesInSuperframe, frameSamples_ = info.frameSamples, sfSamples_ = frames_ * frameSamples_;
        sfBytes_ = info.superframeSize;
        return sfSamples_ > 0 && sfBytes_ > 0;
    }
    bool decode(size_t i) {
        if (size_t(i + 1) * size_t(sfBytes_) > w_.size) return false;
        buf_.resize(size_t(sfSamples_) * size_t(w_.channels));
        const uint8_t* p = &file_[w_.at + i * size_t(sfBytes_)];
        for (int k = 0; k < frames_; ++k) {
            int used = 0;
            if (Atrac9Decode(h_, p, buf_.data() + size_t(k) * size_t(frameSamples_) * size_t(w_.channels), &used) != 0 || used <= 0) return false;
            p += used;
        }
        return true;
    }
    bool decodeNext() {
        if (!decode(next_)) return false;
        ++next_, bufAt_ = 0;
        return true;
    }
    std::vector<uint8_t> file_;
    SxdWave w_;
    void* h_ = nullptr;
    int frames_ = 0, frameSamples_ = 0, sfSamples_ = 0, sfBytes_ = 0;
    size_t next_ = 0, bufAt_ = 0;
    int64_t pos_ = 0, skip_ = 0;
    std::vector<int16_t> buf_;
};
#endif

} // namespace

bool parseCafAac(const std::vector<uint8_t>& file, CafAac& out, std::string* error) {
    const uint8_t* f = file.data();
    const size_t size = file.size();
    if (size < 8 || std::memcmp(f, "caff", 4) != 0) return fail(error, "not a CAF file");
    out = CafAac();
    std::vector<uint64_t> sizes;
    bool desc = false, data = false;
    for (size_t p = 8; p + 12 <= size;) {
        const uint64_t len = be64(f + p + 4);
        const uint8_t* c = f + p + 12;
        if (len > size - p - 12) return fail(error, "a chunk past the end");
        if (std::memcmp(f + p, "desc", 4) == 0 && len >= 32) {
            if (std::memcmp(c + 8, "aac ", 4) != 0) return fail(error, "not AAC");
            const uint64_t rb = be64(c);
            double rate;
            std::memcpy(&rate, &rb, 8);
            out.rate = int(rate + 0.5), out.framesPerPacket = int(be32(c + 20)), out.channels = int(be32(c + 24));
            desc = true;
        } else if (std::memcmp(f + p, "kuki", 4) == 0) {
            if (!findAsc(c, c + len, out.config) && len <= 5) out.config.assign(c, c + len); // (a bare config)
        } else if (std::memcmp(f + p, "pakt", 4) == 0 && len >= 24) {
            const uint64_t n = be64(c);
            out.validFrames = int64_t(be64(c + 8)), out.priming = int(be32(c + 16)), out.remainder = int(be32(c + 20));
            const uint8_t* q = c + 24;
            for (uint64_t i = 0; i < n && q < c + len; ++i) { // (each a big-endian 7-bit run)
                uint64_t v = 0;
                while (q < c + len) {
                    const uint8_t b = *q++;
                    v = v << 7 | (b & 0x7f);
                    if (!(b & 0x80)) break;
                }
                sizes.push_back(v);
            }
        } else if (std::memcmp(f + p, "data", 4) == 0 && len >= 4) {
            out.data.assign(c + 4, c + len); // (its edit count first)
            data = true;
        }
        p += 12 + size_t(len);
    }
    if (!desc || !data || out.config.empty() || sizes.empty() || out.framesPerPacket <= 0) return fail(error, "no desc, kuki, pakt or data");
    uint64_t at = 0;
    for (uint64_t s : sizes) {
        if (at + s > out.data.size()) return fail(error, "the packets past the data");
        out.offset.push_back(uint32_t(at)), out.size.push_back(uint32_t(s));
        at += s;
    }
    return true;
}

std::unique_ptr<MusicStream> openAac(std::vector<uint8_t> file, std::string* error) {
#ifdef CR3_HAVE_FAAD2
    auto s = std::make_unique<AacStream>();
    if (!s->open(std::move(file), error)) return nullptr;
    return s;
#else
    (void)file;
    fail(error, "built without FAAD2");
    return nullptr;
#endif
}

bool parseSxdWaves(const std::vector<uint8_t>& file, std::vector<SxdWave>& out, std::string* error) {
    const uint8_t* f = file.data();
    const size_t size = file.size();
    if (size < 0x60 || std::memcmp(f, "SXDF", 4) != 0) return fail(error, "not an SXD file");
    out.clear();
    for (size_t p = 0x60; p + 8 <= size;) {
        if (std::memcmp(f + p, "SXDS", 4) == 0) break; // (CR3's: the streams' own block, not a chunk)
        const uint32_t len = le32(f + p + 4);
        const uint8_t* c = f + p + 8;
        if (len > size - p - 8) return fail(error, "a chunk past the end");
        if (std::memcmp(f + p, "WAVE", 4) == 0 && len >= 8) {
            const uint32_t n = le32(c + 4);
            for (uint32_t k = 0; k < n && 8 + k * 4 + 4 <= len; ++k) {
                const size_t ref = size_t(c + 8 + k * 4 - f); // (each entry by its offset from its reference)
                const size_t e = ref + le32(f + ref);
                if (e + 0x30 > size) return fail(error, "a wave past the end");
                SxdWave w;
                w.streamed = (le32(f + e) & 2) != 0;
                w.codec = f[e + 4], w.channels = f[e + 5], w.rate = int(le32(f + e + 8));
                w.frames = le32(f + e + 0x14);
                w.loopStart = int32_t(le32(f + e + 0x18)), w.loopEnd = int32_t(le32(f + e + 0x1c));
                w.size = le32(f + e + 0x20), w.at = w.streamed ? le32(f + e + 0x24) : e + 0x24 + le32(f + e + 0x24);
                const uint32_t cfg = le32(f + e + 0x2c);
                for (int b = 0; b < 4; ++b) w.config[b] = uint8_t(cfg >> (24 - 8 * b));
                // (6 of CR3's Vita voices have no config there - 1 or 3 -, their streams the same mono
                // 48 kHz ~72 kbit/s as the other 5,871, whose config they get; an ATRAC9 config starts 0xfe)
                if (w.codec == 0x42 && w.config[0] != 0xfe && w.channels == 1 && w.rate == 48000) {
                    static const uint8_t kMono48[4] = {0xfe, 0x70, 0x05, 0xf0};
                    std::memcpy(w.config, kMono48, 4);
                }
                if ((!w.streamed && w.at + w.size > size) || w.channels <= 0) return fail(error, "a wave's stream past the end");
                if (w.loopEnd <= w.loopStart || w.loopStart < 0) w.loopStart = w.loopEnd = -1;
                out.push_back(w);
            }
        }
        p += 8 + len;
    }
    return true;
}

bool decodeSxdWave(const SxdWave& w, const uint8_t* data, size_t size, std::vector<int16_t>& out) {
    const size_t ch = size_t(std::max(w.channels, 1));
    out.clear();
    if (w.codec == 0x21) { // (HE-VAG: 16-byte frames of 28 samples, a stereo wave's in turn)
        const size_t frames = size / 16 / ch;
        out.assign(frames * 28 * ch, 0);
        for (size_t c = 0; c < ch; ++c) {
            float h1 = 0, h2 = 0, h3 = 0, h4 = 0;
            for (size_t fr = 0; fr < frames; ++fr) {
                const uint8_t* p = data + (fr * ch + c) * 16;
                const int shift = p[0] & 0x0f, flag = p[1] & 0x0f;
                int index = (p[0] >> 4) | (p[1] & 0xf0);
                if (index > 127) index = 0;
                const float* k = hevag_coefs_f[index];
                for (int i = 0; i < 28; ++i) {
                    float s = 0;
                    if (flag < 7) { // (7: silence)
                        const int nibble = i & 1 ? p[2 + i / 2] >> 4 : p[2 + i / 2] & 0x0f;
                        s = h1 * k[0] + h2 * k[1] + h3 * k[2] + h4 * k[3] + float(((nibble >= 8 ? nibble - 16 : nibble) * 4096) >> shift);
                    }
                    const int v = std::clamp(int(s), -32768, 32767);
                    out[(fr * 28 + size_t(i)) * ch + c] = int16_t(v);
                    h4 = h3, h3 = h2, h2 = h1, h1 = float(v);
                }
            }
        }
    } else if (w.codec == 0x42) {
#ifdef CR3_HAVE_ATRAC9
        void* h = Atrac9GetHandle();
        unsigned char config[4];
        std::memcpy(config, w.config, 4);
        Atrac9CodecInfo info;
        if (!h || Atrac9InitDecoder(h, config) != 0 || Atrac9GetCodecInfo(h, &info) != 0 || info.superframeSize <= 0) {
            if (h) Atrac9ReleaseHandle(h);
            return false;
        }
        std::vector<int16_t> sf(size_t(info.framesInSuperframe) * size_t(info.frameSamples) * size_t(info.channels));
        for (size_t at = 0; at + size_t(info.superframeSize) <= size; at += size_t(info.superframeSize)) {
            const uint8_t* p = data + at;
            bool ok = true;
            for (int k = 0; k < info.framesInSuperframe && ok; ++k) {
                int used = 0;
                ok = Atrac9Decode(h, p, sf.data() + size_t(k) * size_t(info.frameSamples) * size_t(info.channels), &used) == 0 && used > 0;
                p += used;
            }
            if (!ok) break;
            out.insert(out.end(), sf.begin(), sf.end());
        }
        Atrac9ReleaseHandle(h);
#else
        return false;
#endif
    } else {
        return false;
    }
    if (w.frames > 0 && out.size() > size_t(w.frames) * ch) out.resize(size_t(w.frames) * ch); // (its frames alone)
    return !out.empty();
}

std::unique_ptr<MusicStream> openSxdAt9(std::vector<uint8_t> file, std::string* error) {
#ifdef CR3_HAVE_ATRAC9
    auto s = std::make_unique<At9Stream>();
    if (!s->open(std::move(file), error)) return nullptr;
    return s;
#else
    (void)file;
    fail(error, "built without LibAtrac9");
    return nullptr;
#endif
}

std::unique_ptr<MusicStream> openSxdAt9(const std::vector<uint8_t>& header, std::vector<uint8_t> data, std::string* error) {
#ifdef CR3_HAVE_ATRAC9
    auto s = std::make_unique<At9Stream>();
    if (!s->open(header, std::move(data), error)) return nullptr;
    return s;
#else
    (void)header, (void)data;
    fail(error, "built without LibAtrac9");
    return nullptr;
#endif
}

namespace {

// an MP4 box's children: {type, body start, body length} for each, from `p` to `end`
struct Box {
    uint32_t type;
    size_t at, len;
};
std::vector<Box> boxes(const uint8_t* f, size_t p, size_t end) {
    std::vector<Box> out;
    while (p + 8 <= end) {
        uint64_t len = be32(f + p);
        size_t head = 8;
        if (len == 1 && p + 16 <= end) len = be64(f + p + 8), head = 16;
        else if (len == 0) len = end - p;
        if (len < head || len > end - p) break;
        out.push_back({be32(f + p + 4), p + head, size_t(len) - head});
        p += size_t(len);
    }
    return out;
}
constexpr uint32_t fourcc(const char* s) { return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 | uint32_t(uint8_t(s[2])) << 8 | uint8_t(s[3]); }
// the first box of `type` among `in`'s children (none: {0, 0, 0})
Box child(const uint8_t* f, const Box& in, const char* type) {
    for (const Box& b : boxes(f, in.at, in.at + in.len))
        if (b.type == fourcc(type)) return b;
    return {0, 0, 0};
}

} // namespace

bool parseMp4Aac(const std::vector<uint8_t>& file, CafAac& out, std::string* error) {
    const uint8_t* f = file.data();
    out = CafAac();
    Box moov{0, 0, 0};
    for (const Box& b : boxes(f, 0, file.size()))
        if (b.type == fourcc("moov")) moov = b;
    if (!moov.type) return fail(error, "not an MP4 file (no moov)");
    // the first track with AAC (mp4a): its sample table (the movies' second, after their video)
    Box trak{0, 0, 0};
    for (const Box& t : boxes(f, moov.at, moov.at + moov.len)) {
        if (t.type != fourcc("trak")) continue;
        const Box sd = child(f, child(f, child(f, child(f, t, "mdia"), "minf"), "stbl"), "stsd");
        if (sd.type && sd.len >= 16 && be32(f + sd.at + 12) == fourcc("mp4a")) {
            trak = t;
            break;
        }
    }
    if (!trak.type) return fail(error, "not AAC (no mp4a track)");
    const Box mdia = child(f, trak, "mdia");
    const Box stbl = child(f, child(f, mdia, "minf"), "stbl");
    const Box mdhd = child(f, mdia, "mdhd");
    const Box stsd = child(f, stbl, "stsd"), stsz = child(f, stbl, "stsz"), stsc = child(f, stbl, "stsc");
    Box stco = child(f, stbl, "stco");
    const bool co64 = !stco.type;
    if (co64) stco = child(f, stbl, "co64");
    if (!mdhd.type || !stsd.type || !stsz.type || !stsc.type || !stco.type) return fail(error, "no sample table");
    if (stsd.len < 8 + 36 || be32(f + stsd.at + 12) != fourcc("mp4a")) return fail(error, "not AAC (mp4a)");
    const size_t entry = stsd.at + 8; // (the first sample description: size, 'mp4a', then its fields)
    out.channels = int(be16(f + entry + 8 + 16));
    out.rate = int(be32(f + mdhd.at + (f[mdhd.at] == 1 ? 20 : 12))); // (the media's timescale: its rate)
    // (its 'esds' after the 36 bytes of the entry's own fields: version / flags, then descriptors)
    for (const Box& b : boxes(f, entry + 36, std::min<size_t>(entry + be32(f + entry), stsd.at + stsd.len)))
        if (b.type == fourcc("esds") && b.len > 4) findAsc(f + b.at + 4, f + b.at + b.len, out.config);
    if (out.config.empty()) return fail(error, "no AudioSpecificConfig");
    // the samples' sizes, the chunks' offsets, and how many samples each chunk holds
    const uint32_t fixed = be32(f + stsz.at + 4), count = be32(f + stsz.at + 8);
    if (!fixed && stsz.len < 12 + size_t(count) * 4) return fail(error, "stsz short");
    const uint32_t chunks = be32(f + stco.at + 4), runs = be32(f + stsc.at + 4);
    if (stco.len < 8 + size_t(chunks) * (co64 ? 8 : 4) || stsc.len < 8 + size_t(runs) * 12) return fail(error, "stco / stsc short");
    out.data = file; // (the packets where the file has them)
    uint32_t sample = 0;
    for (uint32_t r = 0; r < runs && sample < count; ++r) {
        const uint32_t first = be32(f + stsc.at + 8 + 12 * r) - 1, per = be32(f + stsc.at + 12 + 12 * r);
        const uint32_t last = r + 1 < runs ? be32(f + stsc.at + 8 + 12 * (r + 1)) - 1 : chunks;
        for (uint32_t c = first; c < last && c < chunks && sample < count; ++c) {
            uint64_t at = co64 ? be64(f + stco.at + 8 + 8 * size_t(c)) : be32(f + stco.at + 8 + 4 * size_t(c));
            for (uint32_t k = 0; k < per && sample < count; ++k, ++sample) {
                const uint32_t s = fixed ? fixed : be32(f + stsz.at + 12 + 4 * size_t(sample));
                if (at + s > file.size()) return fail(error, "a packet past the end");
                out.offset.push_back(uint32_t(at)), out.size.push_back(s);
                at += s;
            }
        }
    }
    if (out.offset.empty() || out.config.empty()) return fail(error, "no packets");
    // the encoder's priming and the valid frames: iTunes' iTunSMPB tag (" 00000000 <priming>
    // <padding> <valid>"), else none
    const int64_t total = int64_t(out.offset.size()) * out.framesPerPacket;
    out.validFrames = total;
    static const char kTag[] = "iTunSMPB";
    const auto it = std::search(file.begin() + long(moov.at), file.begin() + long(moov.at + moov.len), kTag, kTag + 8);
    if (it != file.begin() + long(moov.at + moov.len)) {
        unsigned prime = 0, pad = 0;
        unsigned long long valid = 0;
        const size_t p = size_t(it - file.begin()) + 8 + 16; // (the tag, its data box's head and flags)
        std::string text(reinterpret_cast<const char*>(f + p), std::min<size_t>(120, file.size() - p));
        if (std::sscanf(text.c_str(), " %*x %x %x %llx", &prime, &pad, &valid) == 3 && valid > 0 && int64_t(valid) + prime <= total)
            out.priming = int(prime), out.remainder = int(pad), out.validFrames = int64_t(valid);
    }
    return true;
}

std::unique_ptr<MusicStream> openMp4Aac(std::vector<uint8_t> file, std::string* error) {
#ifdef CR3_HAVE_FAAD2
    CafAac packets;
    if (!parseMp4Aac(file, packets, error)) return nullptr;
    auto s = std::make_unique<AacStream>();
    if (!s->open(std::move(packets), error)) return nullptr;
    return s;
#else
    (void)file;
    fail(error, "built without FAAD2");
    return nullptr;
#endif
}

bool decodeWhole(MusicStream& s, std::vector<int16_t>& out) {
    out.clear();
    std::vector<int16_t> buf(4096 * size_t(std::max(s.channels(), 1)));
    for (int n; (n = s.read(buf.data(), 4096)) > 0;) out.insert(out.end(), buf.begin(), buf.begin() + long(n) * s.channels());
    return !out.empty();
}

} // namespace cr3
