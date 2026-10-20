#include "../send_gain.h"
#include <array>
#include <cstdlib>
#include <iostream>

void require(bool value) { if(!value) { std::cerr<<"Send gain test failed\n"; std::abort(); } }
bool near(double a,double b) { return std::abs(a-b)<.000002; }
int main() {
    for(int rate:{44100,48000,96000}) {
        zosma::SendGain gain;
        gain.reset(0,rate);
        float left[]{-.7f,0.f,.3f,1.5f}, right[]{.4f,-.6f,.8f,2.f};
        const std::array<float,4> originalL{-.7f,0.f,.3f,1.5f},originalR{.4f,-.6f,.8f,2.f};
        gain.apply(left,right,4);
        for(int i=0;i<4;++i) require(left[i]==originalL[i] && right[i]==originalR[i]);
        for(double db:{-6.,6.,-60.,0.}) {
            gain.setDb(db);
            const int length=rate/50;
            float last=left[3];
            for(int i=0;i<length;++i) {
                float a=1.f,b=-1.f;
                // Repeated packet targets must not restart smoothing.
                gain.setDb(db); gain.apply(&a,&b,1);
                require(a==-b && std::isfinite(a));
                if(i>0) require(std::abs(a-last)<.003f);
                last=a;
            }
            float a=1.f,b=.5f;gain.apply(&a,&b,1);
            const double expected=std::pow(10.,db/20.);
            require(near(a,expected) && near(b,expected*.5));
        }
        gain.reset(-6,rate);
        float a=1.f,b=1.f; gain.apply(&a,&b,1);
        require(near(a,std::pow(10.,-6./20.))); // saved gain applies immediately on start
    }
    std::cout<<"Send gain: unity transparency, dB scaling, stereo link and 20 ms ramps at 44.1/48/96 kHz passed\n";
}
