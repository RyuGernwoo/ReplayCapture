#pragma once
#include "common.h"
namespace replay {
struct Settings {
    int retention = 60, saveSeconds = 60, width = 1920, height = 1080, fps = 30, bitrate = 8;
    int bufferMiB = 256, totalMiB = 512, queueLimit = 3;
    bool systemAudio = true, allowVideoOnly = true, requireFull = false, notifications = true,
         autoStart = false;
    UINT hotkeyModifiers = MOD_CONTROL | MOD_SHIFT, hotkeyKey = VK_F9;
    std::wstring monitor, audioDevice, folder;
    Settings();
    std::wstring validate() const;
    static std::filesystem::path directory();
    static Settings load(std::wstring &warning);
    void save() const;
};
std::wstring hotkeyName(UINT modifiers, UINT key);
} // namespace replay
