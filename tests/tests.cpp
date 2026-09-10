#include "buffer.h"
#include "settings.h"
#include "ui/view_state.h"
#include <iostream>
using namespace replay;
void require(bool value, char const *what) {
    if (!value)
        throw std::runtime_error(what);
}
PacketPtr packet(int second, bool key, size_t bytes = 100) {
    auto p = std::make_shared<Packet>();
    p->video = true;
    p->key = key;
    p->pts = second * Second;
    p->duration = Second;
    p->bytes.resize(bytes);
    return p;
}
int main() {
    try {
        Runtime runtime;
        ReplayBuffer b;
        b.configure(5, 10000);
        for (int i = 0; i < 20; ++i)
            b.append(packet(i, i % 2 == 0));
        auto s = b.snapshot(20 * Second, 5, false);
        require(s.start == 14 * Second, "GOP boundary");
        require(s.end == 20 * Second, "end timestamp");
        require(!s.partial, "full history");
        b.clear();
        b.append(packet(0, true));
        b.append(packet(1, false));
        auto shortClip = b.snapshot(2 * Second, 5, false);
        require(shortClip.partial, "partial history flag");
        bool failed = false;
        try {
            b.snapshot(2 * Second, 5, true);
        } catch (...) {
            failed = true;
        }
        require(failed, "strict history");
        auto pinned = shortClip.packets.front();
        b.clear();
        require(pinned->bytes.size() == 100, "snapshot survives eviction");
        b.configure(60, 250);
        for (int i = 0; i < 10; ++i)
            b.append(packet(i, i % 2 == 0));
        require(b.bytes() <= 250, "byte bound");
        b.clear();
        b.configure(60, 100000);
        for (int i = 0; i < 20; ++i)
            b.append(packet(i, i % 2 == 0));
        auto earlier = b.snapshot(10 * Second, 3, false);
        require(earlier.end == 10 * Second, "exclude future samples");
        for (auto const &p : earlier.packets)
            require(p->pts < 10 * Second, "future packet leaked");
        b.clear();
        b.append(packet(0, false));
        failed = false;
        try {
            b.snapshot(Second, 1, false);
        } catch (...) {
            failed = true;
        }
        require(failed, "reject undecodable buffer without keyframe");
        Settings settings;
        Status recording;
        recording.state = L"녹화 중";
        recording.available = 12;
        require(ui::saveState(recording, 30, settings).enabled, "allow partial GUI save");
        settings.requireFull = true;
        require(!ui::saveState(recording, 30, settings).enabled, "strict GUI save waits");
        require(!ui::saveState(recording, 0, settings).enabled, "invalid GUI length disabled");
        require(!ui::saveState(recording, 61, settings).enabled, "GUI length limited to retention");
        recording.available = 60;
        require(ui::saveState(recording, 30, settings).enabled, "full GUI save ready");
        recording.state = L"일시정지";
        require(!ui::saveState(recording, 30, settings).enabled, "paused GUI save disabled");
        settings.requireFull = false;
        require(settings.validate().empty(), "default configuration");
        settings.saveSeconds = 601;
        require(!settings.validate().empty(), "invalid save length");
        settings.saveSeconds = 60;
        settings.retention = 600;
        require(!settings.validate().empty(), "memory admission");
        std::cout << "PASS: GOP selection, end cut, partial/strict, pinned ownership, byte bound, settings "
                     "validation\n";
        return 0;
    } catch (...) {
        std::wcerr << errorText() << L"\n";
        return 1;
    }
}
