/* Focused pure logic checks; built for the same Android ABI as the QA app. */
#include "GpuFlicker.h"
#include "GpuPost.h"
#include <assert.h>
#include <math.h>
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
    assert(gpu_post_distance_factor(2.f,3.f,12.f)==0.f);
    assert(gpu_post_distance_factor(15.f,3.f,12.f)==1.f);
    assert(fabsf(gpu_post_distance_factor(7.5f,3.f,12.f)-.5f)<.0001f);
    assert(gpu_post_distance_factor(8.f,12.f,3.f)==0.f);
    for(int i=-100;i<=100;i++){
        float distance=i*.25f;
        float factor=gpu_post_distance_factor(distance,2.5f,13.f);
        assert(factor>=0.f&&factor<=1.f);
        assert(factor*.06f<=.06f); /* Far saturation reduction. */
        assert(factor*.035f<=.035f); /* Far contrast reduction. */
    }
    assert(gpu_post_shadow_weight(0.f)==1.f);
    assert(gpu_post_shadow_weight(1.f)==0.f);
    assert(gpu_post_shadow_weight(.2f)>gpu_post_shadow_weight(.8f));
    assert(gpu_post_shadow_weight(0.f)*0.f==0.f); /* Pure black is not tinted. */
    assert(gpu_post_drift(123.f,18.f,0.f)==0.f);
    assert(gpu_post_drift(123.f,0.f,.004f)==0.f);
    for(int i=0;i<10000;i++)assert(fabsf(gpu_post_drift(i*.01f,18.f,.004f))<=.004001f);
    assert(fabsf(gpu_post_drift(3600.f,18.f,.004f))<.00001f);
    puts("HORROR_HYBRID_TEST=PASS");
    return 0;
}
