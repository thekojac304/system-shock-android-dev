/* Optional real SDL_mixer smoke test. Run in a scratch working directory
 * containing generated >=12-second MP3 tones at
 * music/remake/{medical,research,reactor}.mp3 and medical_combat.mp3.
 * Delays below simulate callbacks, never game code. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_mixer.h>
#include <assert.h>
#include <stdio.h>
#include "ExternalMusic.h"

struct MusicDevice *MusicDev;
static SDL_atomic_t stock_callbacks, postmix_callbacks;
void MusicCallback(void *data, Uint8 *stream, int len) {
    (void)data;
    SDL_memset(stream, 0, (size_t)len);
    SDL_AtomicAdd(&stock_callbacks, 1);
}
void StopTheMusic(void) { ExternalMusicSuspend(); }
static void postmix(void *data, Uint8 *stream, int len) {
    (void)data; (void)stream; (void)len;
    SDL_AtomicAdd(&postmix_callbacks, 1);
}

static void dynamic_update(unsigned long deadline, unsigned long now) {
    Uint32 start = SDL_GetTicks();
    ExternalMusicThreat(deadline, now);
    assert(ExternalMusicUpdate(1, 1, 1, 20));
    assert(SDL_GetTicks() - start < 250); /* must not wait for the outgoing fade */
}

static void pump_transition(unsigned long deadline, unsigned long now) {
    for (int i = 0; i < 250; ++i) {
        dynamic_update(deadline, now);
        SDL_Delay(10);
    }
    assert(Mix_PlayingMusic() && Mix_FadingMusic() == MIX_NO_FADING);
    assert(Mix_VolumeMusic(-1) == 25); /* current audiolog gain, not default */
}

int main(void) {
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) == 0);
    assert((Mix_Init(MIX_INIT_MP3) & MIX_INIT_MP3) != 0);
    assert(Mix_OpenAudio(48000, AUDIO_S16SYS, 2, 1024) == 0);
    Mix_HookMusic(MusicCallback, &MusicDev);
    Mix_SetPostMix(postmix, NULL);
    /* A separate generated PCM chunk stands in for the existing SFX path. */
    Uint8 pcm[4096] = {0};
    Mix_Chunk *sfx = Mix_QuickLoad_RAW(pcm, sizeof(pcm));
    assert(sfx && Mix_PlayChannel(0, sfx, -1) == 0);
    assert(!ExternalMusicUpdate(0, 1, 1, 75));
    SDL_Delay(60);
    assert(SDL_AtomicGet(&stock_callbacks) > 0);
    assert(ExternalMusicUpdate(1, 1, 1, 75));
    assert(Mix_MusicDuration(NULL) >= 12.0);
    for (int i = 0; i < 100; ++i) {
        assert(ExternalMusicUpdate(1, 1, 1, 75));
        SDL_Delay(10);
    }
    double explore_before = Mix_GetMusicPosition(NULL);
    assert(explore_before > 0.5);
    assert(Mix_PlayingMusic() && Mix_Playing(0));
    /* Real asynchronous fades, pause with fade pending, and re-engagement. */
    dynamic_update(1001, 1);
    assert(Mix_FadingMusic() == MIX_FADING_OUT);
    Mix_PauseAudio(1);
    dynamic_update(1001, 1); /* frozen Android-style device and game clock */
    Mix_PauseAudio(0);
    ExternalMusicSuspend();
    assert(Mix_PausedMusic());
    dynamic_update(1001, 1); /* resume existing outgoing fade */
    pump_transition(1001, 1);
    double combat_before = Mix_GetMusicPosition(NULL);
    assert(combat_before > 0.5); /* first Combat progressed from zero */
    dynamic_update(0, 1002);
    assert(Mix_FadingMusic() == MIX_FADING_OUT);
    dynamic_update(2003, 1003); /* keep Combat when outgoing fade completes */
    pump_transition(2003, 1003);
    assert(Mix_GetMusicPosition(NULL) >= combat_before - 0.25);
    dynamic_update(0, 2004);
    pump_transition(0, 2004);
    assert(Mix_GetMusicPosition(NULL) >= explore_before - 0.25);
    for (unsigned long now = 3000; now < 6000; now += 1000) {
        dynamic_update(now + 500, now);
        pump_transition(now + 500, now);
        dynamic_update(0, now + 501);
        pump_transition(0, now + 501);
    }
    assert(Mix_Playing(0));
    assert(SDL_AtomicGet(&postmix_callbacks) > 10);
    int stock_count = SDL_AtomicGet(&stock_callbacks);
    SDL_Delay(60);
    assert(SDL_AtomicGet(&stock_callbacks) == stock_count);
    ExternalMusicSuspend();
    assert(Mix_PausedMusic());
    SDL_Delay(60);
    assert(SDL_AtomicGet(&stock_callbacks) > stock_count);
    assert(ExternalMusicUpdate(1, 1, 1, 75));
    assert(!Mix_PausedMusic());
    /* SDL device-level pause/resume preserves the stream and SFX channels. */
    Mix_PauseAudio(1);
    SDL_Delay(60);
    Mix_PauseAudio(0);
    assert(ExternalMusicUpdate(1, 1, 1, 75));
    assert(Mix_PlayingMusic() && Mix_Playing(0));
    assert(ExternalMusicUpdate(1, 2, 1, 75));
    assert(ExternalMusicUpdate(1, 0, 1, 75));
    assert(!ExternalMusicUpdate(1, 10, 1, 75));
    assert(!Mix_PlayingMusic() && Mix_Playing(0));
    stock_count = SDL_AtomicGet(&stock_callbacks);
    SDL_Delay(60);
    assert(SDL_AtomicGet(&stock_callbacks) > stock_count);
    /* Missing file must leave the stock callback operating. */
    assert(rename("music/remake/medical.mp3", "music/remake/tone.mp3") == 0);
    assert(!ExternalMusicUpdate(1, 1, 1, 75));
    assert(rename("music/remake/tone.mp3", "music/remake/medical.mp3") == 0);
    ExternalMusicShutdown();
    Mix_HaltChannel(0);
    Mix_FreeChunk(sfx);
    Mix_CloseAudio();
    Mix_Quit();
    SDL_Quit();
    puts("AUDIO3.1 real MP3 / independent position resume / asynchronous fades / ducking / device pause: PASS");
    return 0;
}
