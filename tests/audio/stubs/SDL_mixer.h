#ifndef AUDIO5_TEST_MIXER_H
#define AUDIO5_TEST_MIXER_H
#include "SDL.h"
#define MIX_MAX_VOLUME 128
typedef struct Mix_Chunk { char path[256]; } Mix_Chunk;
Mix_Chunk *Mix_LoadWAV(const char *path);
int Mix_Playing(int channel);
int Mix_HaltChannel(int channel);
int Mix_FadeOutChannel(int channel,int ms);
void Mix_FreeChunk(Mix_Chunk *chunk);
int Mix_SetPanning(int channel,Uint8 left,Uint8 right);
int Mix_Volume(int channel,int volume);
int Mix_FadeInChannel(int channel,Mix_Chunk *chunk,int loops,int ms);
int Mix_PlayChannel(int channel,Mix_Chunk *chunk,int loops);
#endif
