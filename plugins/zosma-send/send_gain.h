#pragma once
#include <algorithm>
#include <cmath>

namespace zosma {
// Worker-only gain: both channels share a 20 ms linear-amplitude ramp.
// The host's pass-through audio and real-time callback are untouched.
class SendGain {
public:
    void reset(double db,int sampleRate) noexcept {
        rate_=sampleRate; db_=std::clamp(db,-60.,6.);
        current_=target_=std::pow(10.,db_/20.); remaining_=0; step_=0;
    }
    void setDb(double db) noexcept {
        if(!std::isfinite(db)) return;
        db=std::clamp(db,-60.,6.);
        if(db==db_) return;
        db_=db; target_=std::pow(10.,db/20.);
        remaining_=std::max(1,rate_/50);
        step_=(target_-current_)/remaining_;
    }
    void apply(float* left,float* right,int samples) noexcept {
        for(int i=0;i<samples;++i) {
            if(remaining_>0) {
                current_+=step_;
                if(--remaining_==0) current_=target_;
            }
            left[i]=static_cast<float>(left[i]*current_);
            right[i]=static_cast<float>(right[i]*current_);
        }
    }
private:
    int rate_{48000},remaining_{};
    double db_{},current_{1.},target_{1.},step_{};
};
}
