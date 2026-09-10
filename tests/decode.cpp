#include "common.h"
#include <iostream>
using namespace replay;
int wmain(int argc, wchar_t **argv) {
    try {
        Runtime runtime;
        if (argc != 2)
            throw std::runtime_error("Pass one MP4 path");
        for (bool video : {true, false}) {
            ComPtr<IMFSourceReader> reader;
            check(MFCreateSourceReaderFromURL(argv[1], nullptr, &reader));
            DWORD stream = video ? MF_SOURCE_READER_FIRST_VIDEO_STREAM : MF_SOURCE_READER_FIRST_AUDIO_STREAM;
            check(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE));
            check(reader->SetStreamSelection(stream, TRUE));
            ComPtr<IMFMediaType> type;
            check(MFCreateMediaType(&type));
            check(type->SetGUID(MF_MT_MAJOR_TYPE, video ? MFMediaType_Video : MFMediaType_Audio));
            check(type->SetGUID(MF_MT_SUBTYPE, video ? MFVideoFormat_NV12 : MFAudioFormat_PCM));
            check(reader->SetCurrentMediaType(stream, nullptr, type.Get()));
            uint64_t samples = 0, bytes = 0;
            int64_t previous = -1;
            for (;;) {
                DWORD flags{};
                LONGLONG timestamp{};
                ComPtr<IMFSample> sample;
                check(reader->ReadSample(stream, 0, nullptr, &flags, &timestamp, &sample));
                if (flags & MF_SOURCE_READERF_ERROR)
                    throw std::runtime_error("Windows decoder error");
                if (sample) {
                    if (timestamp < previous)
                        throw std::runtime_error("Non-monotonic decoded timestamp");
                    previous = timestamp;
                    DWORD size{};
                    check(sample->GetTotalLength(&size));
                    bytes += size;
                    ++samples;
                }
                if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
                    break;
            }
            if (!samples || !bytes)
                throw std::runtime_error("Empty decoded stream");
            std::cout << (video ? "video" : "audio") << " samples=" << samples << " bytes=" << bytes
                      << " final_pts=" << double(previous) / Second << "\n";
        }
        return 0;
    } catch (...) {
        std::cerr << winrt::to_string(errorText()) << "\n";
        return 1;
    }
}
