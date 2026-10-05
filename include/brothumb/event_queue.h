// Multi-producer, single-consumer queue that carries thumbnail events to the host.
//
// Background workers push value snapshots into the queue. The host drains on
// its own thread whenever it likes. Producers never call into host code except
// the optional wake hook, which must be cheap and thread-safe (post a message,
// write an eventfd, SDL_PushEvent, etc.).
#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace brothumb {

template <class T>
class MessageQueue {
public:
    void push(T value) {
        std::function<void()> wake;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            items_.push_back(std::move(value));
            wake = wake_;
        }
        cv_.notify_all();
        if (wake) wake();
    }

    // Removes and returns everything queued, in push order.
    std::vector<T> drain() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<T> out;
        out.swap(items_);
        return out;
    }

    // Blocks until at least one item is queued or the timeout passes.
    bool wait_for(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] { return !items_.empty(); });
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.empty();
    }

    // Called (on the producer's thread) after every push.
    void set_wake(std::function<void()> wake) {
        std::lock_guard<std::mutex> lock(mutex_);
        wake_ = std::move(wake);
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<T> items_;
    std::function<void()> wake_;
};

}  // namespace brothumb
