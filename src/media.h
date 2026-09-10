#pragma once
#include "settings.h"
#include "buffer.h"
#include <d3d11.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
namespace replay {
struct Monitor {
    HMONITOR handle{};
    std::wstring id, label;
    RECT rect{};
};
struct AudioDevice {
    std::wstring id, label;
};
std::vector<Monitor> monitors();
std::vector<AudioDevice> audioDevices();
class ScreenCapture {
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VideoDevice> videoDevice_;
    ComPtr<ID3D11VideoContext> videoContext_;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    ComPtr<ID3D11VideoProcessor> processor_;
    ComPtr<ID3D11Texture2D> input_, output_, staging_;
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
    winrt::event_token closed_{};
    std::atomic<bool> lost_{false};
    int inputWidth_{}, inputHeight_{}, width_{}, height_{};
    bool valid_{};

  public:
    ScreenCapture(HMONITOR monitor, int width, int height);
    ~ScreenCapture();
    bool read(std::vector<uint8_t> &nv12);
};
class Encoder {
    ComPtr<IMFTransform> transform_;
    ComPtr<IMFMediaEventGenerator> events_;
    bool async_{}, video_{};
    int requests_ = 0;
    DWORD inputId_ = 0, outputId_ = 0;
    ComPtr<IMFMediaType> outputType_;
    std::function<void(PacketPtr)> callback_;
    void output();

  public:
    std::wstring name;
    Encoder(bool video, int width, int height, int fps, int bitrate, std::function<void(PacketPtr)> callback);
    ~Encoder();
    void pump();
    bool feed(IMFSample *sample);
    void drain();
    ComPtr<IMFMediaType> type() const;
};
class Loopback {
    ComPtr<IMMDeviceEnumerator> enumerator_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioCaptureClient> capture_;
    WAVEFORMATEX *format_{};
    std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> formatOwner_{nullptr, &CoTaskMemFree};
    int64_t origin_;
    std::wstring selected_;
    std::vector<int16_t> pending_;
    int64_t pendingStart_ = 0, written_ = 0;

  public:
    std::wstring label;
    Loopback(std::wstring const &id, int64_t origin);
    ~Loopback();
    void pump();
    bool defaultChanged() const;
    std::vector<int16_t> block(int64_t frame, int count);
};
void exportMp4(Snapshot const &snapshot, IMFMediaType *videoType, IMFMediaType *audioType,
               std::filesystem::path const &path, std::atomic<bool> const &cancel);
} // namespace replay
