// Priority-based worker thread pool with cancellation support.
#pragma once

#include "brothumb/common.h"
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>
#include <vector>

namespace brothumb {

// Threads start when work arrives and none is free, up to `thread_count`,
// and end after a while idle, so a pool nobody uses holds no threads.
class WorkerThreadPool {
public:
    explicit WorkerThreadPool(size_t thread_count = 2);
    ~WorkerThreadPool();

    WorkerThreadPool(const WorkerThreadPool&) = delete;
    WorkerThreadPool& operator=(const WorkerThreadPool&) = delete;

    // Enqueues a task with priority and cancellation token.
    void enqueue(RequestId id, Priority priority, CancellationToken token,
                 std::function<void()> work);

    // Marks a pending or in-flight request as canceled.
    void cancel(RequestId id);

    // Shuts down the thread pool and waits for active workers to finish.
    void stop();

    // Worker threads alive now (at most the count it was made with).
    size_t thread_count() const;
    size_t max_thread_count() const { return max_threads_; }

private:
    struct Item {
        RequestId id = 0;
        Priority priority = Priority::Normal;
        uint64_t seq = 0;
        CancellationToken token;
        std::function<void()> work;

        // Higher priority first; on tie, lower sequence number (FIFO) first
        bool operator<(const Item& other) const {
            if (priority != other.priority) {
                return static_cast<int>(priority) < static_cast<int>(other.priority);
            }
            return seq > other.seq;
        }
    };

    struct Worker {
        std::thread thread;
        bool done = false;  // its loop has returned: join it
    };

    void worker_loop(Worker* self);
    void spawn_if_needed_locked();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::priority_queue<Item> queue_;
    std::unordered_set<RequestId> canceled_ids_;
    std::list<Worker> workers_;
    size_t max_threads_ = 2;
    size_t live_ = 0;
    size_t idle_ = 0;
    uint64_t next_seq_ = 0;
    bool stopping_ = false;
};

}  // namespace brothumb
