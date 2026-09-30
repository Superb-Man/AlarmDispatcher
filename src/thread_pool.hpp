#pragma once 

#include <cstddef>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <algorithm>

int default_worker_count() {
    const int hardware_threads = std::thread::hardware_concurrency();
    // A receiver is I/O-bound, but an unbounded number of workers is unsafe.
    return std::max<int>(2, std::min<int>(8, hardware_threads == 0 ? 2 : hardware_threads));
}

class ThreadPool {
public:
    using Task = std::function<void()>; // for simplicity of

    explicit 
    ThreadPool(int worker_count = 0, int max_queue_size = 128) {
        if (worker_count == 0) {
            worker_count = default_worker_count();
        }

        workers_.reserve(worker_count);
        try {
            for (int i = 0; i < worker_count; ++i) {
                workers_.emplace_back(&ThreadPool::worker_loop, this);
            }
        } catch(...) {
            stop();
            throw;
        }
    }

    ~ThreadPool() {
        stop(false);
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    
    bool 
    enqueue(Task task) {
        if (!task) {
            return false;
        }

        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (stopping_ || tasks_.size() >= max_queue_size_) {
                return false;
            }
            tasks_.emplace(std::move(task));
        }
        work_available_.notify_one();
        return true;
    }

    // Stops accepting work and joins all workers. When drain is false, queued
    // work is discarded; tasks already running are allowed to finish.
    void 
    stop(bool drain = true) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (stopping_) {
                return;
            }
            stopping_ = true;
            drain_ = drain;
            if (!drain_) {
                std::queue<Task> empty;
                tasks_.swap(empty);
            }
        }

        work_available_.notify_all();

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

private:
    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
    std::mutex mutex_;
    std::condition_variable work_available_;
    int max_queue_size_;
    bool stopping_ = false;
    bool drain_ = true;

    void 
    worker_loop() {
        for(;;) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                work_available_.wait(lock, [this] {
                    return stopping_ || !tasks_.empty(); // sleep untill threads are not stopping and no tasks
                });

                if (stopping_ && (!drain_ || tasks_.empty())) {
                    return;
                }

                task = std::move(tasks_.front());
                tasks_.pop();
            }

            // A bad connection must not permanently remove a worker from the pool.
            try {
                task();
            } catch (...) {
            }
        }
    }
};
