#include "viewer/audio.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace cr3 {

namespace {

constexpr int kEffectSlots = 16, kVoiceSlots = 16;
constexpr float kUpdates = 20.0f; // (the game's updates a second: what fades count)
constexpr float kBgmAhead = 0.5f; // (seconds of the music kept queued)

} // namespace

AudioPlayer::~AudioPlayer() { close(); }

bool AudioPlayer::openDevice(bool device, std::string* error) {
    slots_.assign(kEffectSlots + kVoiceSlots, Voice());
    if (device) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            if (error) *error = std::string("SDL audio: ") + SDL_GetError();
        } else {
            device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
            if (!device_ && error) *error = std::string("no audio device: ") + SDL_GetError();
        }
    }
    return true;
}

// CR3's hooks: the scripts' fades count the game's frames (30 a second), these the updates (20)
bool AudioPlayer::openCr3(const Cr3Sound& sound, bool device, std::string* error) {
    close();
    cr3_ = &sound;
    const bool ok = openDevice(device, error);
    const auto frames = [](int f) { return f > 0 ? std::max(1, (f * 2 + 1) / 3) : 0; };
    SoundHooks& h = soundHooks();
    h.playBgm = [this, frames](int t, int f) { playBgm(t, frames(f)); };
    h.playBgmOnce = [this, frames](int t, int f, bool loop) {
        if (t < 0) return;
        char n[16];
        std::snprintf(n, sizeof n, "m%02d", t);
        playBgm(n, frames(f), loop);
        track_ = t;
    };
    h.stopBgm = [this, frames](int f) { stopBgm(frames(f)); };
    h.releaseBgm = [this] { releaseBgm(); };
    h.state = [this](int which) { // (utils::SoundGetState: 2 the music on / a voice on, else 1)
        if (which == 0) return bgmPlaying_ ? 2 : 1;
        for (int k = kEffectSlots; k < std::min(kEffectSlots + kVoiceSlots, int(slots_.size())); ++k)
            if (which == 2 && slots_[size_t(k)].active) return 2;
        return 1;
    };
    h.playSe = [this](int c, bool loop) { return playSe(c, loop); };
    h.stopSe = [this, frames](int s, int f) { stopSe(s, frames(f)); };
    h.stopAllSe = [this, frames](int f) { stopAllSe(frames(f)); };
    h.stopAllVoices = [this, frames](int f) { stopAllVoices(frames(f)); };
    h.playSeNamed = [this, frames](const std::string& n, bool loop, int f) { return playSe(n, loop, frames(f)); };
    h.stopSeNamed = [this, frames](const std::string& n, int f) { stopSe(n, frames(f)); };
    h.playVoiceNamed = [this](const std::string& n) { return playVoice(n); };
    h.playingNamed = [this](const std::string& n) { return playingNamed(n); };
    h.bgmNo = [this] { return bgmPlaying_ ? track_ : -1; };
    h.voicePlaying = [this] { return voicePlaying(); };
    return ok;
}

bool AudioPlayer::open(const std::string& archivePath, bool device, std::string* error) {
    close();
    if (!data_.open(archivePath, error)) return false;
    openDevice(device, error);
    SoundHooks& h = soundHooks();
    h.playBgm = [this](int t, int f) { playBgm(t, f); };
    h.stopBgm = [this](int f) { stopBgm(f); };
    h.releaseBgm = [this] { releaseBgm(); };
    h.state = [this](int which) { return which == 0 ? bgmState() : 2; };
    h.playSe = [this](int c, bool loop) { return playSe(c, loop); };
    h.stopSe = [this](int s, int f) { stopSe(s, f); };
    h.stopAllSe = [this](int f) { stopAllSe(f); };
    h.playVoice = [this](int c) { return playVoice(c); };
    h.stopAllVoices = [this](int f) { stopAllVoices(f); };
    h.voicePlaying = [this] { return voicePlaying(); };
    return true;
}

void AudioPlayer::close() {
    if (!slots_.empty()) soundHooks() = SoundHooks();
    for (Voice& v : slots_) endVoice(v);
    endVoice(movie_);
    slots_.clear();
    releaseBgm();
    bgmPos_ = 0, bgmFade_ = 1, bgmFadeStep_ = 0;
    if (device_) SDL_CloseAudioDevice(device_), device_ = 0;
    data_ = SoundData();
    cr3_ = nullptr;
}

void AudioPlayer::setVolumes(float bgm, float effects, float voices) {
    volume_[0] = std::clamp(bgm, 0.0f, 1.0f), volume_[1] = std::clamp(effects, 0.0f, 1.0f), volume_[2] = std::clamp(voices, 0.0f, 1.0f);
    applyGains();
}

void AudioPlayer::applyGains() {
    if (bgm_) SDL_SetAudioStreamGain(bgm_, volume_[0] * bgmFade_);
    if (movie_.stream) SDL_SetAudioStreamGain(movie_.stream, volume_[0]);
    for (const Voice& v : slots_)
        if (v.stream) SDL_SetAudioStreamGain(v.stream, volume_[v.voice ? 2 : 1] * v.fade);
}

// Cr3UtilSoundPlayBGM: the track attached (Sound/bgmNN), its repeat frame, played, faded in
void AudioPlayer::playBgm(int track, int fadeFrames) {
    if (cr3_) { // (CR3's CrxSound::PlayBgm(int): the track m<NN>)
        char n[16];
        std::snprintf(n, sizeof n, "m%02d", track);
        if (track >= 0) playBgm(n, fadeFrames), track_ = track;
        return;
    }
    log.push_back("bgm " + std::to_string(track));
    releaseBgm();
    std::string err;
    std::unique_ptr<MusicStream> o = data_.openTrack(track, &err);
    if (!o) {
        SDL_Log("sound: bgm%02d - none to play (%s)", track, err.c_str());
        return;
    }
    track_ = track;
    SDL_Log("sound: bgm%02d%s", track, device_ ? "" : " (silent)");
    startMusic(std::move(o), fadeFrames);
}

void AudioPlayer::playBgm(const std::string& name, int fadeFrames, bool loop) {
    log.push_back("bgm " + name);
    releaseBgm();
    std::string err;
    std::unique_ptr<MusicStream> o = cr3_ ? cr3_->openTrack(name, &err) : nullptr;
    if (!o) {
        SDL_Log("sound: %s - none to play (%s)", name.c_str(), cr3_ ? err.c_str() : "no CR3 sound");
        return;
    }
    if (!loop) o->loopStart = o->loopEnd = -1; // (PlayOnceBGM: to its end, then over)
    track_ = name.size() == 3 && name[0] == 'm' ? std::atoi(name.c_str() + 1) : -1;
    SDL_Log("sound: %s, %d Hz x%d, loop %lld..%lld%s", name.c_str(), o->rate(), o->channels(), static_cast<long long>(o->loopStart),
            static_cast<long long>(o->loopEnd), device_ ? "" : " (silent)");
    startMusic(std::move(o), fadeFrames);
}

void AudioPlayer::startMusic(std::unique_ptr<MusicStream> o, int fadeFrames) {
    bgmPlaying_ = true, bgmPos_ = 0;
    bgmFade_ = fadeFrames > 0 ? 0.0f : 1.0f;
    bgmFadeStep_ = fadeFrames > 0 ? kUpdates / float(fadeFrames) : 0.0f;
    if (device_) {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16, spec.channels = o->channels(), spec.freq = o->rate();
        bgm_ = SDL_CreateAudioStream(&spec, nullptr);
        if (bgm_ && !SDL_BindAudioStream(device_, bgm_)) SDL_DestroyAudioStream(bgm_), bgm_ = nullptr;
    }
    music_ = std::move(o);
    applyGains();
    feedBgm();
}

void AudioPlayer::feedBgm() {
    if (!music_ || !bgm_ || !bgmPlaying_) return;
    MusicStream& m = *music_;
    const int ch = m.channels(), rate = m.rate();
    const int want = int(kBgmAhead * float(rate)) * ch * 2;
    std::vector<int16_t> buf(4096 * size_t(ch));
    while (SDL_GetAudioStreamQueued(bgm_) < want) {
        int n = 4096;
        if (m.loopStart >= 0 && m.loopEnd >= 0) n = int(std::clamp<int64_t>(m.loopEnd + 1 - bgmPos_, 0, 4096)); // (up to the loop's end)
        int got = n > 0 ? m.read(buf.data(), n) : 0;
        if (got <= 0) { // (its end, or its loop's: back to the repeat frame, else over)
            if (m.loopStart < 0 || !m.seek(uint32_t(m.loopStart))) {
                SDL_FlushAudioStream(bgm_);
                bgmPlaying_ = false;
                return;
            }
            bgmPos_ = m.loopStart;
            continue;
        }
        bgmPos_ += got;
        SDL_PutAudioStreamData(bgm_, buf.data(), got * ch * 2);
    }
}

// Cr3UtilSoundStopBGM: faded out (SetFade 100 -> 0 over the frames), then stopped
void AudioPlayer::stopBgm(int fadeFrames) {
    log.push_back("stop bgm");
    if (fadeFrames <= 0) {
        if (bgm_) SDL_ClearAudioStream(bgm_);
        bgmPlaying_ = false;
        return;
    }
    bgmFadeStep_ = -kUpdates / float(fadeFrames);
}

void AudioPlayer::releaseBgm() {
    if (bgm_) SDL_DestroyAudioStream(bgm_), bgm_ = nullptr;
    music_.reset();
    bgmPlaying_ = false, track_ = 0;
}

int AudioPlayer::bgmState() const { return bgmPlaying_ ? 2 : -2; }

void AudioPlayer::endVoice(Voice& v) {
    if (v.stream) SDL_DestroyAudioStream(v.stream);
    v = Voice();
}

// MVGL::AV::MediaPlayer's sound: a movie's whole track at once, at the music's volume
void AudioPlayer::playMovieSound(std::vector<int16_t> pcm, int rate, int channels) {
    log.push_back("movie sound");
    startPcm(movie_, std::move(pcm), rate, channels, false, false);
    if (movie_.stream) SDL_SetAudioStreamGain(movie_.stream, volume_[0]);
}

void AudioPlayer::stopMovieSound() { endVoice(movie_); }

void AudioPlayer::startClip(Voice& v, SoundData::Bank bank, int clip, bool loop) {
    endVoice(v);
    SoundData::ClipFormat fmt;
    std::vector<int16_t> pcm;
    if (!data_.clip(bank, clip, pcm, &fmt) || pcm.empty()) return;
    startPcm(v, std::move(pcm), fmt.rate, fmt.channels, loop, bank == SoundData::Voices);
    v.clip = clip;
}

int AudioPlayer::playSe(const std::string& name, bool loop, int fadeInFrames) {
    log.push_back("se " + name + (loop ? " (looping)" : ""));
    SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO, "sound: se %s%s", name.c_str(), loop ? " (looping)" : "");
    if (!cr3_ || volume_[1] <= 0) return -1;
    Cr3Sound::Clip c;
    std::string err;
    if (!cr3_->effect(name, c, &err)) return SDL_Log("sound: se %s - %s", name.c_str(), err.c_str()), -1;
    for (int k = 0; k < std::min(kEffectSlots, int(slots_.size())); ++k)
        if (!slots_[size_t(k)].active) {
            Voice& v = slots_[size_t(k)];
            startPcm(v, std::move(c.pcm), c.rate, c.channels, loop, false);
            v.name = name;
            if (fadeInFrames > 0) v.fade = 0, v.fadeStep = kUpdates / float(fadeInFrames); // (PlaySE_Fade: up from 0)
            return v.active ? k : -1;
        }
    return -1; // (all busy)
}

void AudioPlayer::stopSe(const std::string& name, int fadeFrames) {
    for (int k = 0; k < std::min(kEffectSlots, int(slots_.size())); ++k)
        if (slots_[size_t(k)].active && slots_[size_t(k)].name == name) stopSe(k, fadeFrames);
}

bool AudioPlayer::voicePlaying() const {
    for (const Voice& v : slots_)
        if (v.active && v.voice && !v.loop) return true;
    return false;
}

bool AudioPlayer::playingNamed(const std::string& name) const {
    for (const Voice& v : slots_)
        if (v.active && v.name == name) return true;
    return false;
}

int AudioPlayer::playVoice(const std::string& name) {
    log.push_back("voice " + name);
    SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO, "sound: voice %s", name.c_str());
    if (!cr3_ || volume_[2] <= 0) return -1;
    Cr3Sound::Clip c;
    std::string err;
    if (!cr3_->voice(name, c, &err)) return SDL_Log("sound: voice %s - %s", name.c_str(), err.c_str()), -1;
    SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO, "sound: voice %s %.2f s", name.c_str(), c.rate && c.channels ? double(c.pcm.size()) / c.rate / c.channels : 0.0);
    for (int k = kEffectSlots; k < std::min(kEffectSlots + kVoiceSlots, int(slots_.size())); ++k)
        if (!slots_[size_t(k)].active) {
            startPcm(slots_[size_t(k)], std::move(c.pcm), c.rate, c.channels, false, true);
            slots_[size_t(k)].name = name;
            return slots_[size_t(k)].active ? k : -1;
        }
    return -1;
}

void AudioPlayer::startPcm(Voice& v, std::vector<int16_t> pcm, int rate, int channels, bool loop, bool voice) {
    endVoice(v);
    if (pcm.empty()) return;
    v.pcm = std::move(pcm);
    v.rate = rate, v.channels = channels;
    v.active = true, v.loop = loop, v.clip = -1, v.voice = voice;
    if (!device_) return;
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16, spec.channels = v.channels, spec.freq = v.rate;
    v.stream = SDL_CreateAudioStream(&spec, nullptr);
    if (!v.stream || !SDL_BindAudioStream(device_, v.stream)) {
        endVoice(v);
        return;
    }
    SDL_SetAudioStreamGain(v.stream, volume_[v.voice ? 2 : 1]);
    SDL_PutAudioStreamData(v.stream, v.pcm.data(), int(v.pcm.size() * 2));
    if (!loop) SDL_FlushAudioStream(v.stream);
}

// Cr3UtilSoundPlaySE (none at a volume of 0), PlaySE_Repeat (Cr3SoundSetRepeatOnce: looping)
int AudioPlayer::playSe(int clip, bool loop) {
    if (cr3_) // (CR3's CrxSound::PlaySE(int): crx_se_name_table's name - its 1033, sorted, the effects')
        return clip >= 0 && size_t(clip) < cr3_->effects().size() ? playSe(cr3_->effects()[size_t(clip)], loop) : -1;
    log.push_back("se " + std::to_string(clip) + (loop ? " (looping)" : ""));
    SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO, "sound: se %d%s", clip, loop ? " (looping)" : "");
    if (volume_[1] <= 0) return -1;
    for (int k = 0; k < std::min(kEffectSlots, int(slots_.size())); ++k)
        if (!slots_[size_t(k)].active) {
            startClip(slots_[size_t(k)], SoundData::Effects, clip, loop);
            return slots_[size_t(k)].active ? k : -1;
        }
    return -1; // (all busy)
}

void AudioPlayer::stopSe(int slot, int fadeFrames) {
    if (slot < 0 || slot >= kEffectSlots || size_t(slot) >= slots_.size()) return;
    Voice& v = slots_[size_t(slot)];
    if (fadeFrames <= 0) endVoice(v);
    else v.fadeStep = -kUpdates / float(fadeFrames);
}

void AudioPlayer::stopAllSe(int fadeFrames) {
    for (int k = 0; k < std::min(kEffectSlots, int(slots_.size())); ++k) stopSe(k, fadeFrames);
}

// Cr3UtilSoundPlayVoice (none at a volume of 0)
int AudioPlayer::playVoice(int clip) {
    log.push_back("voice " + std::to_string(clip));
    SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO, "sound: voice %d", clip);
    if (volume_[2] <= 0) return -1;
    for (int k = kEffectSlots; k < std::min(kEffectSlots + kVoiceSlots, int(slots_.size())); ++k)
        if (!slots_[size_t(k)].active) {
            startClip(slots_[size_t(k)], SoundData::Voices, clip, false);
            return slots_[size_t(k)].active ? k : -1;
        }
    return -1;
}

void AudioPlayer::stopAllVoices(int fadeFrames) {
    for (int k = kEffectSlots; k < std::min(kEffectSlots + kVoiceSlots, int(slots_.size())); ++k) {
        Voice& v = slots_[size_t(k)];
        if (fadeFrames <= 0) endVoice(v);
        else v.fadeStep = -kUpdates / float(fadeFrames);
    }
}

bool AudioPlayer::playing(int slot) const { return slot >= 0 && slot < int(slots_.size()) && slots_[size_t(slot)].active; }

void AudioPlayer::update(float dt) {
    // the music's fade (in, or out to a stop)
    if (bgmFadeStep_ != 0) {
        bgmFade_ += bgmFadeStep_ * dt;
        if (bgmFade_ >= 1) bgmFade_ = 1, bgmFadeStep_ = 0;
        if (bgmFade_ <= 0) {
            bgmFade_ = 0, bgmFadeStep_ = 0;
            if (bgm_) SDL_ClearAudioStream(bgm_);
            bgmPlaying_ = false;
        }
    }
    feedBgm();
    for (Voice& v : slots_) {
        if (!v.active) continue;
        if (v.fadeStep != 0) {
            v.fade += v.fadeStep * dt;
            if (v.fade >= 1 && v.fadeStep > 0) v.fade = 1, v.fadeStep = 0;
            if (v.fade <= 0) {
                endVoice(v);
                continue;
            }
        }
        if (!v.stream) { // (silent: over after its length)
            if (!v.loop) {
                const float left = float(v.pcm.size()) / float(v.rate * v.channels);
                v.pcm.resize(size_t(std::max(0.0f, left - dt) * float(v.rate)) * size_t(v.channels));
                if (v.pcm.empty()) endVoice(v);
            }
            continue;
        }
        if (v.loop) {
            if (SDL_GetAudioStreamQueued(v.stream) < int(v.pcm.size())) SDL_PutAudioStreamData(v.stream, v.pcm.data(), int(v.pcm.size() * 2));
        } else if (SDL_GetAudioStreamQueued(v.stream) == 0 && SDL_GetAudioStreamAvailable(v.stream) == 0) {
            endVoice(v);
        }
    }
    applyGains();
}

} // namespace cr3
