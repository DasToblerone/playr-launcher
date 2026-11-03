#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace playr {

// A queue of strings handled one by one on a background thread that starts on demand.
class WorkQueue {
public:
    explicit WorkQueue(std::function<void(const std::wstring&)> handler) : handler_(std::move(handler)) {}

    void Push(const std::vector<std::wstring>& items) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            items_.insert(items_.end(), items.begin(), items.end());
        }
        bool expected = false;
        if (running_.compare_exchange_strong(expected, true)) std::thread([this] { Run(); }).detach();
    }

private:
    void Run() {
        for (;;) {
            std::wstring item;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (items_.empty()) { running_ = false; return; }
                item = items_.front();
                items_.pop_front();
            }
            handler_(item);
        }
    }

    std::function<void(const std::wstring&)> handler_;
    std::mutex mutex_;
    std::deque<std::wstring> items_;
    std::atomic<bool> running_{ false };
};

}  // namespace playr
