// ✅ PHẢI LÀ NHƯ NÀY - ĐẶT Ở ĐẦU FILE
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// ✅ Định nghĩa này NGĂN windows.h include winsock.h
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

// ✅ Include winsock2.h TRƯỚC windows.h
#include <winsock2.h>
#include <ws2tcpip.h>

// ✅ Sau đó mới đến windows.h
#include <windows.h>

#include "app.h"
#include "net_utils.h"
#include "logger.h"
#include "config.h"

#include <chrono>

using namespace sender;

namespace {
    uint64_t nowUs() {
        using namespace std::chrono;
        return duration_cast<microseconds>(
            system_clock::now().time_since_epoch()).count();
    }
}

App::~App() {
    stop();
}

void App::stop() {
    running_ = false;

    rawQueue_.stop();
    encQueue_.stop();

    if (tCapture_.joinable()) tCapture_.join();
    if (tEncode_.joinable()) tEncode_.join();
    if (tSend_.joinable())   tSend_.join();

    common::closeSocket(client_);
    common::closeSocket(server_);

    client_ = INVALID_SOCKET;
    server_ = INVALID_SOCKET;
}

void App::captureLoop()
{
    if (!capture_.initialize(0)) {
        common::log_error("DXGI init failed");
        running_ = false;
        return;
    }

    uint64_t id = 0;

    while (running_) {

        RawFrame f;

        if (!capture_.capture(f.bgr)) {

            // ✅ Nếu DXGI bị mất → re-init
            if (!capture_.isValid()) {

                common::log_warn("DXGI lost → reinitializing");

                capture_.shutdown();

                // retry cho đến khi OK
                while (running_ && !capture_.initialize(0)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            else {
                // ✅ chỉ là timeout bình thường
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            continue;
        }

        f.frameId = id++;
        f.timestamp = nowUs();


        rawWidth_ = f.bgr.cols;
        rawHeight_ = f.bgr.rows;


        rawQueue_.pushDropOldest(std::move(f));
    }

    capture_.shutdown();
}



void App::encodeLoop()
{
    const std::vector<int> params = {
        cv::IMWRITE_JPEG_QUALITY,
        common::config::kJpegQuality
    };

    while (running_) {
        RawFrame raw;

        if (!rawQueue_.pop(raw)) break;

        EncodedFrame enc;
        enc.frameId = raw.frameId;
        enc.timestamp = raw.timestamp;

        // ✅ FIX 4: resize trước khi encode
        double scale = 1; // hoặc từ config


        if (raw.bgr.empty())
            continue;

        cv::Mat small;
        cv::resize(raw.bgr, small, cv::Size(), scale, scale);


        if (!cv::imencode(".jpg", small, enc.jpeg, params)) {
            continue;
        }

        encQueue_.pushDropOldest(std::move(enc));
    }
}

void App::sendLoop()
{
    bool firstFrame = true;

    while (running_) {
        EncodedFrame f;

        if (!encQueue_.pop(f)) break;


        if (firstFrame) {
            rawQueue_.resetDropped();
            encQueue_.resetDropped();

            common::log_info("Dropped counter reset at first frame");

            firstFrame = false;
        }

        common::MessageHeader header{};
        header.magic = common::kMagic;
        header.version = common::kVersion;
        header.type = (uint16_t)common::MessageType::Frame;
        header.size =
            sizeof(common::FrameHeader) +
            (uint32_t)f.jpeg.size();

        common::FrameHeader fh{};
        fh.frameId = f.frameId;
        fh.timestamp = f.timestamp;
        fh.imageSize = (uint32_t)f.jpeg.size();


        fh.origWidth = rawWidth_;   // 👈 cần set đúng
        fh.origHeight = rawHeight_;


        uint64_t droppedNow = rawQueue_.droppedCount();

        common::log_info(
            "Send frame " + std::to_string(f.frameId) +
            " | dropped=" + std::to_string(droppedNow)
        );

        fh.droppedFrames = (uint32_t)droppedNow;


        if (!common::sendAll(client_, &header, sizeof(header)) ||
            !common::sendAll(client_, &fh, sizeof(fh)) ||
            !common::sendAll(client_, f.jpeg.data(), f.jpeg.size()))
        {
            common::log_error("Send failed");
            running_ = false;
            break;
        }

        common::log_info("Sent frame " + std::to_string(f.frameId));
    }
}


bool App::run(uint16_t port)
{
    if (!common::initWinsock()) {
        common::log_error("WSAStartup failed");
        return false;
    }

    server_ = common::createServer(port);
    if (server_ == INVALID_SOCKET) {
        common::log_error("Server create failed");
        return false;
    }

    common::log_info("Waiting for client...");

    client_ = common::acceptClient(server_);

    if (client_ == INVALID_SOCKET) {
        common::log_error("Accept failed");
        return false;
    }

    // ✅ FIX 3 (SENDER SIDE CŨNG QUAN TRỌNG)
    int bufSize = 64 * 1024;
    setsockopt(client_, SOL_SOCKET, SO_SNDBUF,
        (char*)&bufSize, sizeof(bufSize));


    common::log_info("Client connected");

    rawQueue_.resetDropped();
    encQueue_.resetDropped();

    running_ = true;

    tCapture_ = std::thread(&App::captureLoop, this);
    tEncode_ = std::thread(&App::encodeLoop, this);
    tSend_ = std::thread(&App::sendLoop, this);
    tInput_ = std::thread(&App::inputLoop, this);

    tCapture_.join();
    tEncode_.join();
    tSend_.join();
    tInput_.join();

    stop();
    common::cleanupWinsock();

    return true;
}

void App::inputLoop()
{
    while (running_) {

        common::MessageHeader header{};

        if (!common::recvAll(client_, &header, sizeof(header)))
            break;

        if (!common::isValidHeader(header))
            break;

        if ((common::MessageType)header.type == common::MessageType::MouseEvent) {

            common::MouseEvent ev;
            if (!common::recvAll(client_, &ev, sizeof(ev))) break;

            injectMouse(ev);
        }
        else if ((common::MessageType)header.type == common::MessageType::KeyEvent) {

            common::KeyEvent ev;
            if (!common::recvAll(client_, &ev, sizeof(ev))) break;

            injectKey(ev);
        }
        else {
            std::vector<uint8_t> skip(header.size);
            common::recvAll(client_, skip.data(), skip.size());
        }
    }
}

void App::injectMouse(const common::MouseEvent& e)
{
    INPUT input{};
    input.type = INPUT_MOUSE;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    input.mi.dx = e.x * 65535 / screenW;
    input.mi.dy = e.y * 65535 / screenH;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

    SendInput(1, &input, sizeof(INPUT));

    if (e.type == 1) {
        input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &input, sizeof(INPUT));
    }
    if (e.type == 2) {
        input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &input, sizeof(INPUT));
    }
}

void App::injectKey(const common::KeyEvent& e)
{
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = (WORD)e.key;

    if (e.action == 0)
        input.ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(1, &input, sizeof(INPUT));
}