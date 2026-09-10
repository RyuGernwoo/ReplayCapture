#include "engine.h"
#include <iostream>
#include <mmsystem.h>
using namespace replay;
int wmain(int argc, wchar_t **argv) {
    try {
        Runtime runtime;
        Settings s;
        s.folder = std::filesystem::absolute(argc > 1 ? argv[1] : L"artifacts/integration").wstring();
        int duration = argc > 2 ? _wtoi(argv[2]) : 12;
        s.retention = std::max(10, duration);
        s.saveSeconds = std::min(60, duration - 3);
        s.totalMiB = 768;
        std::filesystem::create_directories(s.folder);
        std::vector<uint8_t> wav(44 + 48000 * 4);
        auto put = [&](int offset, uint32_t value) { memcpy(wav.data() + offset, &value, 4); };
        memcpy(wav.data(), "RIFF", 4);
        put(4, static_cast<uint32_t>(wav.size() - 8));
        memcpy(wav.data() + 8, "WAVEfmt ", 8);
        put(16, 16);
        wav[20] = 1;
        wav[22] = 2;
        put(24, 48000);
        put(28, 192000);
        wav[32] = 4;
        wav[34] = 16;
        memcpy(wav.data() + 36, "data", 4);
        put(40, 48000 * 4);
        for (int i = 0; i < 48000; ++i) {
            int16_t v =
                i < 4800 ? static_cast<int16_t>(5000 * sin(i * 2 * 3.141592653589793 * 440 / 48000)) : 0;
            memcpy(wav.data() + 44 + i * 4, &v, 2);
            memcpy(wav.data() + 46 + i * 4, &v, 2);
        }
        PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_LOOP);
        Engine engine;
        engine.start(s);
        for (int i = 0; i < duration * 10; ++i) {
            Sleep(100);
            auto st = engine.status();
            if (st.state == L"오류")
                throw std::runtime_error(winrt::to_string(st.error));
        }
        auto before = engine.status();
        std::cout << "Encoder: " << winrt::to_string(before.encoder) << " frames=" << before.frames
                  << " drops=" << before.drops << " available=" << before.available << "\n";
        for (int j = 0; j < 3; ++j)
            engine.save(s.saveSeconds, false);
        bool done = false;
        for (int i = 0; i < 200; ++i) {
            Sleep(100);
            auto jobs = engine.jobs();
            done = jobs.size() == 3;
            for (auto const &j : jobs)
                if (j.state != L"완료")
                    done = false;
            if (done)
                break;
        }
        auto after = engine.status();
        PlaySoundW(nullptr, nullptr, 0);
        if (!done) {
            for (auto const &j : engine.jobs())
                std::cout << winrt::to_string(j.message) << "\n";
            throw std::runtime_error("Export did not complete");
        }
        if (after.frames <= before.frames)
            throw std::runtime_error("Capture stopped during export");
        std::ofstream report(std::filesystem::path(s.folder) / L"result.json");
        report << "{\"frames_before\":" << before.frames << ",\"frames_after\":" << after.frames
               << ",\"drops\":" << after.drops << ",\"available\":" << after.available
               << ",\"buffer_bytes\":" << after.bytes << ",\"saved_jobs\":3}";
        for (auto const &j : engine.jobs())
            std::cout << "Saved: " << winrt::to_string(j.path) << " duration=" << j.actual << "\n";
        return 0;
    } catch (...) {
        PlaySoundW(nullptr, nullptr, 0);
        std::cerr << winrt::to_string(errorText()) << "\n";
        return 1;
    }
}
