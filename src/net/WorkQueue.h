#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace playr {

// A queue of strings handled by up to `threads` background threads that start on demand and end when it is empty.
class WorkQueue {
public:
    explicit WorkQueue(std::function<void(const std::wstring&)> handler, int threads = 1)
        : handler_(std::move(handler)), threads_(threads) {}

    void Push(const std::vector<std::wstring>& items) {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.insert(items_.end(), items.begin(), items.end());
        while (workers_ < threads_ && workers_ < (int)items_.size()) {
            ++workers_;
            std::thread([this] { Run(); }).detach();
        }
    }

private:
    void Run() {
        for (;;) {
            std::wstring item;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (items_.empty()) { --workers_; return; }
                item = items_.front();
                items_.pop_front();
            }
            handler_(item);
        }
    }

    std::function<void(const std::wstring&)> handler_;
    int threads_;
    std::mutex mutex_;
    std::deque<std::wstring> items_;
    int workers_ = 0;  // running threads, guarded by mutex_
};

}  // namespace playr
