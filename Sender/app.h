#pragma once

#include "thread_queue.h"
#include "protocol.h"
#include "dxgi_capture.h"

#include <opencv2/opencv.hpp>
#include <thread>
#include <atomic>
#include <winsock2.h>
#include <vector>


namespace sender {

    struct RawFrame {
        cv::Mat bgr;
        uint64_t frameId = 0;
        uint64_t timestamp = 0;
    };

    struct EncodedFrame {
        std::vector<uint8_t> jpeg;
        uint64_t frameId = 0;
        uint64_t timestamp = 0;
    };

    class App {
    public:
        App() = default;
        ~App();

        bool run(uint16_t port);
        void inputLoop();
        void injectMouse(const common::MouseEvent& e);
        void injectKey(const common::KeyEvent& e);

    private:
        void captureLoop();
        void encodeLoop();
        void sendLoop();

        void stop();

    private:
        std::atomic<bool> running_{ false };

        common::ThreadQueue<RawFrame> rawQueue_{ 100 };
        common::ThreadQueue<EncodedFrame> encQueue_{ 100 };

        std::thread tCapture_;
        std::thread tEncode_;
        std::thread tSend_;
        std::thread tInput_;

        SOCKET server_ = INVALID_SOCKET;
        SOCKET client_ = INVALID_SOCKET;

        DxgiCapture capture_;
        uint32_t rawWidth_ = 0;
        uint32_t rawHeight_ = 0;
    };


}