#ifndef SHOCK_EXTERNAL_MUSIC_H
#define SHOCK_EXTERNAL_MUSIC_H

/* Main/game thread only. No gameplay state is owned by this module. */
enum { SOUNDTRACK_ORIGINAL, SOUNDTRACK_REMAKE };
int ExternalMusicMode(const char *name);
const char *ExternalMusicModeName(int mode);
const char *ExternalMusicPath(int mode, int level);
int ExternalMusicUpdate(int mode, int level, int gameplay, int volume);
/* Stock combat deadline and game clock (same tick units). No saved state. */
void ExternalMusicThreat(unsigned long combat_until, unsigned long game_time);
void ExternalMusicResetThreat(void);
/* New game/load runtime boundary; playback positions never enter the save. */
void ExternalMusicResetRuntime(void);
/* Optional external Explore-only breathing room; default disabled. */
void ExternalMusicSetBreaks(int enabled);
void ExternalMusicSetDynamic(int enabled);
#ifdef AUDIO4_TEST_TIMING
void ExternalMusicTestTiming(unsigned music_ticks, unsigned rest_ticks);
void ExternalMusicTestLoop(int combat, unsigned rate, unsigned long long start, unsigned long long hard_end);
#endif
int ExternalMusicActive(void);
void ExternalMusicSuspend(void);
void ExternalMusicVolume(int volume);
void ExternalMusicShutdown(void);

#endif
