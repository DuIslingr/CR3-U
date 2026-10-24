#include "assets/movie.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "audio/sounddata.hpp"

#ifdef CR3_HAVE_OPENH264
#include <wels/codec_api.h>
#endif

namespace cr3 {

namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

// an MP4 box's children (as audio/streams.cpp reads them)
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
Box child(const uint8_t* f, const Box& in, const char* type) {
    for (const Box& b : boxes(f, in.at, in.at + in.len))
        if (b.type == fourcc(type)) return b;
    return {0, 0, 0};
}
bool fail(std::string* error, const char* why) {
    if (error) *error = why;
    return false;
}

} // namespace

struct Movie::Impl {
#ifdef CR3_HAVE_OPENH264
    ISVCDecoder* dec = nullptr;
    ~Impl() {
        if (dec) dec->Uninitialize(), WelsDestroyDecoder(dec);
    }
#endif
};

Movie::Movie() = default;
Movie::~Movie() = default;

bool Movie::open(std::vector<uint8_t> file, std::string* error) {
    file_ = std::move(file);
    const uint8_t* f = file_.data();
    Box moov{0, 0, 0};
    for (const Box& b : boxes(f, 0, file_.size()))
        if (b.type == fourcc("moov")) moov = b;
    if (!moov.type) return fail(error, "not an MP4 file (no moov)");
    // the video track ('vide' handler, an avc1 sample description)
    for (const Box& trak : boxes(f, moov.at, moov.at + moov.len)) {
        if (trak.type != fourcc("trak")) continue;
        const Box mdia = child(f, trak, "mdia"), hdlr = child(f, mdia, "hdlr");
        if (!hdlr.type || hdlr.len < 12 || be32(f + hdlr.at + 8) != fourcc("vide")) continue;
        const Box mdhd = child(f, mdia, "mdhd");
        const Box stbl = child(f, child(f, mdia, "minf"), "stbl");
        const Box stsd = child(f, stbl, "stsd"), stsz = child(f, stbl, "stsz"), stsc = child(f, stbl, "stsc"), stts = child(f, stbl, "stts");
        Box stco = child(f, stbl, "stco");
        const bool co64 = !stco.type;
        if (co64) stco = child(f, stbl, "co64");
        if (!mdhd.type || !stsd.type || !stsz.type || !stsc.type || !stco.type || stsd.len < 16) return fail(error, "no sample table");
        const size_t entry = stsd.at + 8; // (size, 'avc1', then 78 bytes of fields, then its boxes)
        const uint32_t codec = be32(f + entry + 4);
        if (codec != fourcc("avc1") && codec != fourcc("avc3")) return fail(error, "not H.264 (avc1)");
        width_ = be16(f + entry + 8 + 24), height_ = be16(f + entry + 8 + 26);
        for (const Box& b : boxes(f, entry + 86, std::min<size_t>(entry + be32(f + entry), stsd.at + stsd.len))) {
            if (b.type != fourcc("avcC") || b.len < 7) continue;
            // (AVCDecoderConfigurationRecord: the NAL length's size, the SPS and PPS)
            const uint8_t* c = f + b.at;
            const uint8_t* end = c + b.len;
            nalLength_ = (c[4] & 3) + 1;
            const uint8_t* p = c + 5;
            for (int set = 0; set < 2 && p < end; ++set) {
                int n = set == 0 ? (*p++ & 0x1f) : *p++;
                for (; n > 0 && p + 2 <= end; --n) {
                    const size_t len = be16(p);
                    p += 2;
                    if (p + len > end) break;
                    static const uint8_t kStart[4] = {0, 0, 0, 1};
                    headers_.insert(headers_.end(), kStart, kStart + 4);
                    headers_.insert(headers_.end(), p, p + len);
                    p += len;
                }
            }
        }
        if (headers_.empty()) return fail(error, "no avcC");
        const uint32_t scale = be32(f + mdhd.at + (f[mdhd.at] == 1 ? 20 : 12));
        if (stts.type && stts.len >= 16 && be32(f + stts.at + 4) > 0 && be32(f + stts.at + 12) > 0) fps_ = float(scale) / float(be32(f + stts.at + 12));
        const uint32_t fixed = be32(f + stsz.at + 4), count = be32(f + stsz.at + 8);
        const uint32_t chunks = be32(f + stco.at + 4), runs = be32(f + stsc.at + 4);
        if ((!fixed && stsz.len < 12 + size_t(count) * 4) || stco.len < 8 + size_t(chunks) * (co64 ? 8 : 4) || stsc.len < 8 + size_t(runs) * 12)
            return fail(error, "sample table short");
        uint32_t sample = 0;
        for (uint32_t r = 0; r < runs && sample < count; ++r) {
            const uint32_t first = be32(f + stsc.at + 8 + 12 * r) - 1, per = be32(f + stsc.at + 12 + 12 * r);
            const uint32_t last = r + 1 < runs ? be32(f + stsc.at + 8 + 12 * (r + 1)) - 1 : chunks;
            for (uint32_t c = first; c < last && c < chunks && sample < count; ++c) {
                uint64_t at = co64 ? be64(f + stco.at + 8 + 8 * size_t(c)) : be32(f + stco.at + 8 + 4 * size_t(c));
                for (uint32_t k = 0; k < per && sample < count; ++k, ++sample) {
                    const uint32_t s = fixed ? fixed : be32(f + stsz.at + 12 + 4 * size_t(sample));
                    if (at + s > file_.size()) return fail(error, "a sample past the end");
                    samples_.push_back({at, s});
                    at += s;
                }
            }
        }
        break;
    }
    if (samples_.empty()) return fail(error, "no video track");
#ifdef CR3_HAVE_OPENH264
    impl_ = std::make_unique<Impl>();
    if (WelsCreateDecoder(&impl_->dec) != 0 || !impl_->dec) return fail(error, "OpenH264: no decoder");
    SDecodingParam p;
    std::memset(&p, 0, sizeof p);
    p.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    p.eEcActiveIdc = ERROR_CON_SLICE_COPY;
    if (impl_->dec->Initialize(&p) != 0) return fail(error, "OpenH264: not initialised");
#else
    return fail(error, "built without OpenH264");
#endif
    // the sound: the AAC track decoded whole (FAAD2)
    if (auto s = openMp4Aac(file_)) {
        rate = s->rate(), channels = s->channels();
        decodeWhole(*s, pcm);
    }
    return true;
}

bool Movie::next(std::vector<uint8_t>& rgba) {
#ifdef CR3_HAVE_OPENH264
    if (!impl_ || !impl_->dec) return false;
    ISVCDecoder* d = impl_->dec;
    SBufferInfo info;
    unsigned char* dst[3] = {};
    for (;;) {
        std::memset(&info, 0, sizeof info);
        if (fed_ < samples_.size()) {
            // a sample (an access unit): its NAL units' lengths as start codes, the parameter sets first
            const Sample& s = samples_[fed_];
            annexB_.clear();
            if (fed_ == 0) annexB_ = headers_;
            for (size_t p = s.at; p + size_t(nalLength_) <= s.at + s.size;) {
                size_t len = 0;
                for (int k = 0; k < nalLength_; ++k) len = len << 8 | file_[p + size_t(k)];
                p += size_t(nalLength_);
                if (p + len > s.at + s.size) break;
                static const uint8_t kStart[4] = {0, 0, 0, 1};
                annexB_.insert(annexB_.end(), kStart, kStart + 4);
                annexB_.insert(annexB_.end(), file_.begin() + long(p), file_.begin() + long(p + len));
                p += len;
            }
            info.uiInBsTimeStamp = fed_;
            ++fed_;
            d->DecodeFrameNoDelay(annexB_.data(), int(annexB_.size()), dst, &info);
        } else {
            // the end: the pictures still held back for their order (FlushFrame)
            if (!flushing_) {
                flushing_ = true;
                int eos = 1;
                d->SetOption(DECODER_OPTION_END_OF_STREAM, &eos);
                d->GetOption(DECODER_OPTION_NUM_OF_FRAMES_REMAINING_IN_BUFFER, &remaining_);
            }
            if (remaining_-- <= 0) return false;
            d->FlushFrame(dst, &info);
        }
        if (info.iBufferStatus != 1) continue;
        const int w = info.UsrData.sSystemBuffer.iWidth, h = info.UsrData.sSystemBuffer.iHeight;
        const int ys = info.UsrData.sSystemBuffer.iStride[0], cs = info.UsrData.sSystemBuffer.iStride[1];
        width_ = w, height_ = h;
        rgba.resize(size_t(w) * size_t(h) * 4);
        // (BT.709, video range: the HD movies carry no colour description)
        for (int y = 0; y < h; ++y) {
            const uint8_t* Y = info.pDst[0] + size_t(y) * size_t(ys);
            const uint8_t* U = info.pDst[1] + size_t(y / 2) * size_t(cs);
            const uint8_t* V = info.pDst[2] + size_t(y / 2) * size_t(cs);
            uint8_t* o = &rgba[size_t(y) * size_t(w) * 4];
            for (int x = 0; x < w; ++x, o += 4) {
                const int c = 298 * (Y[x] - 16) + 128, du = U[x / 2] - 128, dv = V[x / 2] - 128;
                o[0] = uint8_t(std::clamp((c + 459 * dv) >> 8, 0, 255));
                o[1] = uint8_t(std::clamp((c - 55 * du - 136 * dv) >> 8, 0, 255));
                o[2] = uint8_t(std::clamp((c + 541 * du) >> 8, 0, 255));
                o[3] = 255;
            }
        }
        return true;
    }
#else
    (void)rgba;
    return false;
#endif
}

std::vector<Subtitle> parseSrt(const std::string& text) {
    std::vector<Subtitle> out;
    std::string s = text;
    if (s.compare(0, 3, "\xef\xbb\xbf") == 0) s.erase(0, 3);
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    size_t p = 0;
    while (p < s.size()) {
        const size_t arrow = s.find("-->", p);
        if (arrow == std::string::npos) break;
        const size_t lineStart = s.rfind('\n', arrow) == std::string::npos ? 0 : s.rfind('\n', arrow) + 1;
        const size_t lineEnd = s.find('\n', arrow);
        int h1, m1, s1, ms1, h2, m2, s2, ms2;
        if (std::sscanf(s.c_str() + lineStart, "%d:%d:%d,%d --> %d:%d:%d,%d", &h1, &m1, &s1, &ms1, &h2, &m2, &s2, &ms2) == 8) {
            Subtitle t;
            t.from = float(h1 * 3600 + m1 * 60 + s1) + float(ms1) / 1000.0f;
            t.to = float(h2 * 3600 + m2 * 60 + s2) + float(ms2) / 1000.0f;
            size_t q = lineEnd == std::string::npos ? s.size() : lineEnd + 1;
            const size_t blank = s.find("\n\n", q);
            const size_t stop = blank == std::string::npos ? s.size() : blank;
            t.text = s.substr(q, stop - q);
            while (!t.text.empty() && (t.text.back() == '\n' || t.text.back() == ' ')) t.text.pop_back();
            out.push_back(t);
            p = stop;
        } else {
            p = arrow + 3;
        }
    }
    return out;
}

} // namespace cr3
