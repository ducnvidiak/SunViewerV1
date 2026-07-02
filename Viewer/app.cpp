#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX          
#endif

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "app.h"
#include "net_utils.h"
#include "logger.h"

#include <chrono>

using namespace viewer;

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
    common::log_warn("[App] stop() called");
    running_ = false;


    // ✅ CRITICAL: chờ recordLoop finish
    while (recording_ || recordQueue_.size() > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (tRecv_.joinable())  tRecv_.join();
    if (tDecode_.joinable()) tDecode_.join();
    if (tRecord_.joinable()) tRecord_.join();
    if (tInput_.joinable()) tInput_.join();

    compQueue_.stop();
    decQueue_.stop();
    inputQueue_.stop();

    common::closeSocket(socket_);
    socket_ = INVALID_SOCKET;
}

void App::receiveLoop()
{
    while (running_) {

        // -------- read message header --------
        common::MessageHeader header{};

        if (!common::recvAll(socket_, &header, sizeof(header))) {
            common::log_error("Failed recv header");
            running_ = false;
            break;
        }

        if (!common::isValidHeader(header)) {
            common::log_error("Invalid header");
            running_ = false;
            break;
        }

        if ((common::MessageType)header.type != common::MessageType::Frame) {
            // skip unknown
            std::vector<uint8_t> skip(header.size);
            if (!common::recvAll(socket_, skip.data(), skip.size()))
                break;
            continue;
        }

        // -------- read frame header --------
        common::FrameHeader fh{};

        if (!common::recvAll(socket_, &fh, sizeof(fh))) {
            running_ = false;
            break;
        }

        if (fh.imageSize == 0 || fh.imageSize > 20 * 1024 * 1024) {
            common::log_error("Invalid image size");
            running_ = false;
            break;
        }

        // -------- read JPEG bytes --------
        CompressedFrame f;

        f.jpeg.resize(fh.imageSize);

        if (!common::recvAll(socket_, f.jpeg.data(), f.jpeg.size())) {
            running_ = false;
            break;
        }

        f.frameId = fh.frameId;
        f.timestamp = fh.timestamp;
        f.droppedFrames = fh.droppedFrames;
        origWidth_ = fh.origWidth;
        origHeight_ = fh.origHeight;


        uint64_t ts = f.timestamp;

        if (!compQueue_.try_push(std::move(f))) {
            // drop frame
            dropReceive_++;
        }
        //compQueue_.push(std::move(f));
        static int counter = 0;
        if (++counter % 30 == 0) {
            printf("compQueue size = %d\n", (int)compQueue_.size());
        }


        recvFps_.tick();                  // tăng count FPS receive
        lastTimestamp_ = ts;    // lưu timestamp từ Sender
    }
}

void App::decodeLoop()
{
    while (running_ || compQueue_.size() > 0) {
        CompressedFrame f;

        if (!compQueue_.pop(f)) break;

        DecodedFrame out;
        out.frameId = f.frameId;
        out.timestamp = f.timestamp;

        if (decQueue_.size() > 80) {
            // skip decode → giảm CPU
            continue;
        }

        out.img = cv::imdecode(f.jpeg, cv::IMREAD_COLOR);
        if (out.img.empty())
            continue;

        // ✅ copy sang record queue (không drop)
        //common::log_info("Push to recordQueue");

        if (recording_)
        {
            recordQueue_.push({ out.img.clone(), out.frameId, out.timestamp });
        }

        decodeFps_.tick();

        if (!decQueue_.try_push(std::move(out))) {
            dropDecode_++;  // ✅ hoặc decDrop_
        }

        static int counter2 = 0;
        if (++counter2 % 30 == 0) {
            printf("decQueue size = %d\n", (int)decQueue_.size());
        }


    }
}

void App::stopRecording()
{
    recording_ = false;
    recordQueue_.notifyAll();
    common::log_info("Stop recording requested");
}

void releaseVideo(cv::VideoWriter& writer, uint64_t totalFrames = 0)
{
    // ✅ FINALIZE — LUÔN CHẠY
    common::log_info("Start release video!");

    if (writer.isOpened())
    {
        writer.release();   // 🔥 QUAN TRỌNG NHẤT
    }

    common::log_info("Finished release video!");

    common::log_info(
        "Recording finished. Total frames: " +
        std::to_string(totalFrames)
    );
}

void App::recordLoop()
{
    common::log_info("Record thread started");

    cv::VideoWriter writer;
    bool initialized = false;

    uint64_t totalFrames = 0;

    while (true)
    {
        // ✅ exit condition rõ ràng (KHÔNG phụ thuộc while)
        if (!recording_ && recordQueue_.size() == 0)
        {
            break;
        }

        DecodedFrame f;

        if (!recordQueue_.popRecord(f, recording_))
        {
            // ✅ CRITICAL: xử lý pop fail đúng cách
            if (!recording_ && recordQueue_.size() == 0)
            {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (f.img.empty())
            continue;

        // ✅ INIT writer
        if (!initialized)
        {
            writer.open(
                "output.mp4",
                cv::VideoWriter::fourcc('a', 'v', 'c', '1'),
                60,
                cv::Size(f.img.cols, f.img.rows)
            );

            if (!writer.isOpened())
            {
                common::log_error("VideoWriter open failed");
                continue;
            }

            common::log_info("Recording started: output.mp4");
            initialized = true;
        }

        // ✅ WRITE FRAME
        if (recording_)
        {
            writer.write(f.img);
            totalFrames++;
        }

        if (totalFrames % 60 == 0)
        {
            common::log_info(
                "Recorded frames: " + std::to_string(totalFrames)
            );
        }
    }

    common::log_info("break loop 1 -----------------------------------");

    releaseVideo(writer, totalFrames);

    common::log_info("[Record] Thread exiting cleanly");
}


void App::handleMouse(int event, int x, int y)
{
    double scale = scale_;
    int offsetX = offsetX_;
    int offsetY = offsetY_;
    int imgW = imgW_;
    int imgH = imgH_;

    if (scale <= 0.0001)
        return;

    int localX = x - offsetX;
    int localY = y - offsetY;

    if (localX < 0 || localY < 0 ||
        localX >= imgW * scale ||
        localY >= imgH * scale)
    {
        return;
    }

    int remoteX = (int)((double)localX / scale_ + 0.5);
    int remoteY = (int)((double)localY / scale_ + 0.5);

    // ✅ MAP theo resolution gốc
    if (origWidth_ > 0 && origHeight_ > 0)
    {
        double scaleBackX = (double)origWidth_ / imgW_;
        double scaleBackY = (double)origHeight_ / imgH_;

        remoteX = (int)(remoteX * scaleBackX + 0.5);
        remoteY = (int)(remoteY * scaleBackY + 0.5);
    }

    remoteX = std::clamp(remoteX, 0, origWidth_ - 1);
    remoteY = std::clamp(remoteY, 0, origHeight_ - 1);

    uint64_t now = nowUs();

    if (event == cv::EVENT_MOUSEMOVE) {
        if (remoteX == lastMouseX_ && remoteY == lastMouseY_)
            return;

        if (now - lastMouseSend_ < 5000)
            return;

        lastMouseSend_ = now;
        lastMouseX_ = remoteX;
        lastMouseY_ = remoteY;

        inputQueue_.try_push({
            InputEvent::MouseMove,
            remoteX, remoteY
            });
    }

    if (event == cv::EVENT_LBUTTONDOWN)
        inputQueue_.try_push({
            InputEvent::MouseDown,
            remoteX, remoteY
            });

    if (event == cv::EVENT_LBUTTONUP)
        inputQueue_.try_push({
            InputEvent::MouseUp,
            remoteX, remoteY
            });
}

void App::sendMouseEvent(int x, int y, uint32_t type, uint32_t button)
{
    common::MessageHeader header{};
    header.magic = common::kMagic;
    header.version = common::kVersion;
    header.type = (uint16_t)common::MessageType::MouseEvent;
    header.size = sizeof(common::MouseEvent);

    common::MouseEvent ev{};
    ev.x = x;
    ev.y = y;
    ev.type = type;
    ev.button = button;

    common::sendAll(socket_, &header, sizeof(header));
    common::sendAll(socket_, &ev, sizeof(ev));
}

void App::sendKeyEvent(uint32_t key, uint32_t action)
{
    common::MessageHeader header{};
    header.magic = common::kMagic;
    header.version = common::kVersion;
    header.type = (uint16_t)common::MessageType::KeyEvent;
    header.size = sizeof(common::KeyEvent);

    common::KeyEvent ev{};
    ev.key = key;
    ev.action = action;

    common::sendAll(socket_, &header, sizeof(header));
    common::sendAll(socket_, &ev, sizeof(ev));
}

void App::renderLoop()
{
    cv::namedWindow("Viewer", cv::WINDOW_NORMAL);
    cv::setMouseCallback("Viewer", [](int event, int x, int y, int flags, void* userdata) {
        App* app = static_cast<App*>(userdata);
        app->handleMouse(event, x, y);
        }, this);

    while (running_) {

        DecodedFrame f;

        // ✅ drop frame cũ → realtime
        while (decQueue_.size() > 1) {
            DecodedFrame tmp;

            if (decQueue_.pop(tmp)) {
                dropRender_++;   // ✅ ADD
            }

        }

        if (!decQueue_.pop(f)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // ✅ update FPS render
        renderFps_.tick();

        cv::Mat frame = f.img;

        // ========================
        // 1. FPS
        // ========================
        std::string line1 =
            "FPS: Rcv=" + std::to_string((int)recvFps_.fps()) +
            " Dec=" + std::to_string((int)decodeFps_.fps()) +
            " Rnd=" + std::to_string((int)renderFps_.fps());

        // ========================
        // 2. Latency
        // ========================
        int latencyMs = (int)((nowUs() - f.timestamp) / 1000);

        std::string line2 =
            "Latency: " + std::to_string(latencyMs) + " ms";

        // ========================
        // 3. Queue size
        // ========================
        std::string line3 =
            "Queue: Comp=" + std::to_string(compQueue_.size()) +
            " Dec=" + std::to_string(decQueue_.size()) +
            " Rec=" + std::to_string(recordQueue_.size());

        uint64_t nowUsFull = nowUs();
        uint64_t nowSec = nowUsFull / 1000000;

        uint64_t nowSys = nowUsFull;   // ✅ lấy thời gian hiện tại (microseconds)

        // ✅ chỉ update mỗi 500ms
        if (lastSysStatUpdate_ == 0 ||
            nowSys - lastSysStatUpdate_ >= 500000)
        {
            auto ps = processMonitor_.getStats();

            // ✅ EMA smoothing (làm mượt)
            cpuDisplay_ = 0.8 * cpuDisplay_ + 0.2 * ps.cpuPercent;
            ramDisplay_ = 0.8 * ramDisplay_ + 0.2 * ps.workingSetMB;

            lastSysStatUpdate_ = nowSys;  // ✅ cập nhật thời điểm cuối
        }

        // ✅ dùng giá trị đã smooth
        std::string line4 =
            "CPU: " + std::to_string((int)cpuDisplay_) + "%" +
            "  RAM: " + std::to_string((int)ramDisplay_) + " MB";


        // ===== Sender drop rate =====
        if (lastSenderDropCheckTime_ == 0) {
            lastSenderDropCheckTime_ = nowSec;
            lastSenderDrop_ = f.droppedFrames;
        }
        else if (nowSec - lastSenderDropCheckTime_ >= 1) {
            uint64_t deltaDrop = f.droppedFrames - lastSenderDrop_;
            uint64_t deltaTime = nowSec - lastSenderDropCheckTime_;

            if (deltaTime > 0) {
                double newRate = (double)deltaDrop * 60.0 / (double)deltaTime;
                senderDropPerMin_ = 0.8 * senderDropPerMin_ + 0.2 * newRate;
            }

            lastSenderDrop_ = f.droppedFrames;
            lastSenderDropCheckTime_ = nowSec;
        }

        // ===== Viewer drop rate =====
        uint64_t totalViewerDrop_ =
            dropReceive_ + dropDecode_ + dropRender_;

        if (lastViewerDropCheckTime_ == 0) {
            lastViewerDropCheckTime_ = nowSec;
            lastViewerDrop_ = totalViewerDrop_;
        }
        else if (nowSec - lastViewerDropCheckTime_ >= 1) {
            uint64_t deltaDrop = totalViewerDrop_ - lastViewerDrop_;
            uint64_t deltaTime = nowSec - lastViewerDropCheckTime_;

            if (deltaTime > 0) {
                double newRate = (double)deltaDrop * 60.0 / (double)deltaTime;
                viewerDropPerMin_ = 0.8 * viewerDropPerMin_ + 0.2 * newRate;
            }

            lastViewerDrop_ = totalViewerDrop_;
            lastViewerDropCheckTime_ = nowSec;
        }

        std::string line5 =
            "S: " + std::to_string(f.droppedFrames) +
            " (" + std::to_string((int)senderDropPerMin_) + "/m)"
            "  V: " + std::to_string(totalViewerDrop_) +
            " (" + std::to_string((int)viewerDropPerMin_) + "/m)"

            " [Rcv:" + std::to_string(dropReceive_) +
            " Dec:" + std::to_string(dropDecode_) +
            " Rnd:" + std::to_string(dropRender_) + "]";



        // ========================
        // 4. Show
        // ========================

        cv::Rect winRect = cv::getWindowImageRect("Viewer");

        int winW = std::max(1, winRect.width);
        int winH = std::max(1, winRect.height);

        if (winW < 10 || winH < 10)
            continue;

        int imgW = frame.cols;
        int imgH = frame.rows;


        scale_ = std::min(
            (double)winW / imgW,
            (double)winH / imgH
        );


        imgW_ = imgW;
        imgH_ = imgH;

        int newW = std::max(1, static_cast<int>(imgW * scale_));
        int newH = std::max(1, static_cast<int>(imgH * scale_));

        cv::Mat resized;

        if (imgW != newW || imgH != newH)
            cv::resize(frame, resized, cv::Size(newW, newH), 0, 0, cv::INTER_LINEAR);
        else
            resized = frame;


        cv::Mat canvas(winH, winW, CV_8UC3, cv::Scalar(20, 20, 20));

        int x = (winW - newW) / 2;
        int y = (winH - newH) / 2;

        offsetX_ = x;
        offsetY_ = y;

        cv::Rect roi(x, y, newW, newH);
        roi &= cv::Rect(0, 0, winW, winH);

        resized(cv::Rect(0, 0, roi.width, roi.height))
            .copyTo(canvas(roi));

        // ========================
        // 5. Background panel
        // ========================
        // ===== overlay mờ =====
        cv::Mat overlay = canvas.clone();

        // vẽ rectangle lên overlay
        cv::rectangle(
            overlay,
            cv::Rect(10, 10, 420, 170),
            cv::Scalar(0, 0, 0),
            cv::FILLED
        );

        // alpha blending
        double alpha = 0.3;  // 0 = trong suốt, 1 = đậm
        cv::addWeighted(overlay, alpha, canvas, 1 - alpha, 0, canvas);


        // ========================
        // 6. Draw text
        // ========================
        cv::putText(canvas, line1, { 20, 35 },
            cv::FONT_HERSHEY_SIMPLEX, 0.6,
            cv::Scalar(0, 255, 0), 2);

        cv::putText(canvas, line2, { 20, 65 },
            cv::FONT_HERSHEY_SIMPLEX, 0.6,
            cv::Scalar(0, 255, 255), 2);

        cv::putText(canvas, line3, { 20, 95 },
            cv::FONT_HERSHEY_SIMPLEX, 0.6,
            cv::Scalar(255, 200, 0), 2);

        cv::putText(canvas, line4, { 20, 125 },
            cv::FONT_HERSHEY_SIMPLEX, 0.6,
            cv::Scalar(180, 255, 180), 2);

        cv::putText(canvas, line5, { 20, 155 },
            cv::FONT_HERSHEY_SIMPLEX, 0.6,
            cv::Scalar(0, 0, 255), 2);


        cv::imshow("Viewer", canvas);

        int key = cv::waitKey(1);

        if (key == 27) {
            running_ = false;
            break;
        }


        // ✅ nhấn F1 để dừng recording
        if (key == 'r' || key == 'R') // F1
        {
            common::log_info("R pressed!");

            stopRecording();

            continue;
        }

        if (key > 0 && key != 255) {
            uint32_t vk = (uint32_t)key;

            // ✅ mapping chữ thường → chữ hoa
            if (vk >= 'a' && vk <= 'z')
                vk = vk - 'a' + 'A';

            // phím đặc biệt OpenCV → VK
            if (vk == 13) vk = VK_RETURN;
            if (vk == 8)  vk = VK_BACK;
            if (vk == 9)  vk = VK_TAB;

            inputQueue_.try_push({
                InputEvent::Key,
                0, 0,
                vk, 1
                });

            inputQueue_.try_push({
                InputEvent::Key,
                0, 0,
                vk, 0
                });
        }

    }
    running_ = false;
    cv::destroyAllWindows();
}

void App::inputLoop()
{
    while (running_)
    {
        InputEvent ev;

        if (!inputQueue_.pop(ev))
            break;

        switch (ev.type)
        {


        case InputEvent::MouseMove:
        {
            InputEvent latest = ev;

            // lấy event mới nhất (bỏ event cũ)
            while (inputQueue_.try_pop(ev)) {
                if (ev.type == InputEvent::MouseMove)
                    latest = ev;
                else {
                    // xử lý luôn event khác
                    switch (ev.type)
                    {
                    case InputEvent::MouseDown:
                        sendMouseEvent(ev.x, ev.y, 1, 1);
                        break;
                    case InputEvent::MouseUp:
                        sendMouseEvent(ev.x, ev.y, 2, 1);
                        break;
                    case InputEvent::Key:
                        sendKeyEvent(ev.key, ev.action);
                        break;
                    }

                    break;
                }
            }

            sendMouseEvent(latest.x, latest.y, 0, 0);
            break;
        }


        case InputEvent::MouseDown:
            sendMouseEvent(ev.x, ev.y, 1, 1);
            break;

        case InputEvent::MouseUp:
            sendMouseEvent(ev.x, ev.y, 2, 1);
            break;

        case InputEvent::Key:
            sendKeyEvent(ev.key, ev.action);
            break;

        }
    }
}

bool App::run(const std::string& ip, uint16_t port)
{
    if (!common::initWinsock()) {
        common::log_error("WSA init failed");
        return false;
    }

    socket_ = common::connectToServer(ip, port);
    if (socket_ == INVALID_SOCKET) {
        common::log_error("Connect failed");
        return false;
    }


    // ✅ FIX 3: giảm TCP receive buffer
    int bufSize = 64 * 1024; // 64KB (nhỏ hơn mặc định)
    setsockopt(socket_, SOL_SOCKET, SO_RCVBUF,
        (char*)&bufSize, sizeof(bufSize));

    common::log_info("Connected to sender");

    running_ = true;

    tRecv_ = std::thread(&App::receiveLoop, this);
    tDecode_ = std::thread(&App::decodeLoop, this);
    tRecord_ = std::thread(&App::recordLoop, this);
    tInput_ = std::thread(&App::inputLoop, this);

    renderLoop();

    stop();
    common::cleanupWinsock();

    return true;
}

