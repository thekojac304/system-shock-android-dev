/* Focused pure logic checks; built for the same Android ABI as the QA app. */
#include "GpuFlicker.h"
#include "GpuPost.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    GpuFlicker state;
    gpu_flicker_reset(&state);
    for(int i=0;i<300;i++)assert(gpu_flicker_sample(&state,4,10)==1.f);
    assert(state.changes==0); /* Stable source never changes its emissive. */
    assert(gpu_flicker_sample(&state,10,10)==1.f);
    assert(gpu_flicker_sample(&state,4,10)==9.f/15.f);
    assert(gpu_flicker_sample(&state,10,10)==1.f);
    gpu_flicker_reset(&state);
    assert(gpu_flicker_sample(&state,0,15)==1.f);
    assert(gpu_flicker_sample(&state,15,15)==1.f);
    assert(gpu_flicker_sample(&state,0,15)==0.f);
    assert(gpu_post_resolve(1,0)==1);
    assert(gpu_post_resolve(6,0)==4);
    assert(gpu_post_resolve(2,0)==0);
    assert(gpu_post_resolve(1,3)==3);
    assert(gpu_post_resolve(6,6)==6);
    assert(gpu_post_resolve(6,99)==4);
    assert(gpu_post_resolve(6,2)==4);
    assert(gpu_post_resolve(1,5)==1);
    assert(gpu_post_resolve(2,3)==0);
    puts("HORROR_HYBRID_TEST=PASS");
    return 0;
}
