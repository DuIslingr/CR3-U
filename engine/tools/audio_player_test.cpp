#include "viewer/audio.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
namespace game = cr3;
int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    game::Database database;
    game::Cr3Sound source;
    game::AudioPlayer audio;
    auto closed = [&] {
        problems += audio.opened();
        problems += audio.playSe(0, false) != -1;
        problems += audio.playVoice(0) != -1;
        audio.stopSe(0, 0);
        audio.stopAllSe(0);
        audio.stopAllVoices(0);
        audio.update(0.05f);
        problems += audio.playing(0) || audio.playing(16);
    };
    closed();
    audio.close();
    closed();
    std::string error;
    problems += audio.open("/no-such-chaos-rings-audio-archive", false, &error);
    closed();
    audio.close();
    if (argc>1) {
        error.clear();
        if (!database.open(argv[1], &error) || !source.open(database, &error) ||
            !audio.openCr3(source, true, &error)) return std::fprintf(stderr,"audio source: %s\n",error.c_str()),1;
        if (source.voices().empty() || source.effects().empty()) return std::fprintf(stderr,"test needs the sound asset packs\n"),1;
        const int voice = audio.playVoice(source.voices().front());
        const int effect = audio.playSe(source.effects().front(),true);
        problems += !error.empty(); // the dummy playback device must actually open
        problems += voice<0 || effect<0;
        audio.stopAllVoices(0);
        problems += audio.playing(voice) || !audio.playing(effect);
        audio.stopAllSe(0);
        problems += audio.playing(effect);
        audio.playBgm(1,0);
        problems += audio.bgmState()!=2;
        audio.close();
        problems += audio.bgmState()!=-2 || audio.bgmTrack()!=0;
        closed();
        std::printf("audio player: real voice/effect decode, independent stops and closed music state; %d problems\n",problems);
    }
    SDL_Quit();
    std::printf("audio player: unopened, closed and failed-open playback are safe; %d problems\n", problems);
    return problems ? 1 : 0;
}
