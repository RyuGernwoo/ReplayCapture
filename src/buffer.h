#pragma once
#include "common.h"
namespace replay {
struct Packet {
    bool video{}, key{};
    int64_t pts{}, duration{};
    std::vector<uint8_t> bytes;
};
using PacketPtr = std::shared_ptr<const Packet>;
struct Snapshot {
    std::vector<PacketPtr> packets;
    int64_t start{}, end{};
    size_t bytes{};
    bool partial{};
};
class ReplayBuffer {
    mutable std::mutex mutex_;
    std::deque<PacketPtr> packets_;
    size_t bytes_ = 0, limit_ = 256 * MiB;
    int64_t retention_ = 60 * Second;

  public:
    void configure(int seconds, size_t limit);
    void append(PacketPtr packet);
    void clear();
    Snapshot snapshot(int64_t end, int seconds, bool requireFull) const;
    double available() const;
    size_t bytes() const;
};
} // namespace replay
