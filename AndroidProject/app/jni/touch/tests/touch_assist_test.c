#include "touch_assist.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    const TouchAssistRect pair[]={{10,10,20,20,1},{25,10,35,20,1}};
    assert(touch_assist_pick(pair,2,15,15,4)==0);
    assert(touch_assist_pick(pair,2,29,15,4)==1);
    assert(touch_assist_pick(pair,2,22,15,4)==0); /* exact tie is stable */
    assert(touch_assist_pick(pair,2,23,15,4)==1);
    assert(touch_assist_pick(pair,2,40,15,4)==-1);
    assert(touch_assist_pick(pair,2,15,40,4)==-1);
    const TouchAssistRect disabled[]={{10,10,20,20,0},{25,10,35,20,1}};
    assert(touch_assist_pick(disabled,2,15,15,4)==-1);
    assert(touch_assist_pick(disabled,2,29,15,4)==1);
    /* A three-pixel-high inventory strip accepts a finger above its art,
       chooses the nearer neighboring tab, and never escapes its cluster. */
    const TouchAssistRect tabs[]={{90,197,108,200,1},{114,197,132,200,1}};
    const TouchAssistRect strip={87,177,145,200,1};
    const TouchAssistRect assisted[]={{90,177,108,200,1},{114,177,132,200,1}};
    assert(touch_assist_pick_cluster(assisted,2,strip,99,178,12)==0);
    assert(touch_assist_pick_cluster(assisted,2,strip,110,192,12)==0);
    assert(touch_assist_pick_cluster(assisted,2,strip,111,192,12)==1);
    assert(touch_assist_pick_cluster(assisted,2,strip,99,176,12)==-1);
    assert(touch_assist_pick_cluster(tabs,2,strip,150,192,12)==-1);
    TouchAssistDebug debug={0};
    touch_assist_debug_set(&debug,tabs[1],strip,12);
    assert(debug.valid && debug.original.x0==114 && debug.cluster.y0==177);
    assert(debug.effective.x0==102 && debug.effective.y0==185);
    assert(debug.selected_x==123 && debug.selected_y==198);
    puts("TOUCH_ASSIST_TESTS=PASS (nearest, ties, bounds, disabled)");
}
