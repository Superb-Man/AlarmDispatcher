#include"SigAlarmDispacther.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

void print() {
    printf("Toriqe is doing somthing\n");
}


void largeDelayPrint() {
    std::this_thread::sleep_for(9s);
    for (int i = 1; i < 100000; i++) {}
    printf("Ending this largeDelayPrint\n");
}
void print2() {
    std::this_thread::sleep_for(3s);
    for (int i = 1; i < 100000; i++) {}
    printf("Ending this task2->print2\n");
}

int
main() {
    SigAlarmDispatcher dispatcher;
    dispatcher.start();
    // logical flow
    // start at t = 0

    dispatcher.schedule(3000ms, largeDelayPrint); // this would be shown t = 3 + 9 = 12
    
    // without thread_pool, the largeDelay will run at t = 3, and block the Dispacther thread for 9 seconds
    // so later scheduled functions will have to wait despite they have earlier firing time.
    // but the largeDelay() is already fired and thread is blocked


    std::this_thread::sleep_for(2s); // block main thread for 2 seconds

    //resumes at t = 2

    dispatcher.schedule(3000ms, print); // will be shown at t= 5
    dispatcher.schedule(3000ms, print); // will be shown at t= 5
    dispatcher.schedule(3000ms, print); // will be shown at t= 5


    dispatcher.schedule(3000ms, print2); // will be shown at t = 8

    std::this_thread::sleep_for(11s); //main thread
    printf("Waited for 11 seconds---> exiting on 13 seconds logical \n");

    dispatcher.stop();
}