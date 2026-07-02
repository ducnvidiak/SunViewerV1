#include "dxgi_capture.h"

#include <dxgi.h>
#include <opencv2/imgproc.hpp>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace sender {

    // ======================================================
    // INITIALIZE
    // ======================================================

    bool DxgiCapture::initialize(UINT outputIndex)
    {
        valid_ = false;
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(
            __uuidof(IDXGIFactory1),
            (void**)factory.GetAddressOf())))
        {
            return false;
        }

        // ===== Adapter =====
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(0, adapter.GetAddressOf())))
            return false;

        // ===== Device =====
        if (FAILED(D3D11CreateDevice(
            adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            device_.GetAddressOf(),
            nullptr,
            context_.GetAddressOf())))
        {
            return false;
        }

        // ===== Output =====
        ComPtr<IDXGIOutput> output;
        if (FAILED(adapter->EnumOutputs(outputIndex, output.GetAddressOf())))
            return false;

        DXGI_OUTPUT_DESC desc{};
        output->GetDesc(&desc);

        width_ = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
        height_ = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;

        // ===== Output1 =====
        ComPtr<IDXGIOutput1> output1;
        if (FAILED(output.As(&output1)))
            return false;

        // ===== Duplication =====
        if (FAILED(output1->DuplicateOutput(
            device_.Get(),
            duplication_.GetAddressOf())))
            return false;

        // ===== Staging texture =====
        D3D11_TEXTURE2D_DESC td{};
        td.Width = (UINT)width_;
        td.Height = (UINT)height_;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        if (FAILED(device_->CreateTexture2D(&td, nullptr, staging_.GetAddressOf())))
            return false;

        valid_ = true;

        return true;
    }

    // ======================================================
    // CAPTURE FRAME
    // ======================================================

    bool DxgiCapture::capture(cv::Mat& outBgr)
    {
        if (!duplication_ || !valid_)
            return false;

        DXGI_OUTDUPL_FRAME_INFO frameInfo{};
        ComPtr<IDXGIResource> resource;

        HRESULT hr = duplication_->AcquireNextFrame(
            16, // timeout ms
            &frameInfo,
            resource.GetAddressOf());

        // ===== no frame =====
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
            return false;

        // ===== device lost (phải re-init bên ngoài) =====
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            valid_ = false;
            shutdown();
            return false;
        }

        if (FAILED(hr))
            return false;

        bool success = false;

        do {
            ComPtr<ID3D11Texture2D> tex;
            if (FAILED(resource.As(&tex)))
                break;

            // GPU -> CPU
            context_->CopyResource(staging_.Get(), tex.Get());

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context_->Map(
                staging_.Get(),
                0,
                D3D11_MAP_READ,
                0,
                &mapped)))
                break;

            // wrap BGRA
            cv::Mat bgra(height_, width_, CV_8UC4,
                mapped.pData,
                mapped.RowPitch);

            // convert -> BGR (copy)
            cv::cvtColor(bgra, outBgr, cv::COLOR_BGRA2BGR);

            context_->Unmap(staging_.Get(), 0);

            success = true;

        } while (false);

        duplication_->ReleaseFrame();

        return success && !outBgr.empty();
    }

    // ======================================================
    // SHUTDOWN
    // ======================================================

    void DxgiCapture::shutdown()
    {
        staging_.Reset();
        duplication_.Reset();
        context_.Reset();
        device_.Reset();

        width_ = 0;
        height_ = 0;
    }

} // namespace sender