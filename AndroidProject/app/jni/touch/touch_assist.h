#ifndef TOUCH_ASSIST_H
#define TOUCH_ASSIST_H
/* Logical UI coordinates. Call only from the touch tap path; the original
   mouse dispatch and visible rectangles remain untouched. */
typedef struct { int x0,y0,x1,y1,eligible; } TouchAssistRect;
typedef struct {
    TouchAssistRect original, effective, cluster;
    int touch_x,touch_y,selected_x,selected_y,valid;
    int index; /* one-based item number within a semantic strip, when known */
    const char *kind; /* static diagnostic label; never used for hit testing */
} TouchAssistDebug;
static inline int touch_assist_in_cluster(TouchAssistRect c,int x,int y) {
    return x>=c.x0 && x<c.x1 && y>=c.y0 && y<c.y1;
}
static inline int touch_assist_distance_sq(TouchAssistRect r,int x,int y) {
    int dx=x<r.x0?r.x0-x:x>=r.x1?x-r.x1+1:0;
    int dy=y<r.y0?r.y0-y:y>=r.y1?y-r.y1+1:0;
    return dx*dx+dy*dy;
}
static inline int touch_assist_pick(const TouchAssistRect *r,int count,int x,int y,int radius) {
    int best=-1, best_distance=radius*radius+1;
    for (int i=0;i<count;++i) {
        if (!r[i].eligible || r[i].x1<=r[i].x0 || r[i].y1<=r[i].y0) continue;
        int d=touch_assist_distance_sq(r[i],x,y);
        if (d<best_distance) { best=i; best_distance=d; }
    }
    return best_distance<=radius*radius?best:-1;
}
/* A semantic strip/grid has one bounded input area. Gaps resolve to the
   nearest eligible rectangle; exact ties retain stable array order. */
static inline int touch_assist_pick_cluster(const TouchAssistRect *r,int count,
                                            TouchAssistRect cluster,int x,int y,int radius) {
    if (!touch_assist_in_cluster(cluster,x,y)) return -1;
    return touch_assist_pick(r,count,x,y,radius);
}
static inline void touch_assist_debug_set(TouchAssistDebug *out,TouchAssistRect original,
                                          TouchAssistRect cluster,int radius) {
    if (!out) return;
    out->original=original;
    out->cluster=cluster;
    out->effective=(TouchAssistRect){original.x0-radius,original.y0-radius,
                                     original.x1+radius,original.y1+radius,1};
    if (out->effective.x0<cluster.x0) out->effective.x0=cluster.x0;
    if (out->effective.y0<cluster.y0) out->effective.y0=cluster.y0;
    if (out->effective.x1>cluster.x1) out->effective.x1=cluster.x1;
    if (out->effective.y1>cluster.y1) out->effective.y1=cluster.y1;
    out->selected_x=(original.x0+original.x1)/2;
    out->selected_y=(original.y0+original.y1)/2;
    out->valid=1;
}
#endif
