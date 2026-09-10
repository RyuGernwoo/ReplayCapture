#include "buffer.h"
namespace replay {
void ReplayBuffer::configure(int seconds, size_t limit) {
    std::lock_guard lock(mutex_);
    retention_ = seconds * Second;
    limit_ = limit;
}
void ReplayBuffer::clear() {
    std::lock_guard lock(mutex_);
    packets_.clear();
    bytes_ = 0;
}
void ReplayBuffer::append(PacketPtr p) {
    std::lock_guard lock(mutex_);
    bytes_ += p->bytes.size();
    packets_.push_back(std::move(p));
    // Audio and video arrive independently. Retain a complete GOP before the time boundary.
    int64_t latest = 0;
    for (auto const &x : packets_)
        latest = std::max(latest, x->pts + x->duration);
    int64_t cut = -1;
    for (auto const &x : packets_)
        if (x->video && x->key && x->pts <= latest - retention_)
            cut = std::max(cut, x->pts);
    auto eraseBefore = [&](int64_t t) {
        for (auto it = packets_.begin(); it != packets_.end();) {
            if ((*it)->pts < t) {
                bytes_ -= (*it)->bytes.size();
                it = packets_.erase(it);
            } else
                ++it;
        }
    };
    if (cut >= 0)
        eraseBefore(cut);
    while (bytes_ > limit_ && !packets_.empty()) {
        int64_t first = INT64_MAX, next = INT64_MAX;
        for (auto const &x : packets_)
            if (x->video && x->key) {
                if (x->pts < first) {
                    next = first;
                    first = x->pts;
                } else if (x->pts < next)
                    next = x->pts;
            }
        if (next == INT64_MAX) {
            packets_.clear();
            bytes_ = 0;
            break;
        }
        eraseBefore(next);
    }
}
Snapshot ReplayBuffer::snapshot(int64_t end, int seconds, bool requireFull) const {
    std::lock_guard lock(mutex_);
    Snapshot out;
    int64_t target = end - seconds * Second, key = -1, first = INT64_MAX;
    for (auto const &p : packets_)
        if (p->video && p->key && p->pts < end) {
            first = std::min(first, p->pts);
            if (p->pts <= target)
                key = std::max(key, p->pts);
        }
    if (key < 0)
        key = first;
    if (key == INT64_MAX)
        throw std::runtime_error("아직 저장할 기록이 없습니다.");
    out.partial = key > target + Second / 30;
    if (requireFull && out.partial)
        throw std::runtime_error("설정한 시간만큼 기록이 쌓이지 않았습니다.");
    out.start = key;
    for (auto const &p : packets_)
        if (p->pts >= key && p->pts < end) {
            out.packets.push_back(p);
            out.bytes += p->bytes.size();
            if (p->video)
                out.end = std::max(out.end, std::min(end, p->pts + p->duration));
        }
    if (out.end <= key)
        throw std::runtime_error("저장 가능한 영상이 없습니다.");
    std::stable_sort(out.packets.begin(), out.packets.end(),
                     [](auto const &a, auto const &b) { return a->pts < b->pts; });
    return out;
}
double ReplayBuffer::available() const {
    std::lock_guard lock(mutex_);
    int64_t first = INT64_MAX, last = 0;
    for (auto const &p : packets_)
        if (p->video) {
            if (p->key)
                first = std::min(first, p->pts);
            last = std::max(last, p->pts + p->duration);
        }
    return first == INT64_MAX ? 0 : std::max(0.0, double(last - first) / Second);
}
size_t ReplayBuffer::bytes() const {
    std::lock_guard lock(mutex_);
    return bytes_;
}
} // namespace replay
