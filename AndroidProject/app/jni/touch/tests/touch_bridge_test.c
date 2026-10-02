#include "touch_shock_bridge.h"
#include "touch_surface.h"
#include "physics.h"
#include "mainloop.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static SDL_Window win;
static SDL_Renderer render;
SDL_Window *window=&win;
SDL_Renderer *renderer=&render;
static TestMap map;
TestMap *global_fullmap=&map;
int mlook_enabled;
bool TriggerRelMouseMode;
uchar game_paused, wrapper_panel_on, olh_overlay_on, physics_running=1, time_passes=1;
short _current_loop=GAME_LOOP;
unsigned _change_flag;
static byte axes[2];
static int hints, releases, nx=1024, ny=768, px, py, buttons, event_count;
static int events[32], aim_x[32], aim_y[32];
static Uint32 flags=SDL_WINDOW_INPUT_FOCUS;
static int center_warps, relative_off;
static int action_down[TOUCH_ACTION_COUNT], action_up[TOUCH_ACTION_COUNT], action_cancels;
static bool sprint_active;
static TouchCursorTarget cursor_target;
static bool switch_target_on_press;
TouchCursorTarget touch_cursor_target_at(int x,int y) {(void)x; (void)y; return cursor_target;}
void touch_input_action(TouchAction action,bool down) {
    assert(action>=0 && action<TOUCH_ACTION_COUNT);
    if(down) action_down[action]++; else action_up[action]++;
    if(action==TOUCH_SPRINT) sprint_active=down;
}
void touch_input_cancel_actions(void) {action_cancels++; sprint_active=false;}
int SDL_SetRelativeMouseMode(int enabled) {assert(!enabled); relative_off++; return 0;}
void touch_surface_init(void) {}
bool touch_surface_failed(void) {return false;}
bool touch_surface_read(const SDL_Event *e,TouchSurfaceEvent *s) {
    if(e->type!=100) return false;
    memcpy(s,e,sizeof(*s)); return true;
}
void SDL_RenderGetScale(SDL_Renderer *r,float *x,float *y) {
    (void)r; *x=*y=fminf(nx/1024.0f,ny/768.0f);
}
void SDL_RenderWindowToLogical(SDL_Renderer *r,int x,int y,float *lx,float *ly) {
    float sx,sy; SDL_RenderGetScale(r,&sx,&sy);
    *lx=(x-(nx-1024*sx)/2)/sx; *ly=(y-(ny-768*sy)/2)/sy;
}

int SDL_SetHintWithPriority(const char *key,const char *value,int priority) {
    assert(!strcmp(value,"0") && priority==SDL_HINT_OVERRIDE);
    assert(!strcmp(key,SDL_HINT_TOUCH_MOUSE_EVENTS) || !strcmp(key,SDL_HINT_MOUSE_TOUCH_EVENTS));
    hints++; return 1;
}
void SDL_RenderGetLogicalSize(SDL_Renderer *r,int *x,int *y) {(void)r; *x=1024; *y=768;}
int SDL_GetRendererOutputSize(SDL_Renderer *r,int *x,int *y) {(void)r; *x=nx; *y=ny; return 0;}
void SDL_GetWindowSize(SDL_Window *w,int *x,int *y) {(void)w; *x=nx; *y=ny;}
Uint32 SDL_GetWindowFlags(SDL_Window *w) {(void)w; return flags;}
void physics_get_one_control(int bank,int axis,byte *value) {assert(bank==1); *value=axes[axis];}
void physics_set_one_control(int bank,int axis,byte value) {assert(bank==1); axes[axis]=value;}
void mouse_look_toggle(void) {
    mlook_enabled=!mlook_enabled;
    if (mlook_enabled) {int dx,dy; touch_bridge_consume_look(&dx,&dy);}
    else {TriggerRelMouseMode=true; center_warps++;}
}
void middleize_mouse(void) {px=512; py=384;}
void touch_input_pointer(float x,float y) {px=(int)x; py=(int)y;}
void touch_input_button(bool left,bool down) {
    int bit=left ? 1 : 2;
    if (left && down && switch_target_on_press) cursor_target=TOUCH_TARGET_WORLD;
    if(down) buttons|=bit; else buttons&=~bit;
    assert(event_count<32);
    events[event_count]=down ? bit : -bit;
    aim_x[event_count]=px; aim_y[event_count++]=py;
}
void touch_input_cancel(void) {releases++; buttons=0; event_count=0;}
void touch_draw(const TouchControls *t,SDL_Renderer *r) {(void)t; (void)r;}
static void finger(Uint32 type,int id,float x,float y) {
    TouchSurfaceEvent s={100,0,42,id,x*nx,y*ny,nx,ny,
        type==SDL_FINGERDOWN ? TOUCH_DOWN : type==SDL_FINGERUP ? TOUCH_UP : TOUCH_MOTION,0};
    SDL_Event ev={0}; memcpy(&ev,&s,sizeof(s)); touch_bridge_event(&ev);
}
static void lifecycle(Uint32 type,int detail) {
    SDL_Event ev={0}; ev.type=type; ev.window.event=detail; touch_bridge_event(&ev);
}
static void move(void) {
    finger(SDL_FINGERDOWN,1,.1f,.7f); finger(SDL_FINGERMOTION,1,.15f,.65f);
    axes[0]=axes[1]=0; touch_bridge_merge_movement();
    assert(axes[0]>0 && axes[1]>0);
}
static void reset(void) {
    touch_bridge_cancel();
    flags=SDL_WINDOW_INPUT_FOCUS; map.cyber=0; game_paused=wrapper_panel_on=olh_overlay_on=0;
    physics_running=time_passes=1; _current_loop=GAME_LOOP; _change_flag=0;
    mlook_enabled=0; axes[0]=axes[1]=0;
    nx=1024; ny=768;
    center_warps=relative_off=0;
    touch_bridge_init(); touch_bridge_controller(false); touch_bridge_sync();
    event_count=0; cursor_target=TOUCH_TARGET_UI_CONTROL; switch_target_on_press=false;
}
static void aim_and_actions(void) {
    reset(); assert(hints>=2);
    px=22; py=55;
    finger(SDL_FINGERDOWN,2,.86f,.73f);
    assert(mlook_enabled && event_count==1 && events[0]==2);
    assert(aim_x[0]==512 && aim_y[0]==384);
    finger(SDL_FINGERMOTION,2,.81f,.70f);
    int x,y; touch_bridge_consume_look(&x,&y); assert(x==-51 && y==-23);
    touch_bridge_consume_look(&x,&y); assert(x==0 && y==0);
    finger(SDL_FINGERUP,2,.81f,.70f); assert(events[1]==-2 && !buttons);
    event_count=0; px=100; py=100;
    finger(SDL_FINGERDOWN,3,.69f,.85f);
    assert(event_count==4 && events[0]==1 && events[1]==-1 && events[2]==1 && events[3]==-1);
    for(int i=0;i<4;i++) assert(aim_x[i]==512 && aim_y[i]==384);
    finger(SDL_FINGERUP,3,.69f,.85f); assert(event_count==4);
}
static void movement_and_lifecycle(void) {
    for(int reason=0;reason<10;reason++) {
        reset(); move(); finger(SDL_FINGERDOWN,2,.86f,.73f);
        finger(SDL_FINGERMOTION,2,.90f,.7f);
        int previous=releases;
        switch(reason) {
        case 0: game_paused=1; break;
        case 1: wrapper_panel_on=1; break;
        case 2: map.cyber=1; break;
        case 3: _current_loop=4; break;
        case 4: nx++; break;
        case 5: lifecycle(SDL_WINDOWEVENT,SDL_WINDOWEVENT_FOCUS_LOST); break;
        case 6: lifecycle(SDL_APP_WILLENTERBACKGROUND,0); break;
        case 7: touch_bridge_controller(true); break;
        case 8: mlook_enabled=0; break;
        case 9: olh_overlay_on=1; break;
        }
        touch_bridge_sync(); assert(releases>previous && !buttons && !axes[0] && !axes[1]);
        int x,y; touch_bridge_consume_look(&x,&y); assert(x==0 && y==0);
        finger(SDL_FINGERMOTION,1,.3f,.6f); assert(!axes[0] && !axes[1]);
        lifecycle(SDL_APP_DIDENTERFOREGROUND,0);
        lifecycle(SDL_WINDOWEVENT,SDL_WINDOWEVENT_FOCUS_GAINED);
    }
    reset(); move();
    axes[0]=-50; axes[1]=100; touch_bridge_merge_movement();
    assert(axes[0]==-50 && axes[1]==100);
    touch_bridge_cancel(); assert(axes[0]==-50 && axes[1]==100);
    reset(); move(); finger(SDL_FINGERUP,1,.15f,.65f); assert(!axes[0] && !axes[1]);
}
static void cursor_fallback(void) {
    reset(); map.cyber=1; touch_bridge_sync();
    finger(SDL_FINGERDOWN,1,.3f,.4f); assert(buttons==0 && !mlook_enabled);
    assert(px==307 && py==307);
    finger(SDL_FINGERDOWN,2,.9f,.8f); assert(px==307 && py==307);
    finger(SDL_FINGERMOTION,1,.5f,.5f); assert(px==512 && py==384 && buttons==1);
    assert(aim_x[0]==307 && aim_y[0]==307);
    finger(SDL_FINGERUP,1,.5f,.5f); assert(buttons==0);
    finger(SDL_FINGERMOTION,2,.6f,.6f); assert(px==512 && py==384);
    reset(); touch_bridge_controller(true);
    finger(SDL_FINGERDOWN,1,.86f,.73f); assert(buttons==0 && !mlook_enabled);
    finger(SDL_FINGERUP,1,.86f,.73f); assert(event_count==2 && events[0]==1 && events[1]==-1);
    reset(); finger(SDL_FINGERDOWN,1,.85f,.08f); assert(!buttons && !mlook_enabled);
    finger(SDL_FINGERMOTION,1,.3f,.5f); assert(!buttons);
}
static void full_surface_and_transition(void) {
    reset(); nx=1920; ny=1080; touch_bridge_sync();
    finger(SDL_FINGERDOWN,1,40.0f/nx,.5f);
    finger(SDL_FINGERMOTION,1,100.0f/nx,.4f);
    axes[0]=axes[1]=0; touch_bridge_merge_movement(); assert(axes[0]>0 && axes[1]>0);
    finger(SDL_FINGERDOWN,2,1880.0f/nx,.3f);
    finger(SDL_FINGERMOTION,2,(1880.0f-14.0625f)/nx,.3f);
    int dx,dy; touch_bridge_consume_look(&dx,&dy); assert(dx==-10 && dy==0);
    finger(SDL_FINGERDOWN,3,.85f,.08f); /* toggle with active movement/look */
    assert(!mlook_enabled && !TriggerRelMouseMode && relative_off==1 && !center_warps);
    assert(!axes[0] && !axes[1] && !buttons);
    finger(SDL_FINGERMOTION,3,.5f,.5f); assert(!buttons);
    finger(SDL_FINGERDOWN,4,.5f,.5f); assert(px==512 && py==384 && !buttons);
    finger(SDL_FINGERMOTION,4,.6f,.5f); assert(buttons==1 && aim_x[0]==512);
    /* Rotate without changing dimensions: still cancels an active drag. */
    lifecycle(SDL_DISPLAYEVENT,SDL_DISPLAYEVENT_ORIENTATION); assert(!buttons);
    finger(SDL_FINGERMOTION,4,.7f,.5f); finger(SDL_FINGERUP,4,.7f,.5f); assert(!buttons);
    finger(SDL_FINGERDOWN,5,0,.5f); assert(px==0 && py==384);
    finger(SDL_FINGERUP,5,0,.5f);
    finger(SDL_FINGERDOWN,6,1,.5f); assert(px==1023 && py==384);
    touch_bridge_cancel();
    SDL_Event old={0}; old.type=SDL_FINGERDOWN; old.tfinger.x=.5f; old.tfinger.y=.5f;
    px=12; py=34; touch_bridge_event(&old); assert(px==12 && py==34 && !buttons);
    TouchSurfaceEvent s={100,0,42,8,960,540,1024,768,TOUCH_DOWN,0};
    memcpy(&old,&s,sizeof(s)); touch_bridge_event(&old); assert(px==12 && py==34 && !buttons);
    nx=1080; ny=1920; touch_bridge_sync();
    finger(SDL_FINGERDOWN,9,.5f,0); assert(px==512 && py==0);
    finger(SDL_FINGERUP,9,.5f,0);
    finger(SDL_FINGERDOWN,10,.5f,1); assert(px==512 && py==767);
}
static void alpha2_actions(void) {
    static const float xy[TOUCH_ACTION_COUNT][2] = {
        {.95f,.52f},{.79f,.54f},{.55f,.78f},{.55f,.90f},
        {.52f,.10f},{.63f,.10f},{.22f,.54f},{.73f,.10f},{.05f,.06f}
    };
    reset(); memset(action_down,0,sizeof(action_down));
    memset(action_up,0,sizeof(action_up));
    for(int a=0;a<TOUCH_ACTION_COUNT;a++) {
        if(a==TOUCH_SPRINT) continue;
        finger(SDL_FINGERDOWN,20+a,xy[a][0],xy[a][1]);
        finger(SDL_FINGERUP,20+a,xy[a][0],xy[a][1]);
        assert(action_down[a]==1 && action_up[a]==1);
    }
    finger(SDL_FINGERDOWN,40,.22f,.54f);
    assert(action_down[TOUCH_SPRINT]==0);
    finger(SDL_FINGERUP,40,.22f,.54f);
    assert(action_down[TOUCH_SPRINT]==1 && action_up[TOUCH_SPRINT]==0);
    finger(SDL_FINGERDOWN,41,.22f,.54f);
    finger(SDL_FINGERUP,41,.22f,.54f);
    assert(action_down[TOUCH_SPRINT]==1 && action_up[TOUCH_SPRINT]==1);
    reset();
    finger(SDL_FINGERDOWN,1,.22f,.54f); /* sprint */
    finger(SDL_FINGERUP,1,.22f,.54f);
    assert(sprint_active);
    int cancelled=action_cancels;
    lifecycle(SDL_WINDOWEVENT,SDL_WINDOWEVENT_FOCUS_LOST);
    assert(action_cancels>cancelled && !sprint_active);
    lifecycle(SDL_WINDOWEVENT,SDL_WINDOWEVENT_FOCUS_GAINED);
    finger(SDL_FINGERDOWN,2,.22f,.54f);
    finger(SDL_FINGERUP,2,.22f,.54f);
    assert(sprint_active);
    lifecycle(SDL_APP_WILLENTERBACKGROUND,0);
    assert(!sprint_active);
    lifecycle(SDL_APP_DIDENTERFOREGROUND,0);
    lifecycle(SDL_WINDOWEVENT,SDL_WINDOWEVENT_FOCUS_GAINED);
    finger(SDL_FINGERDOWN,5,.22f,.54f);
    finger(SDL_FINGERUP,5,.22f,.54f);
    assert(sprint_active);
    cancelled=action_cancels; touch_bridge_controller(true);
    assert(action_cancels>cancelled && !sprint_active);
    reset(); game_paused=1; touch_bridge_sync();
    int previous=action_down[TOUCH_PAUSE];
    finger(SDL_FINGERDOWN,3,.05f,.06f);
    assert(action_down[TOUCH_PAUSE]==previous+1);
    reset(); map.cyber=1; touch_bridge_sync();
    previous=action_down[TOUCH_PAUSE];
    finger(SDL_FINGERDOWN,4,.05f,.06f);
    assert(action_down[TOUCH_PAUSE]==previous);
}
static void cursor_regions(void) {
    reset();
    finger(SDL_FINGERDOWN,1,.85f,.08f); finger(SDL_FINGERUP,1,.85f,.08f);
    cursor_target=TOUCH_TARGET_UI_CONTROL;
    finger(SDL_FINGERDOWN,2,.5f,.5f); finger(SDL_FINGERUP,2,.5f,.5f);
    assert(event_count==2 && events[0]==1 && events[1]==-1);
    cursor_target=TOUCH_TARGET_WORLD; event_count=0;
    finger(SDL_FINGERDOWN,3,.5f,.5f); finger(SDL_FINGERUP,3,.5f,.5f);
    assert(event_count==4 && events[0]==1 && events[1]==-1 &&
           events[2]==1 && events[3]==-1);
    for(int i=0;i<4;i++) assert(aim_x[i]==512 && aim_y[i]==384);
    event_count=0;
    finger(SDL_FINGERDOWN,4,.5f,.5f);
    finger(SDL_FINGERMOTION,4,.6f,.5f);
    finger(SDL_FINGERUP,4,.6f,.5f);
    assert(event_count==2 && events[0]==1 && events[1]==-1);
    event_count=0;
    for(int i=0;i<3;i++) {
        finger(SDL_FINGERDOWN,10+i,.5f,.5f);
        finger(SDL_FINGERUP,10+i,.5f,.5f);
    }
    assert(event_count==12 && !buttons);
    /* Fullscreen icon: one normal press per exit, across repeated entries. */
    cursor_target=TOUCH_TARGET_UI_CONTROL; event_count=0;
    for(int i=0;i<3;i++) {
        switch_target_on_press=true;
        cursor_target=TOUCH_TARGET_UI_CONTROL;
        finger(SDL_FINGERDOWN,30+i,.5f,.5f);
        finger(SDL_FINGERUP,30+i,.5f,.5f);
        assert(event_count==(i+1)*2);
    }
    switch_target_on_press=false;
    /* Gump/inventory item: UI object gets the native double-click sequence. */
    cursor_target=TOUCH_TARGET_UI_OBJECT; event_count=0;
    finger(SDL_FINGERDOWN,40,.5f,.5f);
    finger(SDL_FINGERMOTION,40,.503f,.503f);
    finger(SDL_FINGERUP,40,.504f,.504f);
    assert(event_count==4 && events[0]==1 && events[1]==-1 &&
           events[2]==1 && events[3]==-1);
    event_count=0;
    finger(SDL_FINGERDOWN,41,.5f,.5f);
    finger(SDL_FINGERMOTION,41,.6f,.5f);
    finger(SDL_FINGERUP,41,.6f,.5f);
    assert(event_count==2 && events[0]==1 && events[1]==-1);
}
static void cutscene_touch_skip(void) {
    reset();
    _current_loop=CUTSCENE_LOOP;
    touch_bridge_sync();
    int before=action_down[TOUCH_PAUSE], up=action_up[TOUCH_PAUSE];
    finger(SDL_FINGERDOWN,50,.5f,.5f);
    finger(SDL_FINGERMOTION,50,.503f,.503f);
    assert(action_down[TOUCH_PAUSE]==before && !buttons && !mlook_enabled);
    finger(SDL_FINGERUP,50,.504f,.504f);
    assert(action_down[TOUCH_PAUSE]==before+1 && action_up[TOUCH_PAUSE]==up+1);
    finger(SDL_FINGERUP,50,.504f,.504f); /* duplicate release */
    assert(action_down[TOUCH_PAUSE]==before+1 && !buttons && !axes[0] && !axes[1]);
    finger(SDL_FINGERDOWN,51,.5f,.5f);
    finger(SDL_FINGERMOTION,51,.7f,.5f);
    finger(SDL_FINGERUP,51,.5f,.5f);
    assert(action_down[TOUCH_PAUSE]==before+1 && !buttons && !mlook_enabled);
    finger(SDL_FINGERDOWN,52,.5f,.5f);
    finger(SDL_FINGERDOWN,53,.6f,.5f);
    finger(SDL_FINGERUP,52,.5f,.5f);
    finger(SDL_FINGERUP,53,.6f,.5f);
    assert(action_down[TOUCH_PAUSE]==before+1);
    finger(SDL_FINGERDOWN,55,.5f,.5f);
    nx++; touch_bridge_sync();
    finger(SDL_FINGERUP,55,.5f,.5f);
    assert(action_down[TOUCH_PAUSE]==before+1); /* resize invalidates the tap */
    _current_loop=GAME_LOOP;
    touch_bridge_sync();
    finger(SDL_FINGERDOWN,54,.86f,.73f);
    assert(buttons==2); /* gameplay FIRE still works after the movie */
    finger(SDL_FINGERUP,54,.86f,.73f);
}
int main(void) {
    aim_and_actions(); movement_and_lifecycle(); cursor_fallback(); full_surface_and_transition(); alpha2_actions(); cursor_regions(); cutscene_touch_skip();
    puts("TOUCH_BRIDGE_TESTS=PASS (6 groups, Alpha 2 actions, cutscene tap, lifecycle and surface integration)"); return 0;
}
