#include "brothumb/pool.h"

#include <chrono>

namespace brothumb {

namespace {
// A worker with nothing to do for this long ends; the pool starts another
// when work comes back.
constexpr auto kIdleExit = std::chrono::seconds(15);
}  // namespace

WorkerThreadPool::WorkerThreadPool(size_t thread_count)
    : max_threads_(thread_count == 0 ? 1 : thread_count) {}

WorkerThreadPool::~WorkerThreadPool() {
    stop();
}

void WorkerThreadPool::spawn_if_needed_locked() {
    if (stopping_ || queue_.size() <= idle_ || live_ >= max_threads_) return;
    // Join the workers that ended idle. They hold no lock on the way out, so
    // joining under this one cannot wait on it.
    for (auto it = workers_.begin(); it != workers_.end();) {
        if (!it->done) { ++it; continue; }
        if (it->thread.joinable()) it->thread.join();
        it = workers_.erase(it);
    }
    workers_.emplace_back();
    Worker* w = &workers_.back();
    ++live_;
    w->thread = std::thread([this, w] { worker_loop(w); });
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
        spawn_if_needed_locked();
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
    std::list<Worker> workers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
        workers.swap(workers_);
    }
    cv_.notify_all();

    for (auto& w : workers) {
        if (w.thread.joinable()) {
            w.thread.join();
        }
    }
}

size_t WorkerThreadPool::thread_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return live_;
}

void WorkerThreadPool::worker_loop(Worker* self) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        ++idle_;
        const bool woke = cv_.wait_for(lock, kIdleExit, [this] { return stopping_ || !queue_.empty(); });
        --idle_;
        if (queue_.empty() && (stopping_ || !woke)) break;
        if (queue_.empty()) continue;

        Item item = std::move(const_cast<Item&>(queue_.top()));
        queue_.pop();
        if (canceled_ids_.find(item.id) != canceled_ids_.end()) {
            item.token.cancel();
        }
        lock.unlock();
        if (item.work) {
            item.work();
        }
        item = Item{};
        lock.lock();
    }
    --live_;
    self->done = true;
}

}  // namespace brothumb
