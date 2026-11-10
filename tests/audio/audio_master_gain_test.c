#include "../../SourceSnapshot/src/MacSrc/AudioMasterGain.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    const int steps[]={100,75,50,25,10,0};
    const int expected[]={128,54,23,10,6,0};
    for (int i=0;i<6;++i) assert(AudioVolumeScale(steps[i],128)==expected[i]);
    assert(AudioVolumeScale(-50,128)==0 && AudioVolumeScale(150,128)==128);
    for (int p=1;p<=100;++p)
        assert(AudioVolumeGain(p)>AudioVolumeGain(p-1));
    /* Simulated final mix includes sources from multiple categories. */
    int16_t mix[]={10000,-8000,1200,0};
    AudioMasterGainS16(mix,sizeof(mix),50);
    assert(mix[0]>1700 && mix[0]<1900 && mix[1]< -1300 && mix[1]> -1500 && mix[2]>200 && mix[2]<220 && mix[3]==0);
    AudioMasterGainS16(mix,sizeof(mix),0);
    assert(mix[0]==0 && mix[1]==0 && mix[2]==0);
    int16_t neutral[]={10000,-10000};
    AudioMasterGainS16(neutral,sizeof(neutral),100);
    assert(neutral[0]==10000 && neutral[1]==-10000);
    puts("AUDIO_MASTER_GAIN_TESTS=PASS (all-source final bus, mute, neutral)");
}
