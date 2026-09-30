#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <fcntl.h>
#include <functional>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <sys/select.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

/**
 * Author - Kazi Istiak Toriqe
 * Co-Author - Google/Internet
 * Date created - 30-09-2026
 * This is a timer Dispatcher.
 */
class SigAlarmDispatcher {
public:
    using Callback = std::function<void()>; // the function pointer/reference

    SigAlarmDispatcher(): running_(false) {
        if (pipe(pipe_fds_) != 0) {
            throw std::runtime_error("pipe failed");
        }

        fcntl(pipe_fds_[0], F_SETFL, O_NONBLOCK);
        fcntl(pipe_fds_[1], F_SETFL, O_NONBLOCK);

        instance_ = this;

        struct sigaction sa {}; // empty signal action
        sa.sa_handler = &SigAlarmDispatcher::alarm_handler; // registering the alram handler
        sigemptyset(&sa.sa_mask); // masking which signals will be blocked during SIGALRM firing
        sa.sa_flags = 0; // No special behaviour

        if (sigaction(SIGALRM, &sa, nullptr) != 0) {
            close(pipe_fds_[0]);
            close(pipe_fds_[1]);
            throw std::runtime_error("sigaction failed");
        }
    }

    ~SigAlarmDispatcher() {
        stop();

        if (instance_ == this) {
            instance_ = nullptr;
        }

        close(pipe_fds_[0]);
        close(pipe_fds_[1]);
    }

    void 
    start() {
        bool expected = false;

        if (!running_.compare_exchange_strong(expected, true)) {
            return;
        }

        worker_ = std::thread(&SigAlarmDispatcher::worker_loop, this); // start this thread from main entry point of application
    }

    void 
    stop() {
        bool expected = true;

        if (!running_.compare_exchange_strong(expected, false)) {
            return;
        }

        wake_worker();

        if (worker_.joinable()) {
            worker_.join();
        }

        disarm_timer();
    }

    void 
    schedule(std::chrono::milliseconds delay, Callback callback) { // register a callback function after an interval
        Timer timer;

        timer.deadline = std::chrono::steady_clock::now() + delay;

        timer.callback = std::move(callback);

        {
            std::lock_guard<std::mutex> lock(mutex_);

            timers_.push(std::move(timer));
        }

        wake_worker(); // the SIGALRM will wake up the thread and thread will invoke the function
    }

private:
    
    int pipe_fds_[2]; // 0 for reading and 1 for writing
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::mutex mutex_;

    struct Timer {
        std::chrono::steady_clock::time_point deadline;
        Callback callback;
    };

    struct TimerCompare {
        bool 
        operator()(const Timer& a, const Timer& b) const {
            return a.deadline > b.deadline;
        }
    };

    std::priority_queue<Timer, std::vector<Timer>, TimerCompare> timers_;
    static inline SigAlarmDispatcher* instance_ = nullptr; // SigAlarmDispatcher will be a Object

    /**
     * This is called by SIGALRM trigger from setitimer
     * writes tto the file descriptor
     * Thread wake up from sleep / exits from the wait_for_event
     */
    static void 
    alarm_handler(int) {
        if (instance_ == nullptr) {
            return;
        }

        const uint8_t byte = 1;
        ssize_t result = write(instance_->pipe_fds_[1], &byte, sizeof(byte));

        (void)result;
    }

    void 
    wake_worker() {
        const uint8_t byte = 1;
        // writes to the file descriptor
        // the select(..) will check that pipe has data through read descriptor
        // it will return from the wait_for_event() and 
        // drain the pipe
        ssize_t result = write(pipe_fds_[1], &byte, sizeof(byte)); 

        (void)result;
    }

    void 
    worker_loop() {
        while (running_) {
            wait_for_event(); // will sleep here until event arrives

            if (!running_) {
                break;
            }

            drain_pipe();

            process_expired_timers(); // pop the timers
            arm_next_timer();
        }
    }

    /**
     * Create a set of file descriptors
     * Should not consume CPU/busy waiting 
     */
    void 
    wait_for_event() {
        fd_set read_fds; 

        FD_ZERO(&read_fds);
        FD_SET(pipe_fds_[0], &read_fds);

        int result;

        do {
            result = select(
                pipe_fds_[0] + 1, // upper limit
                &read_fds,
                nullptr,
                nullptr,
                nullptr
            );
        } while (result < 0 && errno == EINTR); // For interrupt condition

    }

    void 
    drain_pipe() {
        uint8_t buffer[64];

        while (read(pipe_fds_[0], buffer, sizeof(buffer)) > 0) {

        }
    }

    void 
    process_expired_timers() {
        while (true) {
            Callback callback;

            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (timers_.empty()) {
                    return;
                }

                auto now = std::chrono::steady_clock::now();

                if (timers_.top().deadline > now) {
                    return;
                }

                callback = std::move(timers_.top().callback);

                timers_.pop();
            }

            callback(); // invoke the scheduled function
        }
    }

    void arm_next_timer() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (timers_.empty()) {
            disarm_timer();
            return;
        }

        auto now = std::chrono::steady_clock::now();

        auto duration = timers_.top().deadline - now;

        auto microseconds =std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

        if (microseconds <= 0) {
            microseconds = 1;
        }

        struct itimerval timer {};

        timer.it_value.tv_sec = microseconds / 1'000'000;

        timer.it_value.tv_usec = microseconds % 1'000'000;

        setitimer(ITIMER_REAL, &timer, nullptr);
    }

    void disarm_timer() {
        struct itimerval timer {};

        setitimer(ITIMER_REAL, &timer,nullptr);
    }
};