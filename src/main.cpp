#include"SigAlarmDispacther.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

void print() {
    printf("Toriqe is doing somthing\n");
}

int
main() {
    SigAlarmDispatcher dispatcher;
    dispatcher.start();

    dispatcher.schedule(5000ms, print); // happens on dispatcher:)

    std::this_thread::sleep_for(12s); //main thread
    printf("Waited for 12 seconds");

    dispatcher.stop();
}