#include "media.h"
#include <dxgi1_6.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <propvarutil.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wmcodecdsp.h>
#include <ksmedia.h>

namespace replay {
std::vector<Monitor> monitors() {
    std::vector<Monitor> result;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR h, HDC, LPRECT, LPARAM data) -> BOOL {
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            GetMonitorInfoW(h, &info);
            auto &v = *reinterpret_cast<std::vector<Monitor> *>(data);
            auto r = info.rcMonitor;
            v.push_back({h, info.szDevice,
                         std::wstring(info.szDevice) + L"  " + std::to_wstring(r.right - r.left) + L"×" +
                             std::to_wstring(r.bottom - r.top),
                         r});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    return result;
}
std::vector<AudioDevice> audioDevices() {
    std::vector<AudioDevice> result;
    ComPtr<IMMDeviceEnumerator> e;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e)));
    ComPtr<IMMDeviceCollection> devices;
    check(e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices));
    UINT count{};
    check(devices->GetCount(&count));
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> d;
        check(devices->Item(i, &d));
        LPWSTR id{};
        check(d->GetId(&id));
        std::wstring key = id;
        CoTaskMemFree(id);
        ComPtr<IPropertyStore> props;
        check(d->OpenPropertyStore(STGM_READ, &props));
        PROPVARIANT v{};
        check(props->GetValue(PKEY_Device_FriendlyName, &v));
        result.push_back({key, v.vt == VT_LPWSTR ? v.pwszVal : L"오디오 장치"});
        PropVariantClear(&v);
    }
    return result;
}
ScreenCapture::ScreenCapture(HMONITOR monitor, int width, int height) : width_(width), height_(height) {
    using namespace winrt::Windows::Graphics::Capture;
    if (!GraphicsCaptureSession::IsSupported())
        throw std::runtime_error("이 환경에서 화면 캡처를 사용할 수 없습니다.");
    ComPtr<IDXGIFactory1> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    for (UINT a = 0;; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND)
            break;
        for (UINT o = 0;; ++o) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND)
                break;
            ComPtr<IDXGIOutput6> modern;
            if (SUCCEEDED(output.As(&modern))) {
                DXGI_OUTPUT_DESC1 desc{};
                check(modern->GetDesc1(&desc));
                if (desc.Monitor == monitor && desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
                    throw std::runtime_error(
                        "HDR 화면은 현재 지원하지 않습니다. Windows 설정에서 HDR을 끄고 다시 시작하십시오.");
            }
        }
    }
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                            D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0,
                            D3D11_SDK_VERSION, &device_, nullptr, &context_));
    check(device_.As(&videoDevice_));
    check(context_.As(&videoContext_));
    ComPtr<IDXGIDevice> dxgi;
    check(device_.As(&dxgi));
    ComPtr<IInspectable> inspectable;
    check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), &inspectable));
    auto d = winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice{nullptr};
    check(inspectable->QueryInterface(winrt::guid_of<decltype(d)>(), winrt::put_abi(d)));
    auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    check(interop->CreateForMonitor(monitor, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item_)));
    auto size = item_.Size();
    inputWidth_ = size.Width;
    inputHeight_ = size.Height;
    closed_ = item_.Closed([this](auto const &, auto const &) { lost_ = true; });
    pool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(
        d, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
    session_ = pool_.CreateCaptureSession(item_);
    session_.IsCursorCaptureEnabled(true);
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputWidth = inputWidth_;
    content.InputHeight = inputHeight_;
    content.OutputWidth = width_;
    content.OutputHeight = height_;
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    check(videoDevice_->CreateVideoProcessorEnumerator(&content, &enumerator_));
    check(videoDevice_->CreateVideoProcessor(enumerator_.Get(), 0, &processor_));
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = inputWidth_;
    desc.Height = inputHeight_;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    check(device_->CreateTexture2D(&desc, nullptr, &input_));
    desc.Width = width_;
    desc.Height = height_;
    desc.Format = DXGI_FORMAT_NV12;
    check(device_->CreateTexture2D(&desc, nullptr, &output_));
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(device_->CreateTexture2D(&desc, nullptr, &staging_));
    session_.StartCapture();
}
ScreenCapture::~ScreenCapture() {
    try {
        if (item_)
            item_.Closed(closed_);
        if (session_)
            session_.Close();
        if (pool_)
            pool_.Close();
    } catch (...) {
    }
}
bool ScreenCapture::read(std::vector<uint8_t> &nv12) {
    if (lost_)
        throw std::runtime_error("캡처 대상이 연결 해제되었습니다.");
    auto frame = pool_.TryGetNextFrame();
    if (frame) {
        auto size = frame.ContentSize();
        if (size.Width != inputWidth_ || size.Height != inputHeight_)
            throw std::runtime_error("화면 크기가 변경되었습니다. 녹화를 다시 시작하십시오.");
        auto access =
            frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> texture;
        check(access->GetInterface(IID_PPV_ARGS(&texture)));
        context_->CopyResource(input_.Get(), texture.Get());
        valid_ = true;
        frame.Close();
    }
    if (!valid_)
        return false;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv{};
    iv.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorInputView> inView;
    check(videoDevice_->CreateVideoProcessorInputView(input_.Get(), enumerator_.Get(), &iv, &inView));
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ov{};
    ov.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorOutputView> outView;
    check(videoDevice_->CreateVideoProcessorOutputView(output_.Get(), enumerator_.Get(), &ov, &outView));
    RECT source{0, 0, inputWidth_, inputHeight_};
    double scale = std::min(double(width_) / inputWidth_, double(height_) / inputHeight_);
    LONG w = static_cast<LONG>(inputWidth_ * scale) & ~1L, h = static_cast<LONG>(inputHeight_ * scale) & ~1L;
    RECT dest{(width_ - w) / 2, (height_ - h) / 2, (width_ + w) / 2, (height_ + h) / 2};
    videoContext_->VideoProcessorSetStreamSourceRect(processor_.Get(), 0, TRUE, &source);
    videoContext_->VideoProcessorSetStreamDestRect(processor_.Get(), 0, TRUE, &dest);
    D3D11_VIDEO_COLOR bg{};
    bg.YCbCr.Y = 0.0625f;
    bg.YCbCr.Cb = 0.5f;
    bg.YCbCr.Cr = 0.5f;
    bg.YCbCr.A = 1;
    videoContext_->VideoProcessorSetOutputBackgroundColor(processor_.Get(), TRUE, &bg);
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputColor{};
    inputColor.RGB_Range = 0;
    inputColor.YCbCr_Matrix = 1;
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputColor{};
    outputColor.YCbCr_Matrix = 1;
    outputColor.Nominal_Range = 1;
    videoContext_->VideoProcessorSetStreamColorSpace(processor_.Get(), 0, &inputColor);
    videoContext_->VideoProcessorSetOutputColorSpace(processor_.Get(), &outputColor);
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = inView.Get();
    check(videoContext_->VideoProcessorBlt(processor_.Get(), outView.Get(), 0, 1, &stream));
    context_->CopyResource(staging_.Get(), output_.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    nv12.resize(static_cast<size_t>(width_) * height_ * 3 / 2);
    for (int y = 0; y < height_ * 3 / 2; ++y)
        memcpy(nv12.data() + size_t(y) * width_,
               static_cast<BYTE *>(mapped.pData) + size_t(y) * mapped.RowPitch, width_);
    context_->Unmap(staging_.Get(), 0);
    return true;
}

namespace {
void codecValue(IMFTransform *t, GUID const &key, ULONG value, bool boolean = false) {
    ComPtr<ICodecAPI> c;
    if (SUCCEEDED(t->QueryInterface(IID_PPV_ARGS(&c)))) {
        VARIANT v{};
        v.vt = static_cast<VARTYPE>(boolean ? VT_BOOL : VT_UI4);
        if (boolean)
            v.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
        else
            v.ulVal = value;
        c->SetValue(&key, &v);
    }
}
ComPtr<IMFMediaType> videoType(GUID subtype, int w, int h, int fps, int bitrate) {
    ComPtr<IMFMediaType> t;
    check(MFCreateMediaType(&t));
    check(t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    check(t->SetGUID(MF_MT_SUBTYPE, subtype));
    check(MFSetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, w, h));
    check(MFSetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, fps, 1));
    check(MFSetAttributeRatio(t.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    check(t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    if (subtype == MFVideoFormat_H264) {
        check(t->SetUINT32(MF_MT_AVG_BITRATE, bitrate));
        check(t->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Base));
    }
    return t;
}
} // namespace
Encoder::Encoder(bool video, int width, int height, int fps, int bitrate,
                 std::function<void(PacketPtr)> callback)
    : video_(video), callback_(std::move(callback)) {
    if (video) {
        MFT_REGISTER_TYPE_INFO in{MFMediaType_Video, MFVideoFormat_NV12},
            out{MFMediaType_Video, MFVideoFormat_H264};
        IMFActivate **activations{};
        UINT count{};
        check(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, &in,
                        &out, &activations, &count));
        for (UINT i = 0; i < count; ++i) {
            if (!transform_ && SUCCEEDED(activations[i]->ActivateObject(IID_PPV_ARGS(&transform_)))) {
                LPWSTR n{};
                UINT len{};
                if (SUCCEEDED(activations[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &n, &len))) {
                    name = n;
                    CoTaskMemFree(n);
                }
            }
            activations[i]->Release();
        }
        CoTaskMemFree(activations);
        if (!transform_) {
            check(CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&transform_)));
            name = L"Microsoft H.264 (소프트웨어)";
        }
    } else {
        check(
            CoCreateInstance(CLSID_AACMFTEncoder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&transform_)));
        name = L"Microsoft AAC";
    }
    ComPtr<IMFAttributes> attrs;
    if (SUCCEEDED(transform_->GetAttributes(&attrs))) {
        UINT32 a{};
        attrs->GetUINT32(MF_TRANSFORM_ASYNC, &a);
        async_ = a != 0;
        if (async_)
            check(attrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE));
    }
    if (async_)
        check(transform_.As(&events_));
    HRESULT ids = transform_->GetStreamIDs(1, &inputId_, 1, &outputId_);
    if (FAILED(ids) && ids != E_NOTIMPL)
        check(ids);
    if (video) {
        codecValue(transform_.Get(), CODECAPI_AVLowLatencyMode, 1, true);
        codecValue(transform_.Get(), CODECAPI_AVEncMPVDefaultBPictureCount, 0);
        codecValue(transform_.Get(), CODECAPI_AVEncMPVGOPSize, fps);
        auto out = videoType(MFVideoFormat_H264, width, height, fps, bitrate);
        check(transform_->SetOutputType(outputId_, out.Get(), 0));
        auto in = videoType(MFVideoFormat_NV12, width, height, fps, bitrate);
        check(transform_->SetInputType(inputId_, in.Get(), 0));
    } else {
        ComPtr<IMFMediaType> in;
        check(MFCreateMediaType(&in));
        check(in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
        check(in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
        check(in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2));
        check(in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000));
        check(in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
        check(in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4));
        check(in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000));
        check(transform_->SetInputType(inputId_, in.Get(), 0));
        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> out;
            check(transform_->GetOutputAvailableType(outputId_, i, &out));
            UINT32 rate{}, channels{}, bps{};
            out->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
            out->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
            out->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bps);
            if (rate == 48000 && channels == 2 && bps == 24000) {
                check(transform_->SetOutputType(outputId_, out.Get(), 0));
                break;
            }
        }
    }
    check(transform_->GetOutputCurrentType(outputId_, &outputType_));
    check(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
    check(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
}
Encoder::~Encoder() {
    if (transform_)
        transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
}
void Encoder::output() {
    MFT_OUTPUT_STREAM_INFO info{};
    check(transform_->GetOutputStreamInfo(outputId_, &info));
    MFT_OUTPUT_DATA_BUFFER out{};
    out.dwStreamID = outputId_;
    ComPtr<IMFSample> sample;
    if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
        check(MFCreateSample(&sample));
        ComPtr<IMFMediaBuffer> buffer;
        check(MFCreateAlignedMemoryBuffer(std::max<DWORD>(info.cbSize, 1024 * 1024),
                                          info.cbAlignment ? info.cbAlignment - 1 : 0, &buffer));
        check(sample->AddBuffer(buffer.Get()));
        out.pSample = sample.Get();
    }
    DWORD status{};
    HRESULT hr = transform_->ProcessOutput(0, 1, &out, &status);
    if (out.pEvents)
        out.pEvents->Release();
    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        return;
    if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
        ComPtr<IMFMediaType> type;
        check(transform_->GetOutputAvailableType(outputId_, 0, &type));
        check(transform_->SetOutputType(outputId_, type.Get(), 0));
        outputType_ = type;
        return;
    }
    check(hr);
    if (!sample)
        sample.Attach(out.pSample);
    if (!sample)
        return;
    auto p = std::make_shared<Packet>();
    p->video = video_;
    check(sample->GetSampleTime(&p->pts));
    sample->GetSampleDuration(&p->duration);
    UINT32 key{};
    sample->GetUINT32(MFSampleExtension_CleanPoint, &key);
    p->key = key != 0;
    ComPtr<IMFMediaBuffer> buffer;
    check(sample->ConvertToContiguousBuffer(&buffer));
    BYTE *data{};
    DWORD bytes{};
    check(buffer->Lock(&data, nullptr, &bytes));
    p->bytes.assign(data, data + bytes);
    buffer->Unlock();
    if (p->duration <= 0)
        p->duration = video_ ? Second / 30 : 1024 * Second / 48000;
    callback_(p);
}
void Encoder::pump() {
    if (!async_)
        return;
    for (;;) {
        ComPtr<IMFMediaEvent> e;
        HRESULT hr = events_->GetEvent(MF_EVENT_FLAG_NO_WAIT, &e);
        if (hr == MF_E_NO_EVENTS_AVAILABLE)
            break;
        check(hr);
        HRESULT status{};
        check(e->GetStatus(&status));
        check(status);
        MediaEventType type{};
        check(e->GetType(&type));
        if (type == METransformNeedInput)
            ++requests_;
        else if (type == METransformHaveOutput)
            output();
    }
}
bool Encoder::feed(IMFSample *sample) {
    pump();
    if (async_ && requests_ <= 0)
        return false;
    HRESULT hr = transform_->ProcessInput(inputId_, sample, 0);
    if (hr == MF_E_NOTACCEPTING) {
        if (!async_)
            output();
        return false;
    }
    check(hr);
    if (async_)
        --requests_;
    else
        output();
    return true;
}
void Encoder::drain() {
    transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, inputId_);
    check(transform_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0));
    for (int i = 0; i < 30; ++i) {
        if (async_) {
            pump();
            Sleep(2);
        } else
            output();
    }
}
ComPtr<IMFMediaType> Encoder::type() const {
    return outputType_;
}

Loopback::Loopback(std::wstring const &id, int64_t origin) : origin_(origin) {
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator_)));
    if (id.empty())
        check(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_));
    else
        check(enumerator_->GetDevice(id.c_str(), &device_));
    LPWSTR key{};
    check(device_->GetId(&key));
    selected_ = key;
    CoTaskMemFree(key);
    check(device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client_));
    check(client_->GetMixFormat(&format_));
    formatOwner_.reset(format_);
    if (format_->nChannels > 2)
        throw std::runtime_error(
            "현재 오디오 장치를 Windows에서 스테레오로 설정하십시오. 다중 채널 믹스는 지원하지 않습니다.");
    if (format_->wBitsPerSample != 16 && format_->wBitsPerSample != 24 && format_->wBitsPerSample != 32)
        throw std::runtime_error("지원하지 않는 오디오 형식입니다. 16/24/32비트 장치를 선택하십시오.");
    check(client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, Second / 5, 0, format_,
                              nullptr));
    check(client_->GetService(IID_PPV_ARGS(&capture_)));
    check(client_->Start());
    label = L"시스템 소리 연결됨";
}
Loopback::~Loopback() {
    if (client_)
        client_->Stop();
}
bool Loopback::defaultChanged() const {
    ComPtr<IMMDevice> d;
    if (FAILED(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &d)))
        return true;
    LPWSTR id{};
    if (FAILED(d->GetId(&id)))
        return true;
    bool changed = selected_ != id;
    CoTaskMemFree(id);
    return changed;
}
void Loopback::pump() {
    UINT32 frames{};
    check(capture_->GetNextPacketSize(&frames));
    while (frames) {
        BYTE *data{};
        DWORD flags{};
        UINT64 pos{}, qpc{};
        check(capture_->GetBuffer(&data, &frames, &flags, &pos, &qpc));
        const int rate = static_cast<int>(format_->nSamplesPerSec), channels = format_->nChannels,
                  bits = format_->wBitsPerSample;
        bool floating = format_->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        if (format_->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
            floating = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(format_)->SubFormat ==
                       KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        int64_t start = (static_cast<int64_t>(qpc) - origin_) * 48000 / Second;
        if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
            start = (clockNow() - origin_) * 48000 / Second - frames * 48000 / rate;
        int count = static_cast<int>(uint64_t(frames) * 48000 / rate);
        auto value = [&](int f, int c) -> float {
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                return 0;
            auto at = data + size_t(f) * format_->nBlockAlign + std::min(c, channels - 1) * bits / 8;
            if (floating && bits == 32) {
                float x;
                memcpy(&x, at, 4);
                return std::isfinite(x) ? x : 0;
            }
            if (bits == 16) {
                int16_t x;
                memcpy(&x, at, 2);
                return x / 32768.f;
            }
            if (bits == 32) {
                int32_t x;
                memcpy(&x, at, 4);
                return x / 2147483648.f;
            }
            if (bits == 24) {
                int32_t x = (int32_t(at[2]) << 24) | (int32_t(at[1]) << 16) | (int32_t(at[0]) << 8);
                return x / 2147483648.f;
            }
            return 0;
        };
        if (pending_.empty())
            pendingStart_ = std::max(written_, start);
        int64_t finish = start + count,
                existingEnd = pendingStart_ + static_cast<int64_t>(pending_.size() / 2);
        if (finish > pendingStart_ && finish - pendingStart_ < 48000 * 2) {
            if (finish > existingEnd)
                pending_.resize(static_cast<size_t>(finish - pendingStart_) * 2, 0);
            for (int i = 0; i < count; ++i) {
                int64_t at = start + i;
                if (at < pendingStart_ || at < written_)
                    continue;
                double source = double(i) * rate / 48000;
                int lo = std::min(int(source), int(frames) - 1), hi = std::min(lo + 1, int(frames) - 1);
                float f = float(source - lo);
                for (int c = 0; c < 2; ++c) {
                    float v = value(lo, c) * (1 - f) + value(hi, c) * f;
                    pending_[static_cast<size_t>(at - pendingStart_) * 2 + c] =
                        static_cast<int16_t>(std::clamp(v, -1.f, 0.999969f) * 32768);
                }
            }
        }
        check(capture_->ReleaseBuffer(frames));
        check(capture_->GetNextPacketSize(&frames));
    }
}
std::vector<int16_t> Loopback::block(int64_t frame, int count) {
    std::vector<int16_t> out(size_t(count) * 2, 0);
    int64_t end = pendingStart_ + static_cast<int64_t>(pending_.size() / 2);
    for (int i = 0; i < count; ++i)
        if (frame + i >= pendingStart_ && frame + i < end) {
            size_t at = static_cast<size_t>(frame + i - pendingStart_) * 2;
            out[size_t(i) * 2] = pending_[at];
            out[size_t(i) * 2 + 1] = pending_[at + 1];
        }
    written_ = frame + count;
    auto drop = std::clamp<int64_t>(written_ - pendingStart_, 0, static_cast<int64_t>(pending_.size() / 2));
    pending_.erase(pending_.begin(), pending_.begin() + drop * 2);
    pendingStart_ += drop;
    return out;
}
void exportMp4(Snapshot const &snap, IMFMediaType *video, IMFMediaType *audio,
               std::filesystem::path const &path, std::atomic<bool> const &cancel) {
    if (cancel)
        throw std::runtime_error("저장이 취소되었습니다.");
    std::filesystem::create_directories(path.parent_path());
    if (std::filesystem::space(path.parent_path()).available < snap.bytes + 8 * MiB)
        throw std::runtime_error("저장 공간이 부족합니다.");
    auto tmp = path;
    tmp += L".partial";
    try {
        ComPtr<IMFAttributes> attrs;
        check(MFCreateAttributes(&attrs, 2));
        check(attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4));
        check(attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE));
        ComPtr<IMFSinkWriter> writer;
        check(MFCreateSinkWriterFromURL(tmp.c_str(), nullptr, attrs.Get(), &writer));
        DWORD v{}, a{};
        check(writer->AddStream(video, &v));
        check(writer->SetInputMediaType(v, video, nullptr));
        if (audio) {
            check(writer->AddStream(audio, &a));
            check(writer->SetInputMediaType(a, audio, nullptr));
        }
        check(writer->BeginWriting());
        for (auto const &p : snap.packets) {
            if (cancel)
                throw std::runtime_error("저장이 취소되었습니다.");
            if (!p->video && !audio)
                continue;
            auto duration = std::min(p->duration, snap.end - p->pts);
            if (duration <= 0)
                continue;
            auto sample = sampleOf(p->bytes.data(), p->bytes.size(), p->pts - snap.start, duration);
            if (p->key)
                check(sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE));
            check(writer->WriteSample(p->video ? v : a, sample.Get()));
        }
        check(writer->Finalize());
        writer.Reset();
        if (cancel)
            throw std::runtime_error("저장이 취소되었습니다.");
        if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH))
            check(HRESULT_FROM_WIN32(GetLastError()));
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        throw;
    }
}
} // namespace replay
