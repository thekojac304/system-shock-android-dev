/* Real overlay drawing code against an SDL command recorder: state restoration
   and the shared draw/hit-test geometry are deterministic without a GPU. */
#include <SDL.h>
#include "touch_controls.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static Uint8 color[4]={12,34,56,78};
static SDL_BlendMode blend=SDL_BLENDMODE_ADD;
static SDL_Rect clip={11,22,33,44};
static SDL_bool clipped=SDL_TRUE;
static int draws, fire_center_lines;
static TouchControls t;
static float sx=1.25f, sy=1.25f;
static SDL_Rect viewport={320,0,1280,960};
void SDL_RenderGetScale(SDL_Renderer *r,float *x,float *y) {(void)r; *x=sx; *y=sy;}
int SDL_RenderSetScale(SDL_Renderer *r,float x,float y) {(void)r; sx=x; sy=y; return 0;}
void SDL_RenderGetViewport(SDL_Renderer *r,SDL_Rect *v) {
    (void)r; *v=(SDL_Rect){(int)(viewport.x/sx),(int)(viewport.y/sy),
        (int)(viewport.w/sx),(int)(viewport.h/sy)};
}
int SDL_RenderSetViewport(SDL_Renderer *r,const SDL_Rect *v) {
    (void)r;
    viewport=v ? (SDL_Rect){(int)(v->x*sx),(int)(v->y*sy),(int)(v->w*sx),(int)(v->h*sy)}
               : (SDL_Rect){0,0,1920,960}; return 0;
}
SDL_bool SDL_RenderIsClipEnabled(SDL_Renderer *r) {(void)r; return clipped;}
void SDL_RenderGetClipRect(SDL_Renderer *r,SDL_Rect *c) {(void)r; *c=clip;}
int SDL_RenderSetClipRect(SDL_Renderer *r,const SDL_Rect *c) {
    (void)r; clipped=c ? SDL_TRUE : SDL_FALSE;
    if(c) clip=*c; else memset(&clip,0,sizeof(clip)); return 0;
}
int SDL_GetRenderDrawColor(SDL_Renderer *r,Uint8 *red,Uint8 *g,Uint8 *b,Uint8 *a) {
    (void)r; *red=color[0]; *g=color[1]; *b=color[2]; *a=color[3]; return 0;
}
int SDL_SetRenderDrawColor(SDL_Renderer *r,Uint8 red,Uint8 g,Uint8 b,Uint8 a) {
    (void)r; color[0]=red; color[1]=g; color[2]=b; color[3]=a; return 0;
}
int SDL_GetRenderDrawBlendMode(SDL_Renderer *r,SDL_BlendMode *b) {(void)r; *b=blend; return 0;}
int SDL_SetRenderDrawBlendMode(SDL_Renderer *r,SDL_BlendMode b) {(void)r; blend=b; return 0;}
int SDL_RenderDrawLineF(SDL_Renderer *r,float x1,float y1,float x2,float y2) {
    (void)r; assert(!clipped && blend==SDL_BLENDMODE_BLEND); draws++;
    assert(sx==1 && sy==1 && viewport.x==0 && viewport.w==1920);
    if(y1==t.layout.fire.y && y2==y1 && x1<t.layout.fire.x && x2>t.layout.fire.x) fire_center_lines++;
    return 0;
}
int SDL_RenderFillRectF(SDL_Renderer *r,const SDL_FRect *rect) {
    (void)r; (void)rect; assert(!clipped && blend==SDL_BLENDMODE_BLEND); draws++; return 0;
}
int SDL_RenderDrawRectF(SDL_Renderer *r,const SDL_FRect *rect) {
    return SDL_RenderFillRectF(r,rect);
}
static void restored(void) {
    assert(color[0]==12 && color[1]==34 && color[2]==56 && color[3]==78);
    assert(clipped && clip.x==11 && clip.y==22 && clip.w==33 && clip.h==44);
    assert(blend==SDL_BLENDMODE_ADD);
    assert(sx==1.25f && sy==1.25f && viewport.x==320 && viewport.w==1280 && viewport.h==960);
}
int main(void) {
    touch_init(&t,NULL,NULL); touch_configure(&t,1920,960,true,true,false);
    touch_draw(&t,NULL); assert(draws>0 && fire_center_lines>0); restored();
    assert(t.layout.fire.x>1600); /* fire is drawn/hit in the right black bar */
    touch_finger(&t,TOUCH_DOWN,1,1,t.layout.fire.x,t.layout.fire.y,0);
    assert(t.fire_count==1); /* drawn center is also the touch hit target */
    touch_set_mode(&t,TOUCH_CURSOR); draws=fire_center_lines=0;
    touch_draw(&t,NULL); assert(draws>0 && !fire_center_lines); restored();
    touch_configure(&t,1024,768,true,true,true); draws=0;
    touch_draw(&t,NULL); assert(!draws); restored();
    touch_configure(&t,1024,768,true,false,false); draws=0;
    touch_draw(&t,NULL); assert(!draws); restored(); /* movie/menu: no overlay */
    puts("TOUCH_RENDER_TESTS=PASS (4 groups)"); return 0;
}
