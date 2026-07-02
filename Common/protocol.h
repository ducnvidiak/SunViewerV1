#pragma once
#include <cstdint>

namespace common {

    inline constexpr uint32_t kMagic = 0x56554E53; // SNUV
    inline constexpr uint16_t kVersion = 1;

    enum class MessageType : uint16_t {
        Frame = 1,
        Heartbeat = 2,
        MouseEvent = 3,
        KeyEvent = 4

    };

#pragma pack(push, 1)

    struct MessageHeader {
        uint32_t magic;
        uint16_t version;
        uint16_t type;
        uint32_t size;
    };

    struct FrameHeader {
        uint64_t frameId;
        uint64_t timestamp;
        uint32_t imageSize;
        uint32_t droppedFrames;


        uint32_t origWidth;
        uint32_t origHeight;

    };

    struct MouseEvent {
        int x;
        int y;
        uint32_t type;   // move, down, up
        uint32_t button; // left/right
    };

    struct KeyEvent {
        uint32_t key;     // virtual key
        uint32_t action;  // down=1, up=0
    };


#pragma pack(pop)

    inline bool isValidHeader(const MessageHeader& h) {
        return h.magic == kMagic && h.version == kVersion;
    }

}