#pragma once
#include <cstdint>

namespace common::config {

	inline constexpr std::uint16_t kDefaultPort = 5000;

	// Network
	inline constexpr int kSendBufferSize = 1 * 1024 * 1024;
	inline constexpr int kRecvBufferSize = 1 * 1024 * 1024;

	// Queue
	inline constexpr size_t kMaxQueueSize = 100;

	// JPEG
	inline constexpr int kJpegQuality = 70;

	// Capture
	inline constexpr int kTargetFps = 30;

	// Window
	inline constexpr bool kShowOverlay = true;

}