#include "engine.h"
#include <iomanip>
namespace replay {
Engine::Engine() {
    exportThread_ = std::thread([this] { exportLoop(); });
}
Engine::~Engine() {
    stop();
    {
        std::lock_guard lock(mutex_);
        shutdown_ = true;
        for (auto &j : jobs_)
            j->cancel = true;
    }
    wake_.notify_all();
    if (exportThread_.joinable())
        exportThread_.join();
}
void Engine::start(Settings const &s) {
    auto invalid = s.validate();
    if (!invalid.empty())
        throw std::runtime_error(winrt::to_string(invalid));
    stop();
    {
        std::lock_guard lock(mutex_);
        settings_ = s;
        status_ = {};
        status_.state = L"시작 중";
        requests_.clear();
    }
    buffer_.configure(s.retention + 2, size_t(s.bufferMiB) * MiB);
    stop_ = false;
    clear_ = false;
    captureThread_ = std::thread([this, s] { captureLoop(s); });
}
void Engine::stop(bool pause) {
    {
        std::lock_guard lock(mutex_);
        status_.state = L"중지 중";
    }
    stop_ = true;
    if (captureThread_.joinable())
        captureThread_.join();
    buffer_.clear();
    std::lock_guard lock(mutex_);
    status_.state = pause ? L"일시정지" : L"중지";
    for (auto const &r : requests_)
        for (auto &j : jobs_)
            if (j->id == r.id) {
                j->state = L"취소";
                j->message = L"녹화가 중지되었습니다.";
            }
    requests_.clear();
}
void Engine::clear() {
    clear_ = true;
}
uint64_t Engine::save(int seconds, bool full) {
    std::lock_guard lock(mutex_);
    if (status_.state != L"녹화 중")
        throw std::runtime_error("녹화가 준비되지 않았습니다.");
    if (seconds < 1 || seconds > settings_.retention)
        throw std::runtime_error("저장 시간은 1초 이상, 보관 시간 이하여야 합니다.");
    int pending = 0;
    for (auto const &j : jobs_)
        if (j->state == L"대기" || j->state == L"저장 중" || j->state == L"접수")
            ++pending;
    if (pending >= settings_.queueLimit + 1)
        throw std::runtime_error("저장 작업이 가득 찼습니다. 완료 후 다시 시도하십시오.");
    auto job = std::make_shared<Job>();
    job->id = nextId_++;
    job->state = L"접수";
    job->requested = seconds;
    jobs_.push_back(job);
    while (jobs_.size() > 100 && jobs_.front()->state != L"대기" && jobs_.front()->state != L"저장 중" &&
           jobs_.front()->state != L"접수")
        jobs_.erase(jobs_.begin());
    requests_.push_back({job->id, seconds, clockNow(), full});
    return job->id;
}
void Engine::cancel(uint64_t id) {
    std::lock_guard lock(mutex_);
    for (auto &j : jobs_)
        if (j->id == id)
            j->cancel = true;
    wake_.notify_all();
}
Status Engine::status() const {
    Status s;
    {
        std::lock_guard lock(mutex_);
        s = status_;
        s.pinned = pinned_;
    }
    s.bytes = buffer_.bytes();
    s.available = buffer_.available();
    return s;
}
std::vector<JobView> Engine::jobs() const {
    std::lock_guard lock(mutex_);
    std::vector<JobView> out;
    for (auto const &j : jobs_)
        out.push_back({j->id, j->state, j->message, j->path.wstring(), j->requested, j->actual});
    return out;
}
void Engine::captureLoop(Settings s) {
    for (int attempt = 0; attempt < 4 && !stop_; ++attempt) {
        try {
            captureSession(s);
            return;
        } catch (...) {
            auto failure = errorText();
            buffer_.clear();
            {
                std::lock_guard lock(mutex_);
                status_.state = attempt < 3 ? L"복구 중" : L"오류";
                status_.error = failure;
                for (auto const &r : requests_)
                    for (auto &j : jobs_)
                        if (j->id == r.id) {
                            j->state = L"실패";
                            j->message = failure;
                        }
                requests_.clear();
            }
            if (attempt == 3)
                return;
            for (int i = 0; i < (5 << attempt) && !stop_; ++i)
                Sleep(100);
        }
    }
}
void Engine::captureSession(Settings s) {
    Runtime runtime;
    auto screens = monitors();
    if (screens.empty())
        throw std::runtime_error("연결된 모니터가 없습니다.");
    auto selected = screens.begin();
    if (!s.monitor.empty()) {
        selected =
            std::find_if(screens.begin(), screens.end(), [&](auto const &m) { return m.id == s.monitor; });
        if (selected == screens.end())
            throw std::runtime_error("선택한 모니터를 찾을 수 없습니다. 설정에서 다시 선택하십시오.");
    }
    ScreenCapture screen(selected->handle, s.width, s.height);
    int64_t origin = clockNow();
    Encoder video(true, s.width, s.height, s.fps, s.bitrate * 1000000,
                  [this](PacketPtr p) { buffer_.append(std::move(p)); });
    std::unique_ptr<Encoder> audio;
    std::unique_ptr<Loopback> loopback;
    if (s.systemAudio) {
        try {
            loopback = std::make_unique<Loopback>(s.audioDevice, origin);
            audio = std::make_unique<Encoder>(false, 0, 0, 0, 0,
                                              [this](PacketPtr p) { buffer_.append(std::move(p)); });
        } catch (...) {
            if (!s.allowVideoOnly)
                throw;
            std::lock_guard lock(mutex_);
            status_.error = L"소리 연결 실패: 영상만 기록합니다. " + errorText();
        }
    }
    {
        std::lock_guard lock(mutex_);
        status_.state = L"녹화 중";
        status_.encoder = video.name;
        status_.audio = audio ? L"시스템 소리 (무음 포함)" : L"영상만";
        if (audio || !s.systemAudio)
            status_.error.clear();
    }
    int64_t frame = 0, audioFrame = 0, lastDeviceCheck = origin;
    std::vector<uint8_t> nv12;
    for (;;) {
        bool pending;
        {
            std::lock_guard lock(mutex_);
            pending = !requests_.empty();
        }
        if (stop_ && !pending)
            break;
        auto now = clockNow();
        if (!pending && clear_.exchange(false))
            buffer_.clear();
        if (loopback)
            loopback->pump();
        video.pump();
        if (audio)
            audio->pump();
        if (loopback && s.audioDevice.empty() && now - lastDeviceCheck > Second) {
            lastDeviceCheck = now;
            if (loopback->defaultChanged())
                throw std::runtime_error(
                    "기본 소리 장치가 변경되었습니다. 다시 연결을 눌러 녹화를 재개하십시오.");
        }
        auto due = (now - origin) * s.fps / Second;
        if (frame <= due) {
            if (due - frame > 2) {
                std::lock_guard lock(mutex_);
                status_.drops += due - frame;
                frame = due;
            }
            if (screen.read(nv12)) {
                auto sample = sampleOf(nv12.data(), nv12.size(), frame * Second / s.fps, Second / s.fps);
                if (video.feed(sample.Get())) {
                    std::lock_guard lock(mutex_);
                    ++status_.frames;
                } else {
                    std::lock_guard lock(mutex_);
                    ++status_.drops;
                }
            }
            ++frame;
        }
        // Leave 120 ms for WASAPI delivery. Every block has a continuous sample-count timestamp.
        if (audio)
            while ((audioFrame + 1024) * Second / 48000 < now - origin - 1200000) {
                auto pcm = loopback->block(audioFrame, 1024);
                auto sample =
                    sampleOf(pcm.data(), pcm.size() * 2, audioFrame * Second / 48000, 1024 * Second / 48000);
                if (!audio->feed(sample.Get()))
                    break;
                audioFrame += 1024;
            }
        std::vector<Request> ready;
        {
            std::lock_guard lock(mutex_);
            while (!requests_.empty() && now - requests_.front().time > 4000000) {
                ready.push_back(requests_.front());
                requests_.pop_front();
            }
        }
        for (auto const &r : ready) {
            std::shared_ptr<Job> j;
            {
                std::lock_guard lock(mutex_);
                for (auto const &x : jobs_)
                    if (x->id == r.id)
                        j = x;
            }
            if (!j)
                continue;
            try {
                if (j->cancel)
                    throw std::runtime_error("저장이 취소되었습니다.");
                auto snap = buffer_.snapshot(r.time - origin, r.seconds, r.full);
                auto v = video.type();
                auto a = audio ? audio->type() : ComPtr<IMFMediaType>{};
                SYSTEMTIME st{};
                GetLocalTime(&st);
                wchar_t stamp[80];
                swprintf_s(stamp, L"Replay_%04u%02u%02u_%02u%02u%02u_%03u_%llu.mp4", st.wYear, st.wMonth,
                           st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, r.id);
                std::lock_guard lock(mutex_);
                if (pinned_ + snap.bytes + size_t(s.bufferMiB + 128) * MiB > size_t(s.totalMiB) * MiB)
                    throw std::runtime_error(
                        "저장 참조 메모리 예산이 부족합니다. 기존 저장 완료 후 다시 시도하십시오.");
                j->path = std::filesystem::path(s.folder) / stamp;
                j->actual = double(snap.end - snap.start) / Second;
                j->startOffset = double(r.time - origin - r.seconds * Second - snap.start) / Second;
                j->endLag = double(r.time - origin - snap.end) / Second;
                j->partial = snap.partial;
                j->snapshot = std::move(snap);
                j->video = v;
                j->audio = a;
                pinned_ += j->snapshot.bytes;
                j->state = L"대기";
                wake_.notify_one();
            } catch (...) {
                std::lock_guard lock(mutex_);
                j->state = j->cancel ? L"취소" : L"실패";
                j->message = errorText();
            }
        }
        Sleep(2);
    }
}
void Engine::exportLoop() {
    Runtime runtime;
    for (;;) {
        std::shared_ptr<Job> j;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] {
                return shutdown_ || std::any_of(jobs_.begin(), jobs_.end(),
                                                [](auto const &x) { return x->state == L"대기"; });
            });
            if (shutdown_)
                break;
            for (auto const &x : jobs_)
                if (x->state == L"대기") {
                    j = x;
                    break;
                }
            if (j)
                j->state = L"저장 중";
        }
        if (!j)
            continue;
        std::wstring failure;
        try {
            exportMp4(j->snapshot, j->video.Get(), j->audio.Get(), j->path, j->cancel);
        } catch (...) {
            failure = errorText();
        }
        {
            std::lock_guard lock(mutex_);
            j->state = failure.empty() ? L"완료" : j->cancel ? L"취소" : L"실패";
            j->message =
                failure.empty() ? (j->partial ? L"보관된 길이만 저장했습니다." : L"저장 완료") : failure;
            if (failure.empty())
                j->message += L" · 시작 여유 " + std::to_wstring(j->startOffset) + L"초 · 끝 누락 " +
                              std::to_wstring(j->endLag) + L"초";
            pinned_ -= j->snapshot.bytes;
            j->snapshot = {};
            j->video.Reset();
            j->audio.Reset();
        }
    }
}
} // namespace replay
