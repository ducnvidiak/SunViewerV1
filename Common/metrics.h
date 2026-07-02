#pragma once
#include <chrono>
#include <mutex>

namespace common {

    class FpsCounter {
    public:
        void tick() {
            using namespace std::chrono;

            std::lock_guard<std::mutex> lock(mutex_);

            ++count_;
            auto now = steady_clock::now();

            if (!started_) {
                started_ = true;
                start_ = now;
                return;
            }

            auto ms = duration_cast<milliseconds>(now - start_).count();
            if (ms >= 1000) {
                fps_ = count_ * 1000.0 / ms;
                count_ = 0;
                start_ = now;
            }
        }

        double fps() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return fps_;
        }

    private:
        mutable std::mutex mutex_;
        bool started_ = false;
        int count_ = 0;
        double fps_ = 0;
        std::chrono::steady_clock::time_point start_{};
    };

}
