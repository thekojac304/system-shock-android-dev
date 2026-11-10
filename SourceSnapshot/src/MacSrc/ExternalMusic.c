#include "ExternalMusic.h"
#include "AudioVolumeCurve.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* A pack owns its deck table; playback owns only the selected entry. Additional
 * packs and (later) per-entry loop/intensity metadata belong here, not in the
 * lifecycle controller. Paths are relative to Android's private files/. */
typedef struct {
    int level;
    const char *path;
} ExternalMusicEntry;
typedef struct {
    const char *name;
    const ExternalMusicEntry *entries;
    size_t count;
} ExternalMusicPack;

static const ExternalMusicEntry remake[] = {
    {0, "music/remake/reactor.mp3"},
    {1, "music/remake/medical.mp3"},
    {2, "music/remake/research.mp3"},
    {3, "music/remake/maintenance.mp3"},
    {4, "music/remake/storage.mp3"},
    {5, "music/remake/flightdeck.mp3"},
    {6, "music/remake/executive.mp3"},
    {7, "music/remake/engineering.mp3"},
    {8, "music/remake/security.mp3"},
    {9, "music/remake/bridge.mp3"},
    {11, "music/remake/groves.mp3"}, /* Delta */
    {12, "music/remake/groves.mp3"}, /* Alpha */
    {13, "music/remake/groves.mp3"}  /* Beta */
};
static const ExternalMusicPack packs[] = {
    {"original", NULL, 0},
    {"remake", remake, sizeof(remake) / sizeof(remake[0])}
};

static int valid_mode(int mode) {
    return mode >= 0 && (size_t)mode < sizeof(packs) / sizeof(packs[0]) ? mode : SOUNDTRACK_ORIGINAL;
}
int ExternalMusicMode(const char *name) {
    if (name) for (size_t i = 0; i < sizeof(packs) / sizeof(packs[0]); ++i) {
        const unsigned char *a = (const unsigned char *)name;
        const char *b = packs[i].name;
        while (*a && *b && tolower(*a) == *b) { ++a; ++b; }
        if (!*a && !*b) return (int)i;
    }
    return SOUNDTRACK_ORIGINAL;
}
const char *ExternalMusicModeName(int mode) { return packs[valid_mode(mode)].name; }
const char *ExternalMusicPath(int mode, int level) {
    const ExternalMusicPack *pack = &packs[valid_mode(mode)];
    for (size_t i = 0; i < pack->count; ++i)
        if (pack->entries[i].level == level) return pack->entries[i].path;
    return NULL;
}

#ifdef USE_SDL_MIXER
#include <SDL.h>
#include <SDL_mixer.h>
#include <stdlib.h>
#include "Audio4Hash.h"
#include "Audio4Loops.h"

enum { AUDIO3_TRANSITION_FADE_MS = 1000 };
enum { AUDIO4_REST_FADE_MS = 2000, AUDIO4_TICKS_PER_SECOND = 280 };

static const char *selected_path;
static int selected_mode = -1, selected_level = -1;
static Mix_Music *external_music;
typedef enum { EXPLORE, COMBAT } ExternalMusicState;
static Mix_Music *tracks[2];
static const Audio4Loop *loops[2];
typedef struct {
    int started, valid;
    double position;
} PlaybackMemory;
static PlaybackMemory memory[2];
static char paths[2][128];
static int combat_failed, fading;
static int breaks_enabled, resting, rest_fading;
static int dynamic_enabled = 1;
static unsigned long music_elapsed, rest_elapsed, music_target, rest_target;
static uint32_t audio_rng = 0x89abcdefu;
static int audio_rng_seeded;
#ifdef AUDIO4_TEST_TIMING
static unsigned test_music_ticks, test_rest_ticks;
static Audio4Loop test_loops[2];
void ExternalMusicTestTiming(unsigned music_ticks, unsigned rest_ticks) {
    test_music_ticks = music_ticks; test_rest_ticks = rest_ticks;
}
void ExternalMusicTestLoop(int combat, unsigned rate, unsigned long long start, unsigned long long hard_end) {
    Audio4Loop *loop = &test_loops[combat ? COMBAT : EXPLORE];
    loop->track = combat ? "medical_combat" : "medical_explore";
    loop->rate = rate; loop->start = start; loop->hard_end = loop->end = hard_end;
    loop->fade_ms = 0;
}
#endif
static ExternalMusicState state, requested;
static unsigned long threat_until, game_time, last_input, held_until, last_time;
static int applied_volume = -1;
static int active;
static int failed;
static int cleanup_registered;
static void fallback(const char *reason);

struct MusicDevice;
extern struct MusicDevice *MusicDev;
extern void MusicCallback(void *userdata, Uint8 *stream, int len);
extern void StopTheMusic(void);

int ExternalMusicActive(void) { return active; }

void ExternalMusicThreat(unsigned long combat_until, unsigned long now) {
    threat_until = combat_until;
    game_time = now;
}

void ExternalMusicResetThreat(void) {
    threat_until = last_input = held_until = last_time = 0;
}

static const char *state_name(ExternalMusicState s) { return s == COMBAT ? "COMBAT" : "EXPLORE"; }

static unsigned long audio4_range(unsigned low_seconds, unsigned high_seconds) {
    if (!audio_rng_seeded) {
#ifndef AUDIO4_TEST_TIMING
        audio_rng ^= SDL_GetTicks();
#endif
        if (!audio_rng) audio_rng = 0x89abcdefu;
        audio_rng_seeded = 1;
    }
    audio_rng ^= audio_rng << 13; audio_rng ^= audio_rng >> 17; audio_rng ^= audio_rng << 5;
    return (unsigned long)(low_seconds + audio_rng % (high_seconds-low_seconds+1)) * AUDIO4_TICKS_PER_SECOND;
}
static void schedule_rest(const char *reason) {
    music_elapsed = rest_elapsed = 0;
    resting = rest_fading = 0;
#ifdef AUDIO4_QA_FAST_BREAKS
    music_target = audio4_range(30, 45);
#else
    music_target = audio4_range(360, 600);
#endif
#ifdef AUDIO4_TEST_TIMING
    if (test_music_ticks) music_target = test_music_ticks;
#endif
    SDL_Log("AUDIO4 rest schedule reset: reason=%s next_music_duration=%lus", reason,
            music_target/AUDIO4_TICKS_PER_SECOND);
}
void ExternalMusicSetBreaks(int enabled) {
    enabled = !!enabled;
    if (enabled == breaks_enabled) return;
    int was_rest = resting || rest_fading;
    breaks_enabled = enabled;
    schedule_rest(enabled ? "enabled" : "disabled");
    if (!enabled && was_rest && active && state == EXPLORE) {
        memory[EXPLORE].valid = 0;
        Mix_HaltMusic();
        if (Mix_FadeInMusic(external_music,loops[EXPLORE] ? 0 : -1,AUDIO4_REST_FADE_MS) < 0)
            fallback("Explore restart after disabling breaks failed");
    }
}
void ExternalMusicSetDynamic(int enabled) { dynamic_enabled=!!enabled; }
static int audio4_matches_filename(const char *base, size_t len, const char *track) {
    size_t stem = strlen(track);
    if (len == stem+4 && !strncmp(base,track,stem)) return 1;
    /* The original AUDIO2 Explore pack uses deck.mp3, not deck_explore.mp3.
     * The SHA check below still decides whether that legacy file is eligible. */
    return stem == len+4 && !strncmp(base,track,len-4) &&
           !strcmp(track+len-4,"_explore");
}
static const Audio4Loop *verified_loop(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base+1 : path;
    size_t len = strlen(base);
    if (len < 5 || strcmp(base+len-4,".mp3")) return NULL;
#ifdef AUDIO4_TEST_TIMING
    for (int state_index=0; state_index<2; ++state_index) {
        const Audio4Loop *test = &test_loops[state_index];
        if (test->rate && audio4_matches_filename(base,len,test->track)) return test;
    }
#endif
    for (size_t i=0; i<AUDIO4_LOOP_COUNT; ++i) {
        const Audio4Loop *entry = &audio4_loops[i];
        if (!audio4_matches_filename(base,len,entry->track)) continue;
        char digest[65];
        if (!audio4_sha256_file(path,digest) || strcmp(digest,entry->sha256)) {
            SDL_Log("AUDIO4 source hash mismatch: track=%s path=%s; legacy whole-file playback",entry->track,path);
            return NULL;
        }
        SDL_Log("AUDIO4 track metadata loaded: track=%s start=%llu hard_end=%llu end=%llu fade=%ums",
                entry->track,(unsigned long long)entry->start,(unsigned long long)entry->hard_end,
                (unsigned long long)entry->end,entry->fade_ms);
        return entry;
    }
    SDL_Log("AUDIO4 metadata unavailable: path=%s; legacy whole-file playback",path);
    return NULL;
}
static double loop_start(const Audio4Loop *loop) { return (double)loop->start/loop->rate; }
static double loop_end(const Audio4Loop *loop) { return (double)loop->hard_end/loop->rate; }
static int apply_loop_boundary(void) {
    const Audio4Loop *loop = loops[state];
    if (!loop || !Mix_PlayingMusic()) return 1;
    double position = Mix_GetMusicPosition(external_music);
    if (position < 0 || position < loop_end(loop)) return 1;
    double start = loop_start(loop);
    SDL_Log("AUDIO4 loop boundary reached: track=%s reported=%.3f; seek to loop_start=%.3f",
            loop->track,position,start);
    if (Mix_SetMusicPosition(start) == 0) return 1;
    SDL_Log("AUDIO4 loop seek failed: track=%s error=%s; falling back to whole file",loop->track,Mix_GetError());
    loops[state] = NULL;
    Mix_HaltMusic();
    return Mix_PlayMusic(external_music,-1) == 0;
}

/* minimp3 reports decoded samples, which may lead the audible output by its
 * buffers. Reject unsupported or implausible readings rather than recycling
 * an old timestamp after a failed query. */
static void remember_position(void) {
    double pos = Mix_GetMusicPosition(external_music);
    double duration = Mix_MusicDuration(external_music);
    PlaybackMemory *m = &memory[state];
    m->valid = 0;
    if (!(pos >= 0.0 && pos < 86400.0) ||
        (duration > 0.0 && pos > duration + 1.0)) {
        SDL_Log("AUDIO3.1 invalid position: deck=%d state=%s pos=%.3f duration=%.3f",
                selected_level, state_name(state), pos, duration);
        return;
    }
    m->position = pos;
    m->valid = 1;
    SDL_Log("AUDIO3.1 save position: deck=%d state=%s pos=%.3f duration=%.3f",
            selected_level, state_name(state), pos, duration);
}

static double resume_position(ExternalMusicState next) {
    PlaybackMemory *m = &memory[next];
    if (!m->started || !m->valid) return 0.0;
    double duration = Mix_MusicDuration(tracks[next]);
    const Audio4Loop *loop = loops[next];
    if (loop && m->position >= loop_end(loop)) {
        double start = loop_start(loop), period = loop_end(loop)-start;
        uint64_t cycles = (uint64_t)((m->position-start)/period);
        double normalized = m->position-(double)cycles*period;
        SDL_Log("AUDIO4 resume normalized: track=%s saved=%.3f normalized=%.3f",loop->track,m->position,normalized);
        return normalized;
    }
    /* Temporary until authored loop metadata exists: restart if fewer than
     * five seconds remain. Unknown durations get no speculative guard. */
    if (!loop && duration > 0.0 && m->position >= duration - 5.0) {
        SDL_Log("AUDIO3.1 near-end guard: deck=%d state=%s saved=%.3f duration=%.3f restart=0",
                selected_level, state_name(next), m->position, duration);
        m->valid = 0;
        return 0.0;
    }
    return m->position;
}

void ExternalMusicVolume(int volume) {
    if (active) {
        if (volume < 0) volume = 0;
        if (volume > 100) volume = 100;
        /* Repeated writes defeat SDL_mixer's fade envelope. Real duck/slider
         * changes still take effect immediately, including during a fade. */
        volume = AudioVolumeScale(volume, MIX_MAX_VOLUME);
        if (volume != applied_volume) {
            Mix_VolumeMusic(volume);
            applied_volume = volume;
        }
    }
}

void ExternalMusicSuspend(void) {
    if (!active) return;
    if (external_music && Mix_PlayingMusic())
        SDL_Log("AUDIO3.1 suspend: deck=%d state=%s reported=%.3f",
                selected_level, state_name(state), Mix_GetMusicPosition(external_music));
    Mix_PauseMusic();
    /* Restore the synth hook for menu/death/cutscene music. Keep the decoder
     * position so a same-deck theme reload cannot restart the long track. */
    Mix_HookMusic(MusicCallback, &MusicDev);
    Mix_VolumeMusic(MIX_MAX_VOLUME);
    applied_volume = -1;
    active = 0;
    SDL_Log("ExternalMusic suspend: %s", selected_path);
}

void ExternalMusicShutdown(void) {
    ExternalMusicSuspend();
    if (tracks[EXPLORE] || tracks[COMBAT]) {
        /* Halt first: FreeMusic otherwise waits on an active outgoing fade. */
        Mix_HaltMusic();
        for (int i = 0; i < 2; ++i) {
            if (tracks[i]) Mix_FreeMusic(tracks[i]);
            tracks[i] = NULL;
            loops[i] = NULL;
        }
        external_music = NULL;
        SDL_Log("ExternalMusic stop: %s", selected_path);
    }
    fading = 0;
    schedule_rest("deck/load");
    memset(memory, 0, sizeof(memory));
}

void ExternalMusicResetRuntime(void) {
    ExternalMusicShutdown();
    ExternalMusicResetThreat();
    SDL_Log("AUDIO3.1 load/runtime reset: deck=%d", selected_level);
}

static void fallback(const char *reason) {
    SDL_Log("ExternalMusic fallback: level=%d path=%s: %s", selected_level, selected_path, reason);
    ExternalMusicShutdown();
    /* Retry on the next deck visit or process restart, never every frame. */
    failed = 1;
}

static Mix_Music *load_mp3(const char *path) {
    Mix_Music *music = Mix_LoadMUS(path);
    if (!music) {
        SDL_Log("ExternalMusic unavailable: level=%d path=%s: %s", selected_level, path, Mix_GetError());
    } else if (Mix_GetMusicType(music) != MUS_MP3) {
        SDL_Log("ExternalMusic unavailable: level=%d path=%s: MP3 required", selected_level, path);
        Mix_FreeMusic(music); /* not the playing object */
        music = NULL;
    }
    return music;
}

static int start_state(ExternalMusicState next, int volume, int fade_in) {
    /* Also clears a stopped decoder's residual fading flag before Play/FadeIn. */
    Mix_HaltMusic();
    state = next;
    external_music = tracks[state];
    selected_path = paths[state];
    applied_volume = -1;
    ExternalMusicVolume(volume);
    double position = resume_position(next);
    if (position > 0.0)
        SDL_Log("AUDIO3.1 resume: deck=%d state=%s saved=%.3f requested=%.3f",
                selected_level, state_name(next), memory[next].position, position);
    int repeats = loops[next] ? 0 : -1;
    int result = fade_in ? Mix_FadeInMusicPos(external_music, repeats, AUDIO3_TRANSITION_FADE_MS, position)
                         : (position > 0.0 ? Mix_FadeInMusicPos(external_music,repeats,0,position) : Mix_PlayMusic(external_music,repeats));
    if (result < 0 && position > 0.0) {
        SDL_Log("AUDIO3.1 seek failure: deck=%d state=%s requested=%.3f error=%s; restart=0",
                selected_level, state_name(next), position, Mix_GetError());
        memory[next].valid = 0;
        result = fade_in ? Mix_FadeInMusic(external_music, repeats, AUDIO3_TRANSITION_FADE_MS)
                         : Mix_PlayMusic(external_music, repeats);
    }
    if (result < 0) {
        SDL_Log("ExternalMusic play failure: level=%d state=%s path=%s: %s",
                selected_level, state_name(state), selected_path, Mix_GetError());
        if (state == COMBAT) {
            combat_failed = 1;
            Mix_HaltMusic();
            return start_state(EXPLORE, volume, 1);
        }
        fallback("Explore playback failed; restoring stock music");
        return 0;
    }
    memory[next].started = 1;
    if (position > 0.0 && memory[next].valid) {
        double reported = Mix_GetMusicPosition(external_music);
        SDL_Log("AUDIO3.1 resumed: deck=%d state=%s requested=%.3f reported=%.3f delta=%+.3f",
                selected_level, state_name(next), position, reported, reported - position);
    }
    SDL_Log("ExternalMusic start: level=%d state=%s path=%s (%s)",
            selected_level, state_name(state), selected_path,loops[state] ? "AUDIO4 region loop" : "full-track loop");
    return 1;
}

int ExternalMusicUpdate(int mode, int level, int gameplay, int volume) {
    mode = valid_mode(mode);
    if (mode != selected_mode || level != selected_level) {
        int previous_level = selected_level;
        const char *next_path = ExternalMusicPath(mode, level);
        if (mode != selected_mode)
            SDL_Log("ExternalMusic mode: %s", ExternalMusicModeName(mode));
        if (selected_path && next_path && external_music)
            SDL_Log("ExternalMusic switch: level=%d -> %d, %s -> %s",
                    selected_level, level, selected_path, next_path);
        ExternalMusicShutdown();
        if (previous_level >= 0 && level != previous_level)
            SDL_Log("AUDIO3.1 deck reset: from=%d to=%d", previous_level, level);
        selected_mode = mode;
        selected_level = level;
        selected_path = next_path;
        failed = 0;
        combat_failed = 0;
        state = requested = EXPLORE;
        held_until = 0;
        /* A level change may retain the previous deck's global deadline.
         * Wait for a fresh sighting/attack refresh on the destination. */
        last_input = threat_until;
        last_time = game_time;
        if (next_path) {
            size_t stem = strlen(next_path) - 4; /* registry paths end in .mp3 */
            snprintf(paths[EXPLORE], sizeof(paths[EXPLORE]), "%.*s_explore.mp3", (int)stem, next_path);
            snprintf(paths[COMBAT], sizeof(paths[COMBAT]), "%.*s_combat.mp3", (int)stem, next_path);
        }
        if (mode != SOUNDTRACK_ORIGINAL)
            SDL_Log("ExternalMusic mapping: level=%d -> %s", level, selected_path ? selected_path : "stock");
    }
    if (!selected_path) return 0;
    if (!gameplay) {
        last_time = game_time;
        ExternalMusicSuspend();
        return 0;
    }
    if (failed) return 0;
    if (!external_music) {
        /* Do not suppress stock music until the file has opened successfully. */
        tracks[EXPLORE] = load_mp3(paths[EXPLORE]);
        if (!tracks[EXPLORE]) {
            snprintf(paths[EXPLORE], sizeof(paths[EXPLORE]), "%s", ExternalMusicPath(mode, level));
            tracks[EXPLORE] = load_mp3(paths[EXPLORE]);
        }
        if (!tracks[EXPLORE]) {
            fallback("no usable Explore variant or legacy track; restoring stock music");
            return 0;
        }
        loops[EXPLORE] = verified_loop(paths[EXPLORE]);
        if (!cleanup_registered) {
            atexit(ExternalMusicShutdown);
            cleanup_registered = 1;
        }
        StopTheMusic();
        /* A custom music hook replaces SDL_mixer's stream decoder. Unhook it
         * only for external playback; SFX and the auxiliary postmix remain. */
        Mix_HookMusic(NULL, NULL);
        active = 1;
        /* Load/new-game can keep the same deck while rewinding the runtime
         * clock. Never charge that gap to the first Explore interval. */
        last_time = game_time;
        return start_state(EXPLORE, volume, 0);
    } else if (!active) {
        last_time = game_time;
        StopTheMusic();
        Mix_HookMusic(NULL, NULL);
        active = 1;
        ExternalMusicVolume(volume);
        Mix_ResumeMusic();
        SDL_Log("AUDIO3.1 lifecycle resume: deck=%d state=%s reported=%.3f",
                selected_level, state_name(state), Mix_GetMusicPosition(external_music));
        SDL_Log("ExternalMusic resume: %s", selected_path);
    }

    unsigned long elapsed = game_time >= last_time ? game_time-last_time : 0;
    if (game_time < last_time) held_until = 0; /* defensive clock rewind */
    last_time = game_time;
    if (threat_until != last_input) {
        if (threat_until > game_time) held_until = threat_until;
        last_input = threat_until;
    }
    ExternalMusicState next = dynamic_enabled && held_until > game_time ? COMBAT : EXPLORE;
    if (next != requested) {
        SDL_Log("ExternalMusic request: level=%d %s -> %s source=stock-combat deadline=%lu held=%lu now=%lu%s",
                level, state_name(requested), state_name(next), threat_until, held_until, game_time,
                fading ? " (retarget pending fade)" : "");
        requested = next;
    }
    if (next == COMBAT && !tracks[COMBAT] && !combat_failed) {
        tracks[COMBAT] = load_mp3(paths[COMBAT]);
        if (tracks[COMBAT]) loops[COMBAT] = verified_loop(paths[COMBAT]);
        if (!tracks[COMBAT]) {
            combat_failed = 1;
            SDL_Log("ExternalMusic Combat fallback: level=%d continue Explore for this visit", level);
        }
    }
    if (combat_failed) next = EXPLORE;
    ExternalMusicVolume(volume);
    if (resting || rest_fading) {
        if (next == COMBAT) {
            SDL_Log("AUDIO4 explore rest interrupted by combat");
            memory[EXPLORE].valid = 0;
            Mix_HaltMusic();
            schedule_rest("combat");
            return start_state(COMBAT,volume,1);
        }
        if (rest_fading && !Mix_PlayingMusic()) {
            rest_fading = 0;
            resting = 1;
            rest_elapsed = 0;
#ifdef AUDIO4_QA_FAST_BREAKS
            rest_target = audio4_range(15,25);
#else
            rest_target = audio4_range(60,150);
#endif
#ifdef AUDIO4_TEST_TIMING
            if (test_rest_ticks) rest_target = test_rest_ticks;
#endif
            SDL_Log("AUDIO4 explore rest begin: duration=%lus",rest_target/AUDIO4_TICKS_PER_SECOND);
        }
        if (resting) {
            rest_elapsed += elapsed;
            if (rest_elapsed >= rest_target) {
                SDL_Log("AUDIO4 explore rest complete: restart Explore at 0");
                memory[EXPLORE].valid = 0;
                schedule_rest("rest-complete");
                Mix_HaltMusic();
                /* Deliberate rest return uses a slower fade than combat switching. */
                state = EXPLORE; external_music = tracks[EXPLORE];
                applied_volume = -1; ExternalMusicVolume(volume);
                if (Mix_FadeInMusic(external_music,loops[EXPLORE] ? 0 : -1,AUDIO4_REST_FADE_MS) < 0) {
                    fallback("Explore restart after rest failed"); return 0;
                }
                memory[EXPLORE].started = 1;
            }
            last_time = game_time;
            return 1;
        }
        last_time = game_time;
        return 1;
    }
    if (fading) {
        if (Mix_PlayingMusic()) return 1;
        fading = 0;
        SDL_Log("ExternalMusic fade complete: level=%d target=%s", level, state_name(next));
        return start_state(next, volume, 1);
    }
    if (!Mix_PlayingMusic()) {
        if (state == COMBAT) {
            combat_failed = 1;
            SDL_Log("ExternalMusic Combat stream stopped: level=%d restoring Explore", level);
            Mix_HaltMusic();
            return start_state(EXPLORE, volume, 1);
        }
        fallback("Explore stream stopped unexpectedly; restoring stock music");
        return 0;
    }
    if (next != state) {
        if (next == COMBAT) schedule_rest("combat");
        else if (state == COMBAT) schedule_rest("combat-end");
        remember_position();
        SDL_Log("ExternalMusic fade start: level=%d %s -> %s path=%s duration=%dms",
                level, state_name(state), state_name(next), paths[next], AUDIO3_TRANSITION_FADE_MS);
        fading = 1;
        if (!Mix_FadeOutMusic(AUDIO3_TRANSITION_FADE_MS)) {
            Mix_HaltMusic();
            fading = 0;
            return start_state(next, volume, 1);
        }
    }
    if (!fading && !apply_loop_boundary()) {
        fallback("loop seek and whole-file fallback failed"); return 0;
    }
    if (breaks_enabled && state == EXPLORE && next == EXPLORE && !fading) {
        /* Game time advances only in gameplay. Suspend/menu/background paths
         * returned above and refresh last_time without consuming this budget. */
        music_elapsed += elapsed;
        if (music_elapsed >= music_target) {
            SDL_Log("AUDIO4 explore rest fade start: duration=%dms",AUDIO4_REST_FADE_MS);
            rest_fading = 1;
            if (!Mix_FadeOutMusic(AUDIO4_REST_FADE_MS)) Mix_HaltMusic();
        }
    }
    last_time = game_time;
    return 1;
}
#else
void ExternalMusicSetBreaks(int enabled) { (void)enabled; }
void ExternalMusicSetDynamic(int enabled) { (void)enabled; }
#ifdef AUDIO4_TEST_TIMING
void ExternalMusicTestTiming(unsigned music_ticks, unsigned rest_ticks) { (void)music_ticks; (void)rest_ticks; }
void ExternalMusicTestLoop(int combat, unsigned rate, unsigned long long start, unsigned long long hard_end) {
    (void)combat; (void)rate; (void)start; (void)hard_end;
}
#endif
void ExternalMusicThreat(unsigned long combat_until, unsigned long game_time) {
    (void)combat_until; (void)game_time;
}
void ExternalMusicResetThreat(void) {}
void ExternalMusicResetRuntime(void) {}
int ExternalMusicUpdate(int enabled, int level, int gameplay, int volume) {
    (void)enabled; (void)level; (void)gameplay; (void)volume;
    return 0;
}
int ExternalMusicActive(void) { return 0; }
void ExternalMusicSuspend(void) {}
void ExternalMusicVolume(int volume) { (void)volume; }
void ExternalMusicShutdown(void) {}
#endif
