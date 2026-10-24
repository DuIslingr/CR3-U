#pragma once
// The game's movies (Cr3System.PlayMovie - MovieControlTask, MVGL::AV::MediaPlayer): cNN.mp4,
// H.264 (Main profile, 1280 x 720, 30 frames a second, B-frames) with AAC LC 48 kHz stereo, and
// their subtitles cNN_en.srt / cNN_jp.srt (MovieControlTask::SM_Set: "%s_jp" for Japanese, else
// "%s_en"). The video is decoded with OpenH264 (the system's, else the copy in
// third_party/openh264), the sound with FAAD2 (audio/sounddata.hpp's MP4 reader). Without
// OpenH264 nothing opens.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cr3 {

class Movie {
public:
    Movie();
    ~Movie();
    Movie(const Movie&) = delete;
    Movie& operator=(const Movie&) = delete;
    // an MP4's video track (avc1: its avcC's parameter sets, its samples) and its sound decoded
    // whole; false: not one, or no decoder
    bool open(std::vector<uint8_t> file, std::string* error = nullptr);
    int width() const { return width_; }
    int height() const { return height_; }
    float fps() const { return fps_; }
    int frames() const { return int(samples_.size()); }
    float duration() const { return fps_ > 0 ? float(samples_.size()) / fps_ : 0.0f; }
    // the next picture in display order as RGBA (BT.601, video range); false at the end
    bool next(std::vector<uint8_t>& rgba);
    // the sound (interleaved s16; empty without FAAD2 or a sound track)
    std::vector<int16_t> pcm;
    int rate = 48000, channels = 2;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<uint8_t> file_;
    std::vector<uint8_t> headers_; // (SPS / PPS, Annex B)
    struct Sample {
        uint64_t at;
        uint32_t size;
    };
    std::vector<Sample> samples_;
    size_t fed_ = 0;
    int nalLength_ = 4, width_ = 0, height_ = 0;
    float fps_ = 30;
    bool flushing_ = false;
    int remaining_ = 0;
    std::vector<uint8_t> annexB_;
};

// a movie's subtitles (SubRip): each line's time span (seconds) and text, "\n" between rows
struct Subtitle {
    float from = 0, to = 0;
    std::string text;
};
std::vector<Subtitle> parseSrt(const std::string& text);

} // namespace cr3
