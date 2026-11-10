/* Real SDL_mixer/minimp3 exercise with generated, non-copyrighted MP3 tones.
 * AUDIO4_TEST_TIMING supplies short test-only loop metadata and rest durations. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_mixer.h>
#include <assert.h>
#include <stdio.h>
#include "ExternalMusic.h"

struct MusicDevice *MusicDev;
void MusicCallback(void *unused, Uint8 *stream, int len) {
    (void)unused; SDL_memset(stream,0,(size_t)len);
}
void StopTheMusic(void) { ExternalMusicSuspend(); }
static void update(unsigned long deadline, unsigned long now) {
    ExternalMusicThreat(deadline,now);
    assert(ExternalMusicUpdate(SOUNDTRACK_REMAKE,1,1,75));
}

int main(void) {
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER)==0);
    assert((Mix_Init(MIX_INIT_MP3)&MIX_INIT_MP3)!=0);
    assert(Mix_OpenAudio(48000,AUDIO_S16SYS,2,1024)==0);
    Mix_HookMusic(MusicCallback,&MusicDev);
    Uint8 pcm[4096]={0};
    Mix_Chunk *sfx=Mix_QuickLoad_RAW(pcm,sizeof(pcm));
    assert(sfx && Mix_PlayChannel(0,sfx,-1)==0);
    ExternalMusicTestLoop(0,44100,44100,132300); /* 1-3 seconds */
    ExternalMusicTestLoop(1,44100,44100,132300);
    ExternalMusicTestTiming(1,2); /* game ticks, no real-minute wait */
    update(0,0);
    int boundaries=0;
    double previous=0;
    for (int i=0;i<75;++i) {
        SDL_Delay(100);
        update(0,0);
        double position=Mix_GetMusicPosition(NULL);
        if (position+0.5<previous) ++boundaries;
        assert(position<3.7 && Mix_PlayingMusic() && Mix_Playing(0));
        previous=position;
    }
    assert(boundaries>=2); /* intro once, then repeated body */
    ExternalMusicSetBreaks(1);
    update(0,1); /* eligible music interval elapsed */
    SDL_Delay(2300);
    update(0,1); /* outgoing rest fade completed */
    assert(!Mix_PlayingMusic() && Mix_Playing(0));
    update(0,3); /* natural rest completion */
    assert(Mix_PlayingMusic() && Mix_GetMusicPosition(NULL)<0.5);
    update(0,4); /* second rest begins */
    SDL_Delay(2300);
    update(0,4);
    assert(!Mix_PlayingMusic());
    update(20,5); /* combat interrupts silence immediately */
    assert(Mix_PlayingMusic());
    update(0,21);
    SDL_Delay(1200);
    update(0,21); /* combat fade complete, Explore returns */
    assert(Mix_PlayingMusic() && Mix_Playing(0));
    ExternalMusicShutdown();
    Mix_HaltChannel(0); Mix_FreeChunk(sfx);
    Mix_CloseAudio(); Mix_Quit(); SDL_Quit();
    puts("AUDIO4 real SDL_mixer synthetic intro/loops/rest/combat: PASS");
    return 0;
}
