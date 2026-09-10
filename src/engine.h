#pragma once
#include "media.h"
namespace replay {
struct Job {
    uint64_t id{};
    std::wstring state = L"대기", message;
    std::filesystem::path path;
    double requested{}, actual{}, startOffset{}, endLag{};
    bool partial{};
    std::atomic<bool> cancel{false};
    Snapshot snapshot;
    ComPtr<IMFMediaType> video, audio;
};
struct Status {
    std::wstring state = L"중지", audio = L"꺼짐", encoder, error;
    double available{};
    size_t bytes{}, pinned{};
    uint64_t frames{}, drops{};
};
struct JobView {
    uint64_t id;
    std::wstring state, message, path;
    double requested, actual;
};
class Engine {
    mutable std::mutex mutex_;
    Status status_;
    Settings settings_;
    ReplayBuffer buffer_;
    std::thread captureThread_, exportThread_;
    std::atomic<bool> stop_{false}, shutdown_{false}, clear_{false};
    std::condition_variable wake_;
    struct Request {
        uint64_t id;
        int seconds;
        int64_t time;
        bool full;
    };
    std::deque<Request> requests_;
    std::vector<std::shared_ptr<Job>> jobs_;
    size_t pinned_{};
    uint64_t nextId_ = 1;
    void captureLoop(Settings settings);
    void captureSession(Settings settings);
    void exportLoop();

  public:
    Engine();
    ~Engine();
    void start(Settings const &settings);
    void stop(bool pause = false);
    void clear();
    uint64_t save(int seconds, bool full);
    void cancel(uint64_t id);
    Status status() const;
    std::vector<JobView> jobs() const;
};
} // namespace replay
