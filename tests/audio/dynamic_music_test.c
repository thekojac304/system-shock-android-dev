/* Deterministic game-clock and asynchronous mixer tests of the production owner. */
#define main audio2_regression_main
#include "external_music_test.c"
#undef main

#ifdef USE_SDL_MIXER
static void update(unsigned long deadline, unsigned long now) {
    ExternalMusicThreat(deadline, now);
    assert(ExternalMusicUpdate(SOUNDTRACK_REMAKE, 1, 1, 20));
}
static void finish_fade(void) {
    assert(fade_pending && !paused);
    playing = fade_pending = 0; /* callback completes asynchronously */
}

int main(void) {
    reset();
    variants = 1;
    update(0, 100);
    assert(strstr(last_path, "medical_explore.mp3"));
    assert(explore_probes == 1 && loads == 1 && volume == AudioVolumeScale(20,MIX_MAX_VOLUME));
    assert(reported_position == 0.0 && seek_starts == 0); /* first Explore */
    reported_position = 30.0;
    update(1101, 101); /* sighting -> immediate fade, never a blocking start */
    assert(fade_outs == 1 && plays == 1 && combat_loads == 1);
    int writes = volume_writes, logged = logs;
    for (int i = 0; i < 1000; ++i) update(1101, 102);
    assert(fade_outs == 1 && plays == 1 && combat_loads == 1);
    assert(volume_writes == writes && logs == logged);
    finish_fade();
    update(1101, 103);
    assert(playing_track == &combat_track && fade_ins == 1 && volume == AudioVolumeScale(20,MIX_MAX_VOLUME));
    assert(requested_position == 0.0); /* first Combat */
    reported_position = 18.0;
    update(0, 104); /* death of ONE enemy clears global timer */
    assert(fade_outs == 1 && playing_track == &combat_track);
    update(0, 1100);
    assert(fade_outs == 1); /* retain the original deadline, no added cooldown */
    update(0, 1102);
    assert(fade_outs == 2);
    update(2110, 1110); /* another enemy during return fade */
    assert(fade_outs == 2 && combat_loads == 1);
    finish_fade();
    update(2110, 1111);
    assert(playing_track == &combat_track && plays == 3);
    assert(requested_position == 18.0); /* retarget resumes Combat */

    /* Theme suspension during fade must resume the same fade, never block. */
    update(0, 2111);
    assert(fade_outs == 3);
    ExternalMusicSuspend();
    assert(fade_pending && paused && hook == MusicCallback);
    update(0, 2111);
    assert(!paused && fade_pending && plays == 3);
    finish_fade(); update(0, 2111);
    assert(playing_track == &track && volume == AudioVolumeScale(20,MIX_MAX_VOLUME));
    assert(requested_position == 30.0); /* independent Explore memory */
    for (unsigned long now = 3000; now < 13000; now += 1000) {
        update(now + 500, now);
        finish_fade(); update(now + 500, now + 1);
        update(0, now + 501);
        finish_fade(); update(0, now + 502);
    }
    assert(loads == 1 && combat_loads == 1); /* no decoder reopen accumulation */

    /* Same-deck load clears held combat, no serialized state. */
    update(15000, 14000); finish_fade(); update(15000, 14001);
    ExternalMusicResetThreat(); update(0, 500);
    finish_fade(); update(0, 500);
    assert(playing_track == &track);
    /* A same-deck load resets both decoder positions and reopens Explore. */
    reported_position = 40.0;
    ExternalMusicResetRuntime();
    update(0, 501);
    assert(playing_track == &track && reported_position == 0.0);
    update(1600, 600); finish_fade(); update(1600, 601);
    assert(playing_track == &combat_track && requested_position == 0.0);
    ExternalMusicThreat(1600, 602);
    assert(ExternalMusicUpdate(1, 2, 1, 20));
    assert(playing_track == &track && strstr(last_path, "research_explore"));
    assert(reported_position == 0.0); /* deck reset */
    int old_fades = fade_outs;
    assert(ExternalMusicUpdate(1, 2, 1, 20));
    assert(old_fades == fade_outs); /* inherited old-deck threat ignored */
    ExternalMusicThreat(1700, 700);
    assert(ExternalMusicUpdate(1, 2, 1, 20));
    assert(fade_outs == old_fades + 1);
    assert(!ExternalMusicUpdate(0, 2, 1, 20)); /* teardown DURING fade */
    assert(!fade_pending && !playing && hook == MusicCallback);

    reset(); variants = 1;
    update(0, 100);
    reported_position = -1.0; /* unsupported/invalid Tell */
    update(1101, 101); finish_fade(); update(1101, 102);
    update(0, 1102); finish_fade(); update(0, 1103);
    assert(playing_track == &track && requested_position == 0.0);

    reset(); variants = 1; duration = 60.0;
    update(0, 100);
    reported_position = 58.0; /* temporary final-five-seconds guard */
    update(1101, 101); finish_fade(); update(1101, 102);
    update(0, 1102); finish_fade(); update(0, 1103);
    assert(playing_track == &track && requested_position == 0.0);

    reset(); variants = 1;
    update(0, 100);
    reported_position = 21.5;
    update(1101, 101);
    assert(position_queries == 1 && reported_position == 21.5); /* before fade */
    finish_fade(); update(1101, 102);
    reported_position = 11.25;
    update(0, 1102); finish_fade(); update(0, 1103);
    assert(requested_position == 21.5 && reported_position == 21.5);
    update(2104, 1104); finish_fade(); update(2104, 1105);
    assert(requested_position == 11.25 && reported_position == 11.25);

    reset(); variants = 1;
    update(0, 100); reported_position = 20.0;
    update(1101, 101); finish_fade(); update(1101, 102);
    seek_fail = 1;
    update(0, 1102); finish_fade(); update(0, 1103);
    assert(playing_track == &track && reported_position == 0.0);
    assert(seek_starts >= 2 && playing && !fade_pending);

    reset();
    combat_missing = 1;
    update(0, 100); update(1101, 101);
    for (int i = 0; i < 1000; ++i) update(2000 + i, 200 + i);
    assert(combat_loads == 1 && !fade_outs && plays == 1 && playing_track == &track);
    assert(explore_probes == 1 && loads == 1); /* legacy compatibility */

    reset(); update(0, 100); wrong_type = 1;
    update(1101, 101);
    assert(!fade_outs && playing_track == &track && frees == 1);

    reset(); update(0, 100); combat_play_fail = 1;
    update(1101, 101); finish_fade(); update(1101, 102);
    assert(playing_track == &track && playing && hook == NULL && volume == AudioVolumeScale(20,MIX_MAX_VOLUME));
    update(2100, 1100);
    assert(fade_outs == 1 && combat_loads == 1);

    reset(); update(0, 100); update(1101, 101);
    finish_fade(); update(1101, 102);
    playing = 0; update(1101, 103); /* runtime Combat decode failure */
    assert(playing_track == &track && playing);
    playing = 0; ExternalMusicThreat(1101, 104);
    assert(!ExternalMusicUpdate(1, 1, 1, 20)); /* Explore also fails -> stock */
    assert(!ExternalMusicActive() && hook == MusicCallback);

    reset(); variants = 1;
    ExternalMusicSetDynamic(0);
    update(0,100); update(1101,101);
    assert(playing_track==&track && combat_loads==0 && fade_outs==0);
    ExternalMusicSetDynamic(1);
    update(1101,102);
    assert(combat_loads==1 && fade_outs==1);
    ExternalMusicSetDynamic(0);
    update(1101,103); /* retarget the pending transition */
    finish_fade(); update(1101,104);
    assert(playing_track==&track);
    ExternalMusicSetDynamic(1);

    reset(); variants = 1; load_fail = 1;
    assert(!ExternalMusicUpdate(1, 1, 1, 20));
    assert(loads == 2 && !plays && !hooks); /* both Explore names tried */
    reset();
    ExternalMusicThreat(5000, 1);
    assert(!ExternalMusicUpdate(0, 1, 1, 75));
    assert(!loads && !hooks && !plays);
    puts("AUDIO3 asynchronous transitions / retained deadline / retarget / fallback / lifecycle: PASS");
    return 0;
}
#else
int main(void) {
    ExternalMusicThreat(1000, 1);
    ExternalMusicResetThreat();
    return audio2_regression_main();
}
#endif
