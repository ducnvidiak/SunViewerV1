#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>

namespace common {

    template<typename T>
    class ThreadQueue {
    public:
        explicit ThreadQueue(size_t maxSize = 100)
            : maxSize_(maxSize) {
        }

        void push(T value) {
            std::unique_lock<std::mutex> lock(mutex_);

            cvFull_.wait(lock, [&] {
                return queue_.size() < maxSize_ || stopped_;
                });

            if (stopped_) return;

            queue_.push(std::move(value));
            cvEmpty_.notify_one();
        }

        void pushDropOldest(T value)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (queue_.size() >= maxSize_) {
                queue_.pop();     // ✅ drop frame cũ nhất
                dropped_++;
            }

            queue_.push(std::move(value));
            cvEmpty_.notify_one();
        }

        bool try_push(T&& item)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (stopped_)
                return false;

            if (queue_.size() >= maxSize_)
                return false;   // ✅ không block → drop

            queue_.push(std::move(item));

            cvEmpty_.notify_one();

            return true;
        }

        void notifyAll()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cvEmpty_.notify_all();
            cvFull_.notify_all();   // nếu có dùng push block
        }


        bool pop(T& out) {
            std::unique_lock<std::mutex> lock(mutex_);

            cvEmpty_.wait(lock, [&] {
                return !queue_.empty() || stopped_;
                });

            if (queue_.empty()) return false;

            out = std::move(queue_.front());
            queue_.pop();

            cvFull_.notify_one();
            return true;
        }

        bool popRecord(T& out, std::atomic<bool>& recording) {
            std::unique_lock<std::mutex> lock(mutex_);

            cvEmpty_.wait(lock, [&] {
                return !queue_.empty() || stopped_ || !recording;
                });

            if (queue_.empty()) return false;

            out = std::move(queue_.front());
            queue_.pop();

            cvFull_.notify_one();
            return true;
        }

        bool try_pop(T& out)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (queue_.empty())
                return false;

            out = std::move(queue_.front());
            queue_.pop();

            return true;
        }


        void stop() {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
            cvEmpty_.notify_all();
            cvFull_.notify_all();
        }

        size_t size() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.size();
        }

        uint64_t droppedCount() const {
            return dropped_.load();
        }

        void resetDropped()
        {
            dropped_ = 0;
        }


    private:
        std::queue<T> queue_;
        size_t maxSize_;
        std::atomic<uint64_t> dropped_{ 0 };
        bool stopped_ = false;

        mutable std::mutex mutex_;
        std::condition_variable cvEmpty_;
        std::condition_variable cvFull_;
    };

}