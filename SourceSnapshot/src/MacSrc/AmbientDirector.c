#include "AmbientDirector.h"
#include "AudioVolumeCurve.h"
#ifdef USE_SDL_MIXER
#include <SDL.h>
#include <SDL_mixer.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CATEGORIES=13, DECKS=11, MAX_ASSETS=256, LAYER=0, SHOT=1 };
static const char *categories[CATEGORIES]={
    "ventilation","electrical","machinery_light","machinery_heavy",
    "computer","steam","hydraulic","metal_creak","structural",
    "distant_impact","distant_mechanical","organic","environmental_drone"
};
static const char *decks[DECKS]={
    "reactor","medical","research","maintenance","storage","flightdeck",
    "executive","engineering","security","bridge","groves"
};
/* Initial authoring weights, not a final listening mix. Index follows decks. */
static const uint8_t weights[DECKS][CATEGORIES]={
    {1,4,1,5,1,3,2,1,3,2,3,0,3}, /* Reactor */
    {5,3,3,0,2,0,1,1,1,0,1,0,2}, /* Medical */
    {3,4,3,0,5,1,1,1,1,1,2,0,2}, /* Research */
    {4,3,4,3,1,3,3,3,3,2,3,0,2}, /* Maintenance */
    {3,2,2,2,1,1,2,3,3,3,2,0,2}, /* Storage */
    {4,3,3,3,2,1,3,2,2,2,3,0,2}, /* Flight Deck */
    {3,2,2,0,4,0,1,1,1,1,1,0,3}, /* Executive */
    {1,4,2,5,2,3,4,3,4,3,4,0,3}, /* Engineering */
    {3,4,3,2,4,1,2,3,3,3,3,0,3}, /* Security */
    {2,4,2,2,5,1,2,2,3,3,4,0,4}, /* Bridge */
    {1,0,1,0,0,0,0,0,0,0,0,5,5}  /* Groves */
};
typedef struct { char path[256]; uint8_t deck, category, loop, bad; uint64_t last_ms; } Asset;
static Asset assets[MAX_ASSETS];
static int asset_count, indexed, channel_base=-1, current_deck=-1, last_asset=-1;
static Mix_Chunk *playing[2];
static int playing_asset[2]={-1,-1};
static int slot_gain[2]; /* bounded local gain, applied with category volume */
static uint64_t next_ms, category_last[CATEGORIES];
static uint32_t rng=0x6a09e667u;

static uint32_t random_u32(void) {
    rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; return rng;
}
static int deck_index(int level) {
    if (level>=0 && level<=9) return level;
    if (level>=11 && level<=13) return 10;
    return -1;
}
static int asset_compare(const void *a,const void *b) {
    return strcmp(((const Asset *)a)->path,((const Asset *)b)->path);
}
static int supported(const char *name) {
    const char *dot=strrchr(name,'.');
    return dot && (!strcasecmp(dot,".wav") || !strcasecmp(dot,".mp3"));
}
static void scan_category(int deck,int category) {
    char folder[192];
    snprintf(folder,sizeof(folder),"audio/ambient/%s/%s",deck<0?"generic":decks[deck],categories[category]);
    DIR *dir=opendir(folder);
    if (!dir) return;
    struct dirent *entry;
    while ((entry=readdir(dir)) && asset_count<MAX_ASSETS) {
        if (entry->d_name[0]=='.' || !supported(entry->d_name)) continue;
        Asset *asset=&assets[asset_count];
        int n=snprintf(asset->path,sizeof(asset->path),"%s/%s",folder,entry->d_name);
        if (n<0 || (size_t)n>=sizeof(asset->path)) continue;
        struct stat info;
        if (stat(asset->path,&info) || !S_ISREG(info.st_mode) || info.st_size<=0) continue;
        asset->deck=(uint8_t)(deck+1); /* zero means generic */
        asset->category=(uint8_t)category;
        asset->loop=!strncmp(entry->d_name,"loop_",5) || !strncmp(entry->d_name,"layer_",6);
        asset->bad=0;
        asset->last_ms=0;
        ++asset_count;
    }
    closedir(dir);
}
static void index_assets(void) {
    if (indexed) return;
    indexed=1;
    for (int category=0;category<CATEGORIES;++category) {
        scan_category(-1,category);
        for (int deck=0;deck<DECKS;++deck) scan_category(deck,category);
    }
    qsort(assets,(size_t)asset_count,sizeof(assets[0]),asset_compare);
    SDL_Log("AUDIO5 ambient index: %d assets%s",asset_count,
            asset_count ? "" : " (empty is supported)");
}
static void halt_slot(int slot,int fade_ms) {
    if (channel_base<0) return;
    int channel=channel_base+slot;
    if (!Mix_Playing(channel)) return;
    if (fade_ms>0) Mix_FadeOutChannel(channel,fade_ms);
    else Mix_HaltChannel(channel);
}
static void reap_slot(int slot) {
    if (!playing[slot] || channel_base<0 || Mix_Playing(channel_base+slot)) return;
    Mix_FreeChunk(playing[slot]);
    playing[slot]=NULL;
    playing_asset[slot]=-1;
}
static void stop_all(void) {
    for (int slot=0;slot<2;++slot) {
        halt_slot(slot,0);
        reap_slot(slot);
    }
}
void AmbientDirectorInit(int first_channel) {
    stop_all();
    channel_base=first_channel;
    current_deck=-1;
    next_ms=0;
}
void AmbientDirectorSuspend(void) {
    stop_all();
    next_ms=0; /* no queued events or catch-up burst after resume */
}
void AmbientDirectorShutdown(void) {
    AmbientDirectorSuspend();
    channel_base=-1;
    current_deck=-1;
    indexed=0; asset_count=0; last_asset=-1;
    memset(category_last,0,sizeof(category_last));
}
static int choose_asset(int deck,int loop,uint64_t now) {
    unsigned total=0;
    int eligible[MAX_ASSETS], count=0;
    int last_candidate=-1;
    for (int category=0;category<CATEGORIES;++category) {
        if (!weights[deck][category] || (!loop && category_last[category] && now-category_last[category]<30000)) continue;
        int has_deck=0;
        for (int i=0;i<asset_count;++i)
            if (!assets[i].bad && assets[i].category==category && assets[i].deck==deck+1 && assets[i].loop==loop) has_deck=1;
        for (int i=0;i<asset_count;++i) {
            Asset *a=&assets[i];
            if (a->bad || a->category!=category || a->loop!=loop || a->deck!=(has_deck?deck+1:0)) continue;
            if (a->last_ms && now-a->last_ms<(loop?30000:90000)) continue;
            if (!loop && i==last_asset) { last_candidate=i; continue; }
            eligible[count++]=i;
            total+=weights[deck][category];
        }
    }
    if (!total) return last_candidate;
    unsigned pick=random_u32()%total;
    for (int n=0;n<count;++n) {
        int i=eligible[n];
        unsigned weight=weights[deck][assets[i].category];
        if (pick<weight) return i;
        pick-=weight;
    }
    return -1;
}
static void start_asset(int slot,int id,int volume,uint64_t now) {
    if (id<0 || channel_base<0) return;
    Mix_Chunk *chunk=Mix_LoadWAV(assets[id].path);
    if (!chunk) { assets[id].bad=1; return; } /* no repeated decode errors */
    int channel=channel_base+slot;
    slot_gain[slot]=(slot==LAYER?28:42)*(75+(int)(random_u32()%26))/100;
    Mix_Volume(channel,AudioVolumeScale(volume,slot_gain[slot]*MIX_MAX_VOLUME)/100);
    if (slot==SHOT) {
        Uint8 right=(Uint8)(95+random_u32()%66);
        Mix_SetPanning(channel,(Uint8)(255-right),right);
    } else Mix_SetPanning(channel,255,255);
    int ok=slot==LAYER ? Mix_FadeInChannel(channel,chunk,-1,1800) : Mix_PlayChannel(channel,chunk,0);
    if (ok<0) { Mix_FreeChunk(chunk); assets[id].last_ms=now; return; }
    playing[slot]=chunk;
    playing_asset[slot]=id;
    assets[id].last_ms=now;
    if (slot==SHOT) {
        category_last[assets[id].category]=now;
        last_asset=id;
    }
}
void AmbientDirectorUpdate(int level,int gameplay,int combat,int enabled,int volume) {
    if (channel_base<0) return;
    int deck=deck_index(level);
    if (!enabled || !gameplay || deck<0 || volume<=0) {
        AmbientDirectorSuspend();
        current_deck=-1;
        return;
    }
    index_assets();
    uint64_t now=SDL_GetTicks64();
    if (deck!=current_deck) {
        halt_slot(LAYER,1200);
        halt_slot(SHOT,0);
        reap_slot(SHOT);
        current_deck=deck;
        next_ms=now+12000;
    }
    reap_slot(LAYER); reap_slot(SHOT);
    if (!asset_count) return;
    if (!playing[LAYER]) start_asset(LAYER,choose_asset(deck,1,now),volume,now);
    if (playing[LAYER]) Mix_Volume(channel_base+LAYER,AudioVolumeScale(volume,slot_gain[LAYER]*MIX_MAX_VOLUME)/100);
    if (playing[SHOT]) Mix_Volume(channel_base+SHOT,AudioVolumeScale(volume,slot_gain[SHOT]*MIX_MAX_VOLUME)/100);
    if (!next_ms) next_ms=now+15000;
    if (now<next_ms) return;
    next_ms=now+(combat?30000:15000)+(random_u32()%20001);
    if (!combat && !playing[SHOT]) start_asset(SHOT,choose_asset(deck,0,now),volume,now);
}
#else
void AmbientDirectorInit(int first_channel) { (void)first_channel; }
void AmbientDirectorUpdate(int level,int gameplay,int combat,int enabled,int volume) {
    (void)level;(void)gameplay;(void)combat;(void)enabled;(void)volume;
}
void AmbientDirectorSuspend(void) {}
void AmbientDirectorShutdown(void) {}
#endif
