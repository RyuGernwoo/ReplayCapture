#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#include <filesystem>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace replay {
using Microsoft::WRL::ComPtr;
constexpr int64_t Second = 10000000;
constexpr size_t MiB = 1024 * 1024;
inline void check(HRESULT hr) {
    winrt::check_hresult(hr);
}
inline int64_t clockNow() {
    LARGE_INTEGER q{}, f{};
    QueryPerformanceCounter(&q);
    QueryPerformanceFrequency(&f);
    return (q.QuadPart / f.QuadPart) * Second + (q.QuadPart % f.QuadPart) * Second / f.QuadPart;
}
inline std::wstring errorText() {
    try {
        throw;
    } catch (winrt::hresult_error const &e) {
        return std::wstring(e.message()) + L" (" + std::to_wstring(static_cast<uint32_t>(e.code().value)) +
               L")";
    } catch (std::exception const &e) {
        return winrt::to_hstring(e.what()).c_str();
    } catch (...) {
        return L"알 수 없는 오류";
    }
}
inline ComPtr<IMFSample> sampleOf(const void *data, size_t bytes, int64_t pts, int64_t duration) {
    ComPtr<IMFSample> sample;
    check(MFCreateSample(&sample));
    ComPtr<IMFMediaBuffer> buffer;
    check(MFCreateMemoryBuffer(static_cast<DWORD>(bytes), &buffer));
    BYTE *target{};
    check(buffer->Lock(&target, nullptr, nullptr));
    memcpy(target, data, bytes);
    check(buffer->Unlock());
    check(buffer->SetCurrentLength(static_cast<DWORD>(bytes)));
    check(sample->AddBuffer(buffer.Get()));
    check(sample->SetSampleTime(pts));
    check(sample->SetSampleDuration(duration));
    return sample;
}
struct Runtime {
    explicit Runtime(winrt::apartment_type kind = winrt::apartment_type::multi_threaded) {
        winrt::init_apartment(kind);
        check(MFStartup(MF_VERSION));
    }
    ~Runtime() {
        MFShutdown();
        winrt::uninit_apartment();
    }
};
} // namespace replay
