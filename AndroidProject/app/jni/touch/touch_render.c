/* Full-surface touch UI; original logical rendering is restored after drawing.
   SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_controls.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void text(SDL_Renderer *r, const char *s, float cx, float cy, float size, float max_width) {
    static const char letters[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ<>0123456789.-+%/";
    static const unsigned char glyph[][5]={
        {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},
        {7,4,6,4,7},{7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},
        {7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},
        {5,7,7,5,5},{5,7,7,7,5},{2,5,5,5,2},{6,5,6,4,4},
        {2,5,5,7,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
        {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
        {5,5,2,2,2},{7,1,2,4,7},{1,2,4,2,1},{4,2,1,2,4},
        {7,5,5,5,7},{2,6,2,2,7},{6,1,2,4,7},{6,1,2,1,6},
        {5,5,7,1,1},{7,4,6,1,6},{3,4,7,5,7},{7,1,2,2,2},
        {7,5,7,5,7},{7,5,7,1,6},{0,0,0,0,2},{0,0,7,0,0},
        {0,2,7,2,0},{5,1,2,4,5},{1,1,2,4,4}
    };
    size=fminf(size,max_width/fmaxf(1,(float)strlen(s)*4));
    float x=cx-(float)strlen(s)*4*size/2;
    for (; *s; ++s,x+=4*size) {
        const char *p=strchr(letters,*s);
        if (!p) continue;
        for (int y=0;y<5;++y) for (int col=0;col<3;++col)
            if (glyph[p-letters][y] & (1u<<(2-col))) {
                SDL_FRect rect={x+col*size,cy-2.5f*size+y*size,size,size};
                SDL_RenderFillRectF(r,&rect);
            }
    }
}
static void color(SDL_Renderer *r, int red, int green, int blue, int alpha, float opacity) {
    SDL_SetRenderDrawColor(r,(Uint8)red,(Uint8)green,(Uint8)blue,(Uint8)lroundf(alpha*opacity));
}
static void rect(SDL_Renderer *r, TouchRect b, bool fill) {
    SDL_FRect v={b.x,b.y,b.w,b.h};
    if (fill) SDL_RenderFillRectF(r,&v); else SDL_RenderDrawRectF(r,&v);
}
static void circle(SDL_Renderer *r, TouchCircle c, bool held, bool selected, float opacity, float stroke) {
    color(r,held ? 37 : 19,held ? 119 : 33,held ? 133 : 43,held ? 210 : 120,opacity);
    for (int y=-(int)c.radius;y<=(int)c.radius;++y) {
        float span=sqrtf(fmaxf(0,c.radius*c.radius-y*y));
        SDL_RenderDrawLineF(r,c.x-span,c.y+y,c.x+span,c.y+y);
    }
    color(r,selected ? 255 : 173,selected ? 211 : 226,selected ? 99 : 232,selected ? 255 : 215,opacity);
    for (int line=0;line<(int)stroke;++line) for (int i=0;i<64;++i) {
        float a=i*6.2831853f/64,b=(i+1)*6.2831853f/64,rad=c.radius-line;
        SDL_RenderDrawLineF(r,c.x+cosf(a)*rad,c.y+sinf(a)*rad,
                           c.x+cosf(b)*rad,c.y+sinf(b)*rad);
    }
}
static bool held(const TouchControls *t,int id) {
    if (id==TC_FIRE) return t->fire_count>0;
    if (id==TC_USE) return t->use_count>0;
    if (id==TC_STICK) return t->move_owner>=0;
    if (id==TC_SPRINT) return t->sprint_latched;
    return id>=TC_JUMP && id<=TC_PAUSE && t->action_count[id-TC_JUMP]>0;
}
static void controls(const TouchControls *t,SDL_Renderer *r) {
    bool preview=t->panel!=TOUCH_UI_CLOSED;
    float h=t->layout.height,stroke=fmaxf(1,roundf(h/384));
    for (int id=0;id<TC_COUNT;++id) {
        if (!preview) {
            if (id==TC_PAUSE && !t->pause_available) continue;
            if (id!=TC_PAUSE && id!=TC_SETTINGS && !t->gameplay_available) continue;
            if (id!=TC_PAUSE && id!=TC_SETTINGS && id!=TC_MODE && t->effective_mode!=TOUCH_GAMEPLAY) continue;
        }
        TouchRect b=touch_control_bounds(t,id);
        bool selected=t->panel==TOUCH_UI_EDITOR && t->selected==id;
        float opacity=selected ? fmaxf(.85f,t->settings.opacity) : t->settings.opacity;
        bool down=!preview && held(t,id);
        const char *name=touch_defaults[id].label;
        if (id==TC_MODE) {
            color(r,24,43,54,170,opacity); rect(r,b,true);
            bool cursor=t->mode==TOUCH_CURSOR;
            color(r,selected || cursor ? 255 : 173,selected || cursor ? 211 : 226,
                  selected || cursor ? 99 : 232,240,opacity);
            for (int line=0;line<(int)stroke;++line) {
                TouchRect outline={b.x+line,b.y+line,b.w-2*line,b.h-2*line}; rect(r,outline,false);
            }
            name=t->mode==TOUCH_GAMEPLAY ? "LOOK >" : "CURSOR >";
        } else {
            TouchCircle c={b.x+b.w/2,b.y+b.h/2,b.w/2};
            if (id==TC_STICK && !preview && t->move_owner>=0) { c.x=t->anchor_x; c.y=t->anchor_y; }
            circle(r,c,down,selected,opacity,stroke);
            if (id==TC_STICK) {
                TouchCircle nub={c.x+t->movement_x*c.radius,c.y+t->movement_y*c.radius,c.radius*.37f};
                circle(r,nub,down,false,opacity,stroke);
                if (!preview) continue;
            }
            if (id==TC_SPRINT && down) name="RUN ON";
        }
        color(r,selected ? 255 : 226,selected ? 224 : 245,selected ? 135 : 245,245,opacity);
        text(r,name,b.x+b.w/2,b.y+b.h/2,h/240*t->settings.control[id].scale,b.w*.86f);
    }
}
static const char *ui_label(const TouchControls *t,int action) {
    static const char *names[TU_COUNT]={"EDIT LAYOUT","-","+","-","+","-","+",
        "INVERT Y OFF","-","+","RESET TOUCH SETTINGS","CANCEL","SAVE / DONE",
        "<",">","SIZE -","SIZE +","SETTINGS","MOVE TOOLBAR"};
    if (action==TU_INVERT && t->settings.invert_y) return "INVERT Y ON";
    if (t->panel==TOUCH_UI_EDITOR) {
        if (action==TU_OPACITY_DOWN) return "OPACITY -";
        if (action==TU_OPACITY_UP) return "OPACITY +";
        if (action==TU_RESET) return "RESET DEFAULTS";
    }
    return names[action];
}
static void panel(const TouchControls *t,SDL_Renderer *r) {
    float w=t->layout.width,h=t->layout.height;
    char value[100];
    if (t->panel==TOUCH_UI_SETTINGS) {
        color(r,4,10,17,160,1); rect(r,(TouchRect){0,0,w,h},true);
        color(r,15,28,40,250,1); rect(r,(TouchRect){w*.18f,h*.035f,w*.64f,h*.94f},true);
        color(r,208,241,244,255,1);
        text(r,"TOUCH SETTINGS",w*.5f,h*.09f,h/185,w*.5f);
        const float values[4]={t->settings.opacity*100,t->settings.sensitivity_x,
                              t->settings.sensitivity_y,t->settings.control[TC_STICK].scale};
        const char *names[4]={"OPACITY","LOOK X","LOOK Y","STICK SIZE"};
        const float rows[4]={.3075f,.4075f,.5075f,.7075f};
        for (int i=0;i<4;++i) {
            if (i==0) snprintf(value,sizeof(value),"%s %.0f%%",names[i],values[i]);
            else snprintf(value,sizeof(value),"%s %.2f",names[i],values[i]);
            text(r,value,w*.5f,h*rows[i],h/255,w*.33f);
        }
    } else {
        if (t->ui_drag) return; /* captured drag can cross either toolbar edge */
        float y=t->toolbar_top ? 0 : h*.28f;
        color(r,11,23,34,248,1); rect(r,(TouchRect){0,y,w,h*.17f},true);
        color(r,240,231,190,255,1);
        snprintf(value,sizeof(value),"%s %.1f",touch_defaults[t->selected].key,t->settings.control[t->selected].scale);
        /* Keys are lowercase on disk; labels remain readable in the bitmap font. */
        for (char *p=value;*p;++p) if (*p>='a' && *p<='z') *p=(char)(*p-'a'+'A');
        text(r,value,w*.175f,y+h*.0445f,h/290,w*.19f);
        color(r,213,239,241,220,1);
        snprintf(value,sizeof(value),"DRAG A CONTROL   OPACITY %.0f%%",t->settings.opacity*100);
        text(r,value,w*.5f,t->toolbar_top ? h*.20f : h*.25f,h/330,w*.75f);
    }
    for (int a=0;a<TU_COUNT;++a) {
        TouchRect b=touch_ui_bounds(t,(TouchUiAction)a);
        if (b.w<=0) continue;
        bool down=t->ui_owned && t->ui_action==a;
        color(r,down ? 43 : 26,down ? 119 : 51,down ? 129 : 65,255,1); rect(r,b,true);
        color(r,a==TU_SAVE ? 105 : 115,a==TU_SAVE ? 222 : 161,190,255,1); rect(r,b,false);
        color(r,227,244,245,255,1);
        text(r,ui_label(t,a),b.x+b.w/2,b.y+b.h/2,h/270,b.w*.88f);
    }
    if (t->save_failed) {
        color(r,255,153,132,255,1);
        text(r,"SAVE FAILED - RETRY OR CANCEL",w*.5f,h*.125f,h/310,w*.60f);
    }
}
void touch_draw(const TouchControls *t,SDL_Renderer *r) {
    if (!t->enabled || (!t->gameplay_available && !t->pause_available) || t->controller) return;
    Uint8 red,green,blue,alpha;
    SDL_BlendMode blend;
    SDL_Rect clip,viewport;
    float sx,sy;
    SDL_bool clipped=SDL_RenderIsClipEnabled(r);
    SDL_RenderGetClipRect(r,&clip);
    SDL_GetRenderDrawColor(r,&red,&green,&blue,&alpha);
    SDL_GetRenderDrawBlendMode(r,&blend);
    SDL_RenderGetScale(r,&sx,&sy);
    SDL_RenderSetScale(r,1,1);
    SDL_RenderGetViewport(r,&viewport);
    SDL_RenderSetViewport(r,NULL);
    SDL_RenderSetClipRect(r,NULL);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    controls(t,r);
    if (t->panel!=TOUCH_UI_CLOSED) panel(t,r);
    SDL_SetRenderDrawBlendMode(r,blend);
    SDL_SetRenderDrawColor(r,red,green,blue,alpha);
    SDL_RenderSetViewport(r,&viewport);
    SDL_RenderSetScale(r,sx,sy);
    SDL_RenderSetClipRect(r,clipped ? &clip : NULL);
}
