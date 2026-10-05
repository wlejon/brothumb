#include "brothumb/pool.h"

namespace brothumb {

WorkerThreadPool::WorkerThreadPool(size_t thread_count) {
    if (thread_count == 0) thread_count = 1;
    threads_.reserve(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        threads_.emplace_back(&WorkerThreadPool::worker_loop, this);
    }
}

WorkerThreadPool::~WorkerThreadPool() {
    stop();
}

void WorkerThreadPool::enqueue(RequestId id, Priority priority,
                               CancellationToken token, std::function<void()> work) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        Item item;
        item.id = id;
        item.priority = priority;
        item.seq = next_seq_++;
        item.token = std::move(token);
        item.work = std::move(work);
        queue_.push(std::move(item));
    }
    cv_.notify_one();
}

void WorkerThreadPool::cancel(RequestId id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        canceled_ids_.insert(id);
    }
    cv_.notify_all();
}

void WorkerThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    cv_.notify_all();

    for (auto& th : threads_) {
        if (th.joinable()) {
            th.join();
        }
    }
    threads_.clear();
}

void WorkerThreadPool::worker_loop() {
    while (true) {
        Item item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });

            if (stopping_ && queue_.empty()) {
                return;
            }

            if (!queue_.empty()) {
                item = std::move(const_cast<Item&>(queue_.top()));
                queue_.pop();
            } else {
                continue;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (canceled_ids_.find(item.id) != canceled_ids_.end()) {
                item.token.cancel();
            }
        }

        if (item.work) {
            item.work();
        }
    }
}

}  // namespace brothumb
