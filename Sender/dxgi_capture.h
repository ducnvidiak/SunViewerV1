#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <opencv2/core.hpp>

namespace sender {

    class DxgiCapture
    {
    public:
        bool initialize(UINT outputIndex = 0);
        bool capture(cv::Mat& outBgr);
        void shutdown();
        bool isValid() const { return valid_; }

    private:
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;

        int width_ = 0;
        int height_ = 0;
        bool valid_ = false;
    };

}