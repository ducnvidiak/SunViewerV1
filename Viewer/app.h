#pragma once

#include "thread_queue.h"
#include "protocol.h"

#include <opencv2/opencv.hpp>
#include <thread>
#include <atomic>
#include <vector>
#include <winsock2.h>
#include <string>
#include "metrics.h"
#include "process_monitor.h"


namespace viewer {

    struct CompressedFrame {
        std::vector<uint8_t> jpeg;
        uint64_t frameId = 0;
        uint64_t timestamp = 0;
        uint32_t droppedFrames;
    };

    struct DecodedFrame {
        cv::Mat img;
        uint64_t frameId = 0;
        uint64_t timestamp = 0;
        uint32_t droppedFrames;
    };

    struct InputEvent {
        enum Type {
            MouseMove,
            MouseDown,
            MouseUp,
            Key
        };

        Type type;

        int x = 0;
        int y = 0;

        uint32_t key = 0;
        uint32_t action = 0;
    };

    class App {
    public:
        App() = default;
        ~App();

        bool run(const std::string& ip, uint16_t port);
        void handleMouse(int event, int x, int y);
        void sendMouseEvent(int x, int y, uint32_t type, uint32_t button);
        void sendKeyEvent(uint32_t key, uint32_t action);

    private:
        void receiveLoop();
        void decodeLoop();
        void renderLoop();
        void recordLoop();
        void inputLoop();
        void stopRecording();
        void stop();




    private:
        std::atomic<bool> running_{ false };

        common::ThreadQueue<CompressedFrame> compQueue_{ 100 };
        common::ThreadQueue<DecodedFrame> decQueue_{ 100 };
        common::ThreadQueue<DecodedFrame> recordQueue_{ 1000 };
        common::ThreadQueue<InputEvent> inputQueue_{ 200 };

        std::thread tRecv_;
        std::thread tDecode_;
        std::thread tRecord_;
        std::thread tInput_;


        SOCKET socket_ = INVALID_SOCKET;

        common::FpsCounter recvFps_;
        common::FpsCounter decodeFps_;
        common::FpsCounter renderFps_;

        std::atomic<uint64_t> lastTimestamp_{ 0 };
        common::ProcessMonitor processMonitor_;

        int offsetX_ = 0;
        int offsetY_ = 0;
        double scale_ = 1.0;
        int imgW_ = 0;
        int imgH_ = 0;

        uint64_t lastMouseSend_ = 0;
        int lastMouseX_ = -1;
        int lastMouseY_ = -1;

        uint64_t dropReceive_ = 0;
        uint64_t dropDecode_ = 0;
        uint64_t dropRender_ = 0;

        // Sender stats
        uint64_t lastSenderDrop_ = 0;
        uint64_t lastSenderDropCheckTime_ = 0;
        double senderDropPerMin_ = 0.0;

        // Viewer stats
        uint64_t lastViewerDrop_ = 0;
        uint64_t lastViewerDropCheckTime_ = 0;
        double viewerDropPerMin_ = 0.0;


        double cpuDisplay_ = 0;
        double ramDisplay_ = 0;
        uint64_t lastSysStatUpdate_ = 0;

        int origWidth_ = 0;
        int origHeight_ = 0;

        std::atomic<bool> recording_{ true };
    };

}