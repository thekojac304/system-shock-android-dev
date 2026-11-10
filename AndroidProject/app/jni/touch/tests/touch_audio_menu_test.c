#include "touch_controls.h"
#include <assert.h>
#include <stdio.h>
static TouchControls t;
static int changes,saves;
static void output(void *user,TouchOutput e,float x,float y) {
    (void)user;(void)x;(void)y;
    if (e==TOUCH_AUDIO_OPEN) t.audio=(TouchAudioSettings){100,75,100,70,false,true,false};
    if (e==TOUCH_AUDIO_CHANGED) ++changes;
    if (e==TOUCH_AUDIO_SAVE) ++saves;
}
int main(void) {
    touch_init(&t,output,NULL);
    touch_configure(&t,1920,1080,true,true,false);
    touch_ui_open(&t);
    touch_ui_action(&t,TU_AUDIO);
    assert(t.panel==TOUCH_UI_AUDIO && t.audio.master==100 && t.audio.music==75);
    TouchRect r=touch_ui_bounds(&t,TU_AUDIO_MASTER);
    touch_ui_finger(&t,TOUCH_DOWN,1,1,r.x+r.w*.2f,r.y+r.h*.5f);
    assert(t.audio.master<25 && changes==1);
    touch_ui_finger(&t,TOUCH_MOTION,1,1,r.x+r.w*.8f,r.y+r.h*.5f);
    assert(t.audio.master>75 && changes==2);
    touch_ui_finger(&t,TOUCH_UP,1,1,r.x+r.w*.8f,r.y+r.h*.5f);
    assert(saves==1 && !t.ui_owned);
    touch_ui_finger(&t,TOUCH_DOWN,1,2,r.x,r.y+r.h*.5f);
    assert(t.audio.master==0);
    touch_ui_finger(&t,TOUCH_MOTION,1,2,r.x+r.w,r.y+r.h*.5f);
    assert(t.audio.master==100);
    touch_ui_finger(&t,TOUCH_UP,1,2,r.x+r.w,r.y+r.h*.5f);
    touch_ui_action(&t,TU_AUDIO_SOUNDTRACK);
    touch_ui_action(&t,TU_AUDIO_DYNAMIC);
    touch_ui_action(&t,TU_AUDIO_ENABLE);
    assert(t.audio.remake && !t.audio.dynamic && t.audio.enhanced);
    assert(changes==7 && saves==5);
    touch_ui_action(&t,TU_AUDIO_BACK);
    assert(t.panel==TOUCH_UI_SETTINGS);
    puts("TOUCH_AUDIO_MENU_TESTS=PASS (live drag, save, modes, ownership)");
}
