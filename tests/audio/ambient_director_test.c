#define USE_SDL_MIXER 1
#include "../../SourceSnapshot/src/MacSrc/AmbientDirector.c"
#include <assert.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#endif

static Uint64 clock_ms;
static int playing_channel[20], gains[20], loads, starts, halts, logs, frees;
static char last_path[256];
Uint64 SDL_GetTicks64(void) { return clock_ms; }
void SDL_Log(const char *format,...) { (void)format; ++logs; }
Mix_Chunk *Mix_LoadWAV(const char *path) {
    Mix_Chunk *chunk=malloc(sizeof(*chunk)); assert(chunk);
    snprintf(chunk->path,sizeof(chunk->path),"%s",path);
    snprintf(last_path,sizeof(last_path),"%s",path);
    ++loads; return chunk;
}
int Mix_Playing(int channel) { return playing_channel[channel]; }
int Mix_HaltChannel(int channel) { playing_channel[channel]=0; ++halts; return 0; }
int Mix_FadeOutChannel(int channel,int ms) { (void)ms; playing_channel[channel]=0; ++halts; return 1; }
void Mix_FreeChunk(Mix_Chunk *chunk) { ++frees; free(chunk); }
int Mix_SetPanning(int channel,Uint8 left,Uint8 right) { (void)channel;(void)left;(void)right;return 1; }
int Mix_Volume(int channel,int volume) { gains[channel]=volume; return volume; }
int Mix_FadeInChannel(int channel,Mix_Chunk *chunk,int loops,int ms) {
    (void)chunk;(void)loops;(void)ms; playing_channel[channel]=1;++starts;return channel;
}
int Mix_PlayChannel(int channel,Mix_Chunk *chunk,int loops) {
    (void)chunk;(void)loops;playing_channel[channel]=1;++starts;return channel;
}
static void directory(const char *path) {
#ifdef _WIN32
    assert(_mkdir(path)==0);
#else
    assert(mkdir(path,0700)==0);
#endif
}
static void file(const char *path) { FILE *f=fopen(path,"wb");assert(f);fputc(1,f);fclose(f); }
int main(void) {
    AmbientDirectorInit(12);
    AmbientDirectorUpdate(1,1,0,1,70); /* nonexistent tree is normal */
    assert(asset_count==0 && starts==0 && logs==1);
    AmbientDirectorUpdate(1,1,0,1,70);
    assert(logs==1);
    AmbientDirectorShutdown();
    directory("audio");directory("audio/ambient");directory("audio/ambient/generic");
    directory("audio/ambient/generic/ventilation");
    AmbientDirectorInit(12);
    AmbientDirectorUpdate(1,1,0,1,70); /* empty tree */
    assert(asset_count==0 && starts==0);
    AmbientDirectorShutdown();
    file("audio/ambient/generic/ventilation/a.wav");
    file("audio/ambient/generic/ventilation/loop_air.wav");
    directory("audio/ambient/medical");directory("audio/ambient/medical/ventilation");
    file("audio/ambient/medical/ventilation/b.wav");
    file("audio/ambient/medical/ventilation/c.wav");
    AmbientDirectorInit(12); clock_ms=1000;
    AmbientDirectorUpdate(1,1,0,1,70);
    assert(asset_count==4 && playing_channel[12] && strstr(last_path,"loop_air.wav"));
    clock_ms=14000; AmbientDirectorUpdate(1,1,0,1,70);
    assert(playing_channel[13] && strstr(last_path,"medical/ventilation/"));
    assert(choose_asset(1,0,clock_ms+100000)!=last_asset); /* alternate available */
    int first_gain=gains[12];
    AmbientDirectorUpdate(1,1,0,1,30);
    assert(gains[12]<first_gain && gains[13]<42);
    int before=starts;
    clock_ms=25000; playing_channel[13]=0;
    AmbientDirectorUpdate(1,1,0,1,70);
    assert(starts==before); /* interval and source cooldown */
    AmbientDirectorSuspend(); assert(!playing_channel[12] && !playing_channel[13]);
    before=starts;clock_ms=500000;
    AmbientDirectorUpdate(1,1,0,1,70);
    assert(starts==before+1 && !playing_channel[13]); /* no backlog */
    before=halts;
    AmbientDirectorUpdate(2,1,0,1,70);
    assert(halts>before && !playing_channel[13] && current_deck==2);
    AmbientDirectorUpdate(1,1,0,0,70);
    assert(!playing_channel[12] && !playing_channel[13]);
    AmbientDirectorShutdown();
    puts("AMBIENT_DIRECTOR_TESTS=PASS (empty, discovery, override, gain, cooldown, lifecycle, off)");
}
