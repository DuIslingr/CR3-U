#include "audio/cr3sound.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace cr3 {

namespace {

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// a CSV's rows, its cells split at commas (the comments' Shift-JIS left as bytes)
std::vector<std::vector<std::string>> csv(const std::vector<uint8_t>& d) {
    std::vector<std::vector<std::string>> rows;
    std::string line;
    std::istringstream in(std::string(d.begin(), d.end()));
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> cells;
        size_t at = 0;
        for (size_t comma; (comma = line.find(',', at)) != std::string::npos; at = comma + 1) cells.push_back(line.substr(at, comma - at));
        cells.push_back(line.substr(at));
        rows.push_back(std::move(cells));
    }
    return rows;
}

long number(const std::string& s, long fallback) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    return end && end != s.c_str() ? v : fallback;
}

bool isVita(const Database& db) { return db.kind() == Database::Kind::Vita; }

// an .snd (music, a voice): Ogg Vorbis or AAC in MP4, by its first bytes
std::unique_ptr<MusicStream> openSnd(std::vector<uint8_t> d, std::string* error) {
    if (d.size() >= 4 && std::memcmp(d.data(), "OggS", 4) == 0) {
        auto s = std::make_unique<OggStream>();
        if (!s->open(std::move(d))) return fail(error, "Ogg Vorbis that doesn't decode"), nullptr;
        return s;
    }
    if (d.size() >= 8 && std::memcmp(d.data() + 4, "ftyp", 4) == 0) return openMp4Aac(std::move(d), error);
    return fail(error, "neither Ogg Vorbis nor MP4"), nullptr;
}

} // namespace

bool Cr3Sound::open(const Database& db, std::string* error) {
    *this = Cr3Sound();
    db_ = &db;
    std::vector<uint8_t> d;
    if (!db.read("data\\bgm.csv", d)) return fail(error, "no data\\bgm.csv");
    for (const auto& row : csv(d)) {
        if (row.size() < 4 || row[0].empty() || row[0] == "name") continue;
        if (row[0] == "master") {
            master_[0] = int(number(row[1], 100));
            continue;
        }
        BgmTrack t;
        t.name = row[0];
        t.volume = int(number(row[1], 100));
        t.loopStart = number(row[2], -1), t.loopEnd = number(row[3], -1);
        if (t.loopEnd <= t.loopStart || t.loopStart < 0) t.loopStart = t.loopEnd = -1;
        tracks_.push_back(t);
    }
    if (db.read("data\\se.csv", d))
        for (const auto& row : csv(d)) {
            if (row.size() < 2 || row[0].empty() || row[0] == "name") continue;
            if (row[0] == "master") master_[1] = int(number(row[1], 100));
            else seVolumes_.emplace_back(lower(row[0]), int(number(row[1], 100)));
        }
    if (db.read("data\\voice.csv", d))
        for (const auto& row : csv(d))
            if (row.size() >= 2 && row[0] == "master") master_[2] = int(number(row[1], 100));
    // the effects and voices the release has
    for (const auto& e : db.entries()) {
        const std::string k = Database::key(e.name);
        if (k.compare(0, 6, "sound\\") != 0) continue;
        const size_t dot = k.rfind('.');
        if (dot == std::string::npos) continue;
        const std::string base = e.name.substr(6, dot - 6), ext = k.substr(dot + 1);
        if (ext == "sndl" || ext == "sndm" || ext == "snds" || ext == "snd3") effects_.push_back(base);
        else if ((ext == "snd" || ext == "snd1") && !track(base)) voices_.push_back(base);
    }
    std::sort(effects_.begin(), effects_.end());
    std::sort(voices_.begin(), voices_.end());
    if (tracks_.empty()) return fail(error, "data\\bgm.csv lists no tracks");
    return true;
}

const BgmTrack* Cr3Sound::track(const std::string& name) const {
    const std::string n = lower(name);
    for (const BgmTrack& t : tracks_)
        if (lower(t.name) == n) return &t;
    return nullptr;
}

int Cr3Sound::effectVolume(const std::string& name) const {
    const std::string n = lower(name);
    for (const auto& [k, v] : seVolumes_)
        if (k == n) return v;
    return 100;
}

std::unique_ptr<MusicStream> Cr3Sound::openStream(const std::string& name, std::string* error) const {
    if (!db_) return fail(error, "no release"), nullptr;
    std::vector<uint8_t> a, b;
    if (isVita(*db_)) {
        if (!db_->read("sound\\" + name + ".snd1", a, error) || !db_->read("sound\\" + name + ".snd2", b, error)) return nullptr;
        return openSxdAt9(a, std::move(b), error);
    }
    if (!db_->read("sound\\" + name + ".snd", a, error)) return nullptr;
    return openSnd(std::move(a), error);
}

std::unique_ptr<MusicStream> Cr3Sound::openTrack(const std::string& name, std::string* error) const {
    const BgmTrack* t = track(name);
    if (!t) return fail(error, name + ": not in bgm.csv"), nullptr;
    auto s = openStream(t->name, error);
    if (!s) return nullptr;
    if (t->loopStart >= 0) { // (bgm.csv's 48 kHz frames, in the stream's own)
        const double k = double(s->rate()) / 48000.0;
        s->loopStart = int64_t(double(t->loopStart) * k + 0.5);
        s->loopEnd = std::min<int64_t>(int64_t(double(t->loopEnd) * k + 0.5), s->frames());
    }
    return s;
}

bool Cr3Sound::effect(const std::string& name, Clip& out, std::string* error) const {
    out = Clip();
    if (!db_) return fail(error, "no release");
    std::vector<uint8_t> d;
    if (isVita(*db_)) {
        if (!db_->read("sound\\" + name + ".snd3", d, error)) return false;
        std::vector<SxdWave> waves;
        if (!parseSxdWaves(d, waves, error) || waves.empty()) return fail(error, name + ": no wave");
        const SxdWave& w = waves[0];
        if (!decodeSxdWave(w, d.data() + w.at, w.size, out.pcm)) return fail(error, name + ": its wave doesn't decode");
        out.rate = w.rate, out.channels = w.channels, out.loopStart = w.loopStart, out.loopEnd = w.loopEnd;
        return true;
    }
    static const struct { const char* ext; int rate, channels; } kRaw[] = {{"sndl", 22050, 1}, {"sndm", 22050, 2}, {"snds", 44100, 2}};
    for (const auto& r : kRaw) {
        if (!db_->contains("sound\\" + name + "." + r.ext)) continue;
        if (!db_->read("sound\\" + name + "." + r.ext, d, error)) return false;
        out.rate = r.rate, out.channels = r.channels;
        out.pcm.resize(d.size() / 2 / size_t(r.channels) * size_t(r.channels));
        std::memcpy(out.pcm.data(), d.data(), out.pcm.size() * 2);
        return true;
    }
    return fail(error, name + ": no such effect");
}

bool Cr3Sound::voice(const std::string& name, Clip& out, std::string* error) const {
    out = Clip();
    auto s = openStream(name, error);
    if (!s) return false;
    out.rate = s->rate(), out.channels = s->channels();
    return decodeWhole(*s, out.pcm) || fail(error, name + ": no samples");
}

} // namespace cr3
