#pragma once
// CR3's sound (docs/FORMATS.md, "Sound"), from any one release's Database: files named
// `sound\<name>.<ext>`.
//
//   music   54 tracks (m01..), listed in data\bgm.csv with their volume and loop - "pitch1" /
//           "pitch2" are the loop's start and end in 48 kHz frames (the Vita's own rate; the
//           other releases' streams are 44.1 kHz, the same length: scaled to theirs)
//   voices  sound\<name>.snd
//   effects sound\<name>.sndl (16-bit mono, 22,050 Hz), .sndm (stereo, 22,050 Hz), .snds (stereo,
//           44,100 Hz); volumes in data\se.csv
//
// Per release: iOS's .snd is AAC in MP4, Android's Ogg Vorbis; the Vita's music and voices are
// SXD, a header (.snd1) over its stream (.snd2; ATRAC9), and its effects one SXD each (.snd3;
// HE-VAG) - those carry their own rate, channels and loop.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "assets/mvgl.hpp"
#include "audio/sounddata.hpp"

namespace cr3 {

struct BgmTrack {
    std::string name;                     // "m01"
    int volume = 100;                     // %, of the master's
    int64_t loopStart = -1, loopEnd = -1; // 48 kHz frames; -1 none
};

class Cr3Sound {
public:
    struct Clip {
        std::vector<int16_t> pcm; // interleaved
        int rate = 22050, channels = 1;
        int64_t loopStart = -1, loopEnd = -1; // (the Vita's effects say; frames)
    };

    // the release's lists (`db` must outlive this)
    bool open(const Database& db, std::string* error = nullptr);
    const std::vector<BgmTrack>& tracks() const { return tracks_; }
    const BgmTrack* track(const std::string& name) const;
    int masterVolume(int which) const { return master_[which]; } // 0 music, 1 effects, 2 voices
    int effectVolume(const std::string& name) const;         // (se.csv's; 100 unlisted)
    const std::vector<std::string>& effects() const { return effects_; }
    const std::vector<std::string>& voices() const { return voices_; }

    // a music track opened to play, its loop in its own frames (null: missing, or no decoder)
    std::unique_ptr<MusicStream> openTrack(const std::string& name, std::string* error = nullptr) const;
    // any .snd (music or a voice) as a stream, without the loop
    std::unique_ptr<MusicStream> openStream(const std::string& name, std::string* error = nullptr) const;
    bool effect(const std::string& name, Clip& out, std::string* error = nullptr) const;
    bool voice(const std::string& name, Clip& out, std::string* error = nullptr) const;

private:
    const Database* db_ = nullptr;
    std::vector<BgmTrack> tracks_;
    std::vector<std::pair<std::string, int>> seVolumes_;
    std::vector<std::string> effects_, voices_;
    int master_[3] = {100, 100, 100};
};

} // namespace cr3
