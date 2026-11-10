/* Optional real-pack regression: supply a user-owned, hash-matching
 * music/remake/medical.mp3 in the scratch working directory. Never package it. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_mixer.h>
#include <assert.h>
#include <stdio.h>
#include "ExternalMusic.h"
#include "Audio4Loops.h"

struct MusicDevice *MusicDev;
void MusicCallback(void *unused, Uint8 *stream, int len) {
    (void)unused; SDL_memset(stream,0,(size_t)len);
}
void StopTheMusic(void) { ExternalMusicSuspend(); }

int main(void) {
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER)==0);
    assert((Mix_Init(MIX_INIT_MP3)&MIX_INIT_MP3)!=0);
    assert(Mix_OpenAudio(48000,AUDIO_S16SYS,2,1024)==0);
    Mix_HookMusic(MusicCallback,&MusicDev);
    ExternalMusicThreat(0,0);
    assert(ExternalMusicUpdate(SOUNDTRACK_REMAKE,1,1,75));
    assert(Mix_GetMusicPosition(NULL)<0.5); /* intro begins at zero */
    const Audio4Loop *loop=NULL;
    for (size_t i=0;i<AUDIO4_LOOP_COUNT;++i)
        if (!SDL_strcmp(audio4_loops[i].track,"medical_explore")) loop=&audio4_loops[i];
    assert(loop);
    double end=(double)loop->hard_end/loop->rate;
    double start=(double)loop->start/loop->rate;
    assert(Mix_SetMusicPosition(end-0.2)==0);
    SDL_Delay(500);
    ExternalMusicThreat(0,0);
    assert(ExternalMusicUpdate(SOUNDTRACK_REMAKE,1,1,75));
    double after=Mix_GetMusicPosition(NULL);
    assert(after>=start-0.2 && after<start+1.0);
    ExternalMusicShutdown();
    Mix_CloseAudio(); Mix_Quit(); SDL_Quit();
    puts("AUDIO4 real Medical legacy filename/hash/boundary: PASS");
    return 0;
}
