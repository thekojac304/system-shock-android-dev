/* SPDX-License-Identifier: GPL-3.0-or-later
 * Bounded binary loader for accepted AUTHOR1D/1E QA profiles. No diagnostic
 * JSON, geometry walk, portal animation, or resource bitmap enters this path. */
#include "GpuGenerated.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *error_text="none";
static const char *baseline[2]={
    "ebdb77338941febd90b424bd456435ce4e3cd81bfcc5f0c7bb9ee0d1957c41e7",
    "e481e2988d22704112ee5366029251abe93f506e35c37ec5a5efc8690d238c12"
};
static const char *rules="bb9f391b82a3084d030251f18dc9c8b124992da8f5ef6d2f22356f840f16f466";
typedef struct { const uint8_t *at,*end; int ok; } Reader;
static uint8_t u8(Reader *r){if(r->at>=r->end){r->ok=0;return 0;}return *r->at++;}
static uint16_t u16(Reader *r){uint16_t a=u8(r),b=u8(r);return a|(b<<8);}
static uint32_t u32(Reader *r){uint32_t a=u16(r),b=u16(r);return a|(b<<16);}
static uint64_t u64(Reader *r){uint64_t a=u32(r),b=u32(r);return a|(b<<32);}
static float f32(Reader *r){uint32_t v=u32(r);float f;memcpy(&f,&v,4);if(!isfinite(f))r->ok=0;return f;}
static int signature(Reader *r,const char *hex){
    for(int i=0;i<32;i++){unsigned value=0;sscanf(hex+i*2,"%2x",&value);if(u8(r)!=value)return 0;}
    return r->ok;
}
static uint32_t crc32_body(const uint8_t *p,size_t n){
    uint32_t crc=~0u;
    for(size_t i=0;i<n;i++){crc^=p[i];for(int j=0;j<8;j++)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    return ~crc;
}
static int sid_cmp(GpuSurfaceId a,GpuSurfaceId b){
    const int av[]={a.level,a.x,a.y,a.kind,a.face,a.subface,a.terrain_type};
    const int bv[]={b.level,b.x,b.y,b.kind,b.face,b.subface,b.terrain_type};
    for(int i=0;i<7;i++)if(av[i]!=bv[i])return av[i]<bv[i]?-1:1;
    if(a.geometry_key!=b.geometry_key)return a.geometry_key<b.geometry_key?-1:1;
    if(a.neighbor_geometry_key!=b.neighbor_geometry_key)return a.neighbor_geometry_key<b.neighbor_geometry_key?-1:1;
    return 0;
}
void gg_profile_free(GgProfile *p){
    if(!p)return;free(p->region_ambient);free(p->lights);free(p->surfaces);free(p);
}
const char *gg_profile_error(void){return error_text;}
int gg_mask_row(int material){
    static const int ids[]={20,35,36,82,101,144,185,215,216,217};
    for(int i=0;i<GG_MASKS;i++)if(ids[i]==material)return i;
    return -1;
}
GgProfile *gg_profile_load(int level){
    error_text="profile unavailable";
    if(level!=1&&level!=6)return NULL;
    char path[64];snprintf(path,sizeof(path),"horror-generated/level-%d.bin",level);
    SDL_RWops *file=SDL_RWFromFile(path,"rb");if(!file)return NULL;
    Sint64 size=SDL_RWsize(file);
    if(size<96||size>1048576){SDL_RWclose(file);error_text="profile length";return NULL;}
    uint8_t *bytes=malloc((size_t)size);if(!bytes){SDL_RWclose(file);error_text="profile allocation";return NULL;}
    size_t got=SDL_RWread(file,bytes,1,(size_t)size);SDL_RWclose(file);
    if(got!=(size_t)size){free(bytes);error_text="profile read";return NULL;}
    Reader r={bytes,bytes+(size_t)size,1};
    if(u8(&r)!='H'||u8(&r)!='G'||u8(&r)!='1'||u8(&r)!='P')r.ok=0;
    uint32_t version=u32(&r),file_level=u32(&r),nlights=u32(&r),nsurfaces=u32(&r),nregions=u32(&r),crc=u32(&r);
    if(version!=1||file_level!=(uint32_t)level||nlights>512||nsurfaces>4096||nregions>1024||!nlights||!nsurfaces)r.ok=0;
    if(!signature(&r,baseline[level==1?0:1])||!signature(&r,rules))r.ok=0;
    if(!r.ok||crc32_body(r.at,(size_t)(r.end-r.at))!=crc){free(bytes);error_text="profile version/signature/checksum";return NULL;}
    GgProfile *p=calloc(1,sizeof(*p));
    if(!p){free(bytes);error_text="profile allocation";return NULL;}
    p->level=level;p->light_count=(int)nlights;p->surface_count=(int)nsurfaces;p->region_count=(int)nregions;
    p->region_ambient=calloc(nregions+1,sizeof(*p->region_ambient));
    p->lights=calloc(nlights,sizeof(*p->lights));p->surfaces=calloc(nsurfaces,sizeof(*p->surfaces));
    if(!p->region_ambient||!p->lights||!p->surfaces){r.ok=0;goto done;}
    for(int i=0;i<4096;i++)p->map_keys[i]=u32(&r);
    for(int i=0;i<4096;i++){p->region_for_tile[i]=u16(&r);if(p->region_for_tile[i]>nregions)r.ok=0;}
    for(int j=0;j<3;j++)p->default_ambient[j]=f32(&r);
    for(uint32_t i=0;i<nregions;i++)for(int j=0;j<3;j++)p->region_ambient[i+1][j]=f32(&r);
    for(uint32_t i=0;i<nlights;i++){
        GgLight *light=&p->lights[i];
        for(int j=0;j<3;j++)light->position[j]=f32(&r);
        for(int j=0;j<3;j++)light->rgb[j]=f32(&r);
        light->intensity=f32(&r);light->radius=f32(&r);
        light->material=(int32_t)u32(&r);light->source_object=(int32_t)u32(&r);
        if(light->radius<=0||light->intensity<0)r.ok=0;
    }
    for(uint32_t i=0;i<nsurfaces;i++){
        GgSurface *surface=&p->surfaces[i];GpuSurfaceId *id=&surface->id;
        id->level=u8(&r);id->x=u8(&r);id->y=u8(&r);id->kind=u8(&r);
        id->face=u8(&r);id->subface=u8(&r);id->terrain_type=u8(&r);id->valid=u8(&r);
        id->geometry_key=u32(&r);id->neighbor_geometry_key=u32(&r);
        surface->count=u8(&r);
        if(id->level!=level||id->x>63||id->y>63||!id->valid||surface->count>GG_MAX_LIGHTS||
           (i&&sid_cmp(p->surfaces[i-1].id,*id)>=0)||
           p->map_keys[id->y*64+id->x]!=id->geometry_key)r.ok=0;
        if(!r.ok)goto done;
        for(int j=0;j<surface->count;j++){
            GgReceiver *rec=&surface->lights[j];
            rec->light_index=u16(&r);rec->kind=u8(&r);rec->polygons=u8(&r);
            rec->portal_dependencies=u64(&r);
            if(rec->light_index>=nlights||rec->kind<1||rec->kind>2||
               rec->polygons>GG_MAX_POLYGONS||(rec->kind==1&&rec->polygons!=0)||
               (rec->kind==2&&rec->polygons==0))r.ok=0;
            if(!r.ok)goto done;
            for(int k=0;k<rec->polygons;k++){
                int count=rec->plane_counts[k]=u8(&r);
                if(count<3||count>GG_MAX_PLANES)r.ok=0;
                if(!r.ok)goto done;
                for(int m=0;m<count;m++)for(int c=0;c<4;c++){
                    float v=f32(&r);if(m<GG_MAX_PLANES)rec->planes[k][m][c]=v;
                }
            }
        }
    }
    for(int i=0;i<GG_MASKS;i++)for(int j=0;j<256;j++){
        p->masks[i][j]=u8(&r);if(p->masks[i][j]>1)r.ok=0;
    }
    if(r.at!=r.end)r.ok=0;
done:
    free(bytes);
    if(!r.ok){gg_profile_free(p);error_text="profile malformed/stale";return NULL;}
    error_text="none";return p;
}
const GgSurface *gg_profile_find(const GgProfile *p,GpuSurfaceId id){
    if(!p||!id.valid||id.level!=p->level)return NULL;
    int lo=0,hi=p->surface_count;
    while(lo<hi){int mid=(lo+hi)/2,cmp=sid_cmp(p->surfaces[mid].id,id);
        if(cmp<0)lo=mid+1;else hi=mid;}
    return lo<p->surface_count&&gpu_surface_id_equal(p->surfaces[lo].id,id)?&p->surfaces[lo]:NULL;
}
int gg_profile_map_matches(const GgProfile *p,const uint32_t *keys){
    return p&&keys&&!memcmp(p->map_keys,keys,sizeof(p->map_keys));
}
const float *gg_profile_ambient(const GgProfile *p,int tile){
    if(!p)return NULL;
    int region=tile>=0&&tile<4096?p->region_for_tile[tile]:0;
    return region?p->region_ambient[region]:p->default_ambient;
}
