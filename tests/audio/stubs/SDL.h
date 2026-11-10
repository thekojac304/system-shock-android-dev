#ifndef AUDIO5_TEST_SDL_H
#define AUDIO5_TEST_SDL_H
#include <stdint.h>
typedef uint8_t Uint8;
typedef uint64_t Uint64;
Uint64 SDL_GetTicks64(void);
void SDL_Log(const char *format,...);
#endif
