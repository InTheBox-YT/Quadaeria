#pragma once
#include <atomic>
#include <chrono>
#include <deque>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace qc {
class TaskPool {
public:
    using Fn = std::function<void()>;
    void start(unsigned workers) {
        if (workers == 0) workers = 1;
        quit_ = false;
        for (unsigned i = 0; i < workers; ++i) threads_.emplace_back([this] { loop(); });
    }
    void stop() {
        {
            std::lock_guard<std::mutex> lk(m_);
            quit_ = true;
            while (!q_.empty()) q_.pop();
        }
        cv_.notify_all();
        for (auto& t : threads_) t.join();
        threads_.clear();
        std::lock_guard<std::mutex> lk(doneM_);
        done_.clear();
        urgent_.clear();
        pending_ = 0;
    }
    ~TaskPool() {
        if (!threads_.empty()) stop();
    }

    void submit(float priority, Fn work) {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push({priority, seq_++, std::move(work)});
            pending_++;
        }
        cv_.notify_one();
    }
    void post(Fn mainThread, bool urgent = false) {
        std::lock_guard<std::mutex> lk(doneM_);
        (urgent ? urgent_ : done_).push_back(std::move(mainThread));
    }
    int drain(int maxCount = 1 << 30, double budgetMs = 1e9) {
        auto start = std::chrono::steady_clock::now();
        int ran = 0;
        while (ran < maxCount) {
            Fn callback;
            {
                std::lock_guard<std::mutex> lk(doneM_);
                auto& queue = urgent_.empty() ? done_ : urgent_;
                if (queue.empty()) break;
                callback = std::move(queue.front());
                queue.pop_front();
            }
            callback();
            ++ran;
            if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= budgetMs) break;
        }
        return ran;
    }
    int pending() const { return pending_.load(); }
    unsigned workers() const { return unsigned(threads_.size()); }

private:
    struct Item {
        float pri;
        uint64_t seq;
        Fn fn;
        bool operator<(const Item& o) const { return pri != o.pri ? pri > o.pri : seq > o.seq; }
    };
    void loop() {
        for (;;) {
            Item it;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return quit_ || !q_.empty(); });
                if (quit_) return;
                it = std::move(const_cast<Item&>(q_.top()));
                q_.pop();
            }
            it.fn();
            pending_--;
        }
    }
    std::vector<std::thread> threads_;
    std::priority_queue<Item> q_;
    std::mutex m_, doneM_;
    std::condition_variable cv_;
    std::deque<Fn> done_, urgent_;
    std::atomic<int> pending_{0};
    uint64_t seq_ = 0;
    bool quit_ = false;
};

}
