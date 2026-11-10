#ifndef SHOCK_AMBIENT_DIRECTOR_H
#define SHOCK_AMBIENT_DIRECTOR_H
/* Main-thread soundscape controller. Paths are relative to the private files
   directory, which Shockolate makes the process working directory. */
void AmbientDirectorInit(int first_channel);
void AmbientDirectorUpdate(int level, int gameplay, int combat, int enabled, int volume);
void AmbientDirectorSuspend(void);
void AmbientDirectorShutdown(void);
#endif
