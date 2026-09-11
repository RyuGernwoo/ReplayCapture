#include "settings.h"
#include <winrt/Windows.Data.Json.h>
#include <shlobj.h>
namespace replay {
using namespace winrt::Windows::Data::Json;
Settings::Settings() {
    PWSTR value{};
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &value))) {
        folder = std::wstring(value) + L"\\ReplayCapture";
        CoTaskMemFree(value);
    }
}
std::filesystem::path Settings::directory() {
    wchar_t overridePath[32768]{};
    auto count = GetEnvironmentVariableW(L"REPLAYCAPTURE_DATA_DIR", overridePath, 32768);
    if (count > 0 && count < 32768) {
        std::filesystem::path p(overridePath);
        if (!p.is_absolute())
            throw std::runtime_error("Test data directory must be absolute");
        return p;
    }
    PWSTR value{};
    check(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &value));
    std::filesystem::path p(value);
    CoTaskMemFree(value);
    return p / L"ReplayCapture";
}
std::wstring Settings::validate() const {
    if (retention < 5 || retention > 600 || saveSeconds < 1 || saveSeconds > retention)
        return L"보관 시간은 5~600초, 저장 시간은 1초 이상·보관 시간 이하로 설정하십시오.";
    if (width < 320 || width > 3840 || height < 240 || height > 2160 || width % 2 || height % 2)
        return L"해상도는 짝수이며 320×240~3840×2160 범위여야 합니다.";
    if (fps < 10 || fps > 60 || bitrate < 1 || bitrate > 50)
        return L"FPS는 10~60, 비트레이트는 1~50Mbps 범위여야 합니다.";
    if (bufferMiB < 32 || bufferMiB > 2048 || totalMiB < bufferMiB + 128 || totalMiB > 4096)
        return L"압축 메모리는 32~2048MiB, 총 예산은 압축 메모리+128MiB 이상·4096MiB 이하로 설정하십시오.";
    const double estimate = (bitrate * 1000000.0 + 192000) * (retention + 3) / 8 * 1.15;
    if (estimate > bufferMiB * double(MiB))
        return L"보관 시간과 화질에 비해 압축 메모리 예산이 부족합니다. 시간을 줄이거나 예산을 늘리십시오.";
    if (queueLimit < 1 || queueLimit > 10)
        return L"저장 대기 수는 1~10이어야 합니다.";
    if (folder.empty() || !std::filesystem::path(folder).is_absolute())
        return L"저장 폴더를 선택하십시오 (절대 경로).";
    if (hotkeyKey < 8 || hotkeyKey > 254 || hotkeyKey == VK_CONTROL || hotkeyKey == VK_SHIFT ||
        hotkeyKey == VK_MENU || !(hotkeyModifiers & (MOD_CONTROL | MOD_ALT | MOD_SHIFT)) ||
        (hotkeyModifiers & ~(MOD_CONTROL | MOD_ALT | MOD_SHIFT)) || hotkeyKey == VK_F12)
        return L"Ctrl/Alt/Shift와 일반 키를 조합하십시오. F12는 예약된 키입니다.";
    return {};
}
Settings Settings::load(std::wstring &warning) {
    Settings s;
    try {
        auto path = directory() / L"settings.json";
        if (!std::filesystem::exists(path))
            return s;
        if (std::filesystem::file_size(path) > 65536)
            throw std::runtime_error("Settings file too large");
        std::ifstream file(path, std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(file)), {});
        auto j = JsonObject::Parse(winrt::to_hstring(raw));
        if (j.GetNamedNumber(L"schemaVersion", 0) != 1)
            throw std::runtime_error("Unsupported settings version");
        auto number = [&](wchar_t const *k, int d) {
            auto v = j.GetNamedNumber(k, d);
            if (!std::isfinite(v) || v < 0 || v > 100000 || v != std::floor(v))
                throw std::runtime_error("Invalid numeric setting");
            return static_cast<int>(v);
        };
        s.retention = number(L"retentionSeconds", 60);
        s.saveSeconds = number(L"saveSeconds", 60);
        s.width = number(L"width", 1920);
        s.height = number(L"height", 1080);
        s.fps = number(L"fps", 30);
        s.bitrate = number(L"bitrateMbps", 8);
        s.bufferMiB = number(L"bufferMiB", 256);
        s.totalMiB = number(L"totalMiB", 512);
        s.queueLimit = number(L"queueLimit", 3);
        s.hotkeyModifiers = number(L"hotkeyModifiers", MOD_CONTROL | MOD_SHIFT);
        s.hotkeyKey = number(L"hotkeyKey", VK_F9);
        s.systemAudio = j.GetNamedBoolean(L"systemAudio", true);
        s.allowVideoOnly = true; // Audio fallback is always enabled, including for older settings.
        s.requireFull = j.GetNamedBoolean(L"requireFull", false);
        s.notifications = j.GetNamedBoolean(L"notifications", true);
        s.autoStart = j.GetNamedBoolean(L"autoStart", false);
        s.folder = j.GetNamedString(L"folder", s.folder);
        s.monitor = j.GetNamedString(L"monitor", L"");
        s.audioDevice = j.GetNamedString(L"audioDevice", L"");
        auto invalid = s.validate();
        if (!invalid.empty())
            throw std::runtime_error(winrt::to_string(invalid));
    } catch (...) {
        warning = L"설정을 읽지 못해 기본값을 사용합니다. " + errorText();
        return Settings{};
    }
    return s;
}
void Settings::save() const {
    auto invalid = validate();
    if (!invalid.empty())
        throw std::runtime_error(winrt::to_string(invalid));
    JsonObject j;
    auto n = [&](auto k, double v) { j.Insert(k, JsonValue::CreateNumberValue(v)); };
    auto b = [&](auto k, bool v) { j.Insert(k, JsonValue::CreateBooleanValue(v)); };
    n(L"schemaVersion", 1);
    n(L"retentionSeconds", retention);
    n(L"saveSeconds", saveSeconds);
    n(L"width", width);
    n(L"height", height);
    n(L"fps", fps);
    n(L"bitrateMbps", bitrate);
    n(L"bufferMiB", bufferMiB);
    n(L"totalMiB", totalMiB);
    n(L"queueLimit", queueLimit);
    n(L"hotkeyModifiers", hotkeyModifiers);
    n(L"hotkeyKey", hotkeyKey);
    b(L"systemAudio", systemAudio);
    b(L"allowVideoOnly", allowVideoOnly);
    b(L"requireFull", requireFull);
    b(L"notifications", notifications);
    b(L"autoStart", autoStart);
    j.Insert(L"folder", JsonValue::CreateStringValue(folder));
    j.Insert(L"monitor", JsonValue::CreateStringValue(monitor));
    j.Insert(L"audioDevice", JsonValue::CreateStringValue(audioDevice));
    auto dir = directory();
    std::filesystem::create_directories(dir);
    auto tmp = dir / L"settings.json.tmp", path = dir / L"settings.json";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << winrt::to_string(j.Stringify());
        f.flush();
        if (!f)
            throw std::runtime_error("Cannot write settings");
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        check(HRESULT_FROM_WIN32(GetLastError()));
}
std::wstring hotkeyName(UINT modifiers, UINT key) {
    std::wstring s;
    if (modifiers & MOD_CONTROL)
        s += L"Ctrl+";
    if (modifiers & MOD_ALT)
        s += L"Alt+";
    if (modifiers & MOD_SHIFT)
        s += L"Shift+";
    if (modifiers & MOD_WIN)
        s += L"Win+";
    wchar_t name[64]{};
    auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 64);
    s += name[0] ? name : L"?";
    return s;
}
} // namespace replay
