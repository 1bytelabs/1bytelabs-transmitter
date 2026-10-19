#include "../audio_queue.h"
#include <cstdlib>
#include <iostream>
#include <thread>

void require(bool value) { if(!value) { std::cerr << "Queue test failed\n"; std::abort(); } }
int main() {
    zosma::AudioQueue queue;
    queue.prepare(4);
    const double left[]{1,2,3},right[]{4,5,6};
    float l[3]{},r[3]{};
    require(queue.push(left,right,3));
    require(!queue.push(left,right,2)); // full: reject the entire block
    require(!queue.pop(l,r,4));
    require(queue.pop(l,r,2));
    require(l[0]==1 && l[1]==2 && r[0]==4 && r[1]==5);
    require(queue.push(left,right,3,true,false)); // wraparound and silence
    require(queue.pop(l,r,3));
    require(l[0]==3 && l[1]==0 && l[2]==0 && r[0]==6 && r[1]==4 && r[2]==5);
    queue.discard(); require(queue.available()==0);
    require(queue.push<float>(nullptr,nullptr,2));
    require(queue.pop(l,r,2) && l[0]==0 && r[1]==0);

    queue.prepare(127);
    constexpr int total=300000;
    std::thread producer([&] {
        for(int i=0;i<total;) {
            float a=static_cast<float>(i),b=-a;
            if(queue.push(&a,&b,1)) ++i;
            else std::this_thread::yield();
        }
    });
    for(int i=0;i<total;) {
        float a{},b{};
        if(queue.pop(&a,&b,1)) {
            require(a==static_cast<float>(i) && b==-a); ++i;
        } else std::this_thread::yield();
    }
    producer.join(); require(queue.available()==0);
    std::cout << "Queue: overflow, wraparound, silence, float conversion and concurrent ordering passed\n";
}
