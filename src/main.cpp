#include "engine.h"
#include "resource.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wtsapi32.h>
#include <map>
#include <set>
#include <iomanip>
#pragma comment(lib, "wtsapi32.lib")
using namespace replay;
namespace {
constexpr UINT TrayMessage = WM_APP + 1;
enum Id {
    Tab = 100,
    Start,
    Pause,
    Stop,
    Save,
    Clear,
    Exit,
    ThisSeconds,
    ResetSeconds,
    StatusText,
    Progress,
    Summary,
    MonitorChoice = 200,
    AudioChoice,
    RefreshDevices,
    Retention,
    DefaultSeconds,
    Folder,
    Browse,
    OpenFolder,
    Width,
    Height,
    Fps,
    Bitrate,
    BufferLimit,
    TotalLimit,
    QueueLimit,
    SystemAudio,
    VideoOnly,
    FullOnly,
    Notify,
    AutoStart,
    Apply,
    Defaults,
    KeyField = 300,
    ApplyKey,
    ResetKey,
    TestKey,
    KeyStatus,
    Jobs = 400,
    OpenVideo,
    RevealVideo,
    CancelJob,
    JobDetail,
    Diagnostics = 500,
    CopyDiagnostics,
    ExportDiagnostics,
    Reconnect
};
struct Control {
    HWND handle;
    int page, x, y, w, h;
};
struct App {
    HWND window{}, tab{};
    HFONT font{}, titleFont{};
    Engine engine;
    Settings settings;
    std::vector<Monitor> screens;
    std::vector<AudioDevice> devices;
    std::vector<Control> controls;
    std::map<int, HWND> fields;
    std::set<uint64_t> announced;
    UINT dpi = 96;
    int page = 0, hotkeyId = 1;
    bool keyRegistered = false, keyTest = false, wasLocked = false, closing = false;
    int64_t exitDeadline{};
    std::wstring warning;
    HBRUSH background = CreateSolidBrush(RGB(246, 248, 252));
    ~App() {
        DeleteObject(font);
        DeleteObject(titleFont);
        DeleteObject(background);
    }
    int px(int n) const {
        return MulDiv(n, dpi, 96);
    }
    void fitDpi() {
        MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
        dpi = std::min({GetDpiForWindow(window), UINT((info.rcWork.right - info.rcWork.left) * 96 / 980),
                        UINT((info.rcWork.bottom - info.rcWork.top) * 96 / 760)});
    }
    HWND control(wchar_t const *klass, std::wstring const &text, int id, int p, int x, int y, int w, int h,
                 DWORD style = 0) {
        HWND c = CreateWindowExW((wcscmp(klass, L"EDIT") == 0) ? WS_EX_CLIENTEDGE : 0, klass, text.c_str(),
                                 WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                                 reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
        if (!c)
            check(HRESULT_FROM_WIN32(GetLastError()));
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        controls.push_back({c, p, x, y, w, h});
        if (id)
            fields[id] = c;
        return c;
    }
    void label(std::wstring const &t, int p, int x, int y, int w = 180, int h = 24) {
        control(L"STATIC", t, 0, p, x, y, w, h);
    }
    void button(std::wstring const &t, int id, int p, int x, int y, int w = 136, int h = 36) {
        control(L"BUTTON", t, id, p, x, y, w, h, WS_TABSTOP | BS_PUSHBUTTON);
    }
    void edit(int id, int p, int x, int y, int w = 100, bool numeric = true) {
        control(L"EDIT", L"", id, p, x, y, w, 28, WS_TABSTOP | ES_AUTOHSCROLL | (numeric ? ES_NUMBER : 0));
    }
    void checkbox(std::wstring const &t, int id, int x, int y, int w = 410) {
        control(L"BUTTON", t, id, 1, x, y, w, 28, WS_TABSTOP | BS_AUTOCHECKBOX);
    }
    HWND get(int id) {
        return fields.at(id);
    }
    std::wstring text(int id) {
        auto h = get(id);
        int n = GetWindowTextLengthW(h);
        std::wstring s(size_t(n) + 1, L'\0');
        GetWindowTextW(h, s.data(), n + 1);
        s.resize(n);
        return s;
    }
    void text(int id, std::wstring const &s) {
        SetWindowTextW(get(id), s.c_str());
    }
    void number(int id, int n) {
        text(id, std::to_wstring(n));
    }
    int number(int id) {
        auto s = text(id);
        size_t end{};
        int n = std::stoi(s, &end);
        if (end != s.size())
            throw std::runtime_error("숫자 입력을 확인하십시오.");
        return n;
    }
    bool checked(int id) {
        return SendMessageW(get(id), BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    void checked(int id, bool v) {
        SendMessageW(get(id), BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    void alert(std::wstring const &s, bool error = false) {
        MessageBoxW(window, s.c_str(), L"ReplayCapture",
                    MB_OK | (error ? MB_ICONWARNING : MB_ICONINFORMATION));
    }
    void balloon(std::wstring const &s) {
        if (!settings.notifications)
            return;
        NOTIFYICONDATAW n{sizeof(n)};
        n.hWnd = window;
        n.uID = 1;
        n.uFlags = NIF_INFO;
        n.dwInfoFlags = NIIF_INFO;
        wcscpy_s(n.szInfoTitle, L"ReplayCapture");
        wcsncpy_s(n.szInfo, s.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &n);
    }
    void show() {
        ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
    }
    void selectPage(int p) {
        page = p;
        TabCtrl_SetCurSel(tab, p);
        for (auto const &c : controls)
            ShowWindow(c.handle, c.page < 0 || c.page == p ? SW_SHOW : SW_HIDE);
        keyTest = false;
    }
    void layout() {
        for (auto const &c : controls)
            MoveWindow(c.handle, px(c.x), px(c.y), px(c.w), px(c.h), TRUE);
    }
    void fonts() {
        if (font)
            DeleteObject(font);
        if (titleFont)
            DeleteObject(titleFont);
        font = CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"맑은 고딕");
        titleFont = CreateFontW(-px(26), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                CLEARTYPE_QUALITY, 0, L"맑은 고딕");
        for (auto const &c : controls)
            SendMessageW(c.handle, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    void create() {
        fitDpi();
        fonts();
        auto title = control(L"STATIC", L"ReplayCapture", 0, -1, 28, 18, 380, 40);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
        label(L"지나간 순간을, 화면과 소리 그대로", -1, 30, 62, 650);
        tab = control(WC_TABCONTROLW, L"", Tab, -1, 24, 104, 912, 34, WS_TABSTOP);
        wchar_t const *names[] = {L"녹화", L"설정", L"단축키", L"저장 작업", L"진단"};
        for (int i = 0; i < 5; ++i) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<LPWSTR>(names[i]);
            TabCtrl_InsertItem(tab, i, &item);
        }
        control(L"STATIC", L"중지", StatusText, 0, 36, 166, 880, 32);
        control(PROGRESS_CLASSW, L"", Progress, 0, 36, 213, 880, 16);
        SendMessageW(get(Progress), PBM_SETRANGE32, 0, 1000);
        control(L"STATIC", L"", Summary, 0, 36, 246, 880, 104);
        button(L"녹화 시작", Start, 0, 36, 372);
        button(L"일시정지", Pause, 0, 188, 372);
        button(L"녹화 중지", Stop, 0, 340, 372);
        button(L"버퍼 비우기", Clear, 0, 492, 372);
        label(L"이번 저장 길이 (초)", 0, 36, 446, 190);
        edit(ThisSeconds, 0, 236, 442, 100);
        button(L"기본 시간으로", ResetSeconds, 0, 352, 440, 146);
        button(L"최근 60초 저장", Save, 0, 36, 500, 462, 56);
        label(L"저장 중에도 녹화는 계속됩니다. 단축키는 설정의 기본 시간을 사용합니다.", 0, 36, 576, 880);
        button(L"앱 종료", Exit, -1, 800, 638, 128);
        label(L"모니터", 1, 36, 163, 125);
        control(WC_COMBOBOXW, L"", MonitorChoice, 1, 170, 159, 560, 250,
                WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
        button(L"장치 새로고침", RefreshDevices, 1, 750, 158, 160, 32);
        label(L"소리 출력 장치", 1, 36, 203, 125);
        control(WC_COMBOBOXW, L"", AudioChoice, 1, 170, 199, 740, 250,
                WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
        checkbox(L"시스템 소리 포함", SystemAudio, 36, 238, 210);
        checkbox(L"소리 연결 실패 시 영상만 허용", VideoOnly, 300, 238, 400);
        label(L"보관 시간 (초)", 1, 36, 282, 130);
        edit(Retention, 1, 170, 278, 100);
        label(L"기본 저장 (초)", 1, 310, 282, 135);
        edit(DefaultSeconds, 1, 452, 278, 100);
        checkbox(L"전체 길이가 쌓인 경우만 저장", FullOnly, 586, 277, 330);
        label(L"저장 폴더", 1, 36, 322, 125);
        edit(Folder, 1, 170, 318, 540, false);
        button(L"찾아보기", Browse, 1, 726, 316, 90, 32);
        button(L"열기", OpenFolder, 1, 826, 316, 84, 32);
        label(L"출력 폭 / 높이", 1, 36, 366, 135);
        edit(Width, 1, 170, 362, 84);
        edit(Height, 1, 266, 362, 84);
        label(L"FPS", 1, 380, 366, 55);
        edit(Fps, 1, 436, 362, 70);
        label(L"영상 Mbps", 1, 552, 366, 120);
        edit(Bitrate, 1, 674, 362, 90);
        label(L"압축 메모리 MiB", 1, 36, 410, 145);
        edit(BufferLimit, 1, 190, 406, 84);
        label(L"총 예산 MiB", 1, 316, 410, 130);
        edit(TotalLimit, 1, 452, 406, 84);
        label(L"저장 대기 수", 1, 578, 410, 135);
        edit(QueueLimit, 1, 720, 406, 84);
        checkbox(L"저장 완료·오류 알림", Notify, 36, 454, 330);
        checkbox(L"Windows 로그인 시 앱 자동 실행", AutoStart, 400, 454, 470);
        label(L"AAC 48kHz / 192kbps · 키프레임 목표 1초 · SDR 전용\n설정 적용 시 녹화 중인 버퍼는 새로 "
              L"시작합니다. 기존 저장 작업은 유지됩니다.",
              1, 36, 500, 880, 55);
        button(L"설정 적용", Apply, 1, 36, 576, 180);
        button(L"기본값 불러오기", Defaults, 1, 236, 576, 180);
        label(L"최근 기록 저장 — 전역 단축키", 2, 36, 168, 880, 32);
        label(L"아래 입력칸을 선택한 뒤 원하는 Ctrl / Alt / Shift + 키 조합을 누르십시오.", 2, 36, 216, 880);
        control(HOTKEY_CLASSW, L"", KeyField, 2, 36, 262, 450, 40, WS_TABSTOP);
        button(L"단축키 적용", ApplyKey, 2, 36, 328, 170);
        button(L"기본 키 불러오기", ResetKey, 2, 224, 328, 190);
        button(L"단축키 테스트", TestKey, 2, 432, 328, 180);
        control(L"STATIC", L"", KeyStatus, 2, 36, 396, 880, 110);
        label(L"다른 앱을 사용하는 중이나 트레이에 숨긴 상태에서도 저장합니다.\n키를 누르고 있어도 한 번만 "
              L"저장하며, 이 화면에서는 실제 저장을 억제합니다.\n기본 키: Ctrl+Shift+F9. 키 충돌 시 기존 "
              L"설정을 유지합니다.",
              2, 36, 518, 880, 85);
        control(WC_LISTVIEWW, L"", Jobs, 3, 36, 164, 880, 325,
                WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
        ListView_SetExtendedListViewStyle(get(Jobs), LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        wchar_t const *cols[] = {L"작업", L"상태", L"요청 / 실제 (초)", L"파일 / 메시지"};
        int widths[] = {65, 95, 160, 530};
        for (int i = 0; i < 4; ++i) {
            LVCOLUMNW c{};
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.pszText = const_cast<LPWSTR>(cols[i]);
            c.cx = px(widths[i]);
            ListView_InsertColumn(get(Jobs), i, &c);
        }
        button(L"영상 열기", OpenVideo, 3, 36, 508);
        button(L"폴더에서 보기", RevealVideo, 3, 188, 508, 160);
        button(L"작업 취소", CancelJob, 3, 364, 508);
        control(L"STATIC", L"작업을 선택하면 상세 내용이 표시됩니다.", JobDetail, 3, 36, 563, 880, 60);
        control(L"EDIT", L"", Diagnostics, 4, 36, 164, 880, 380,
                WS_TABSTOP | ES_MULTILINE | ES_READONLY | WS_VSCROLL);
        button(L"진단 복사", CopyDiagnostics, 4, 36, 566);
        button(L"진단 파일 저장", ExportDiagnostics, 4, 188, 566, 170);
        button(L"장치 다시 연결", Reconnect, 4, 374, 566, 170);
        loadFields(settings);
        layout();
        selectPage(0);
        number(ThisSeconds, settings.saveSeconds);
        keyRegistered = RegisterHotKey(window, hotkeyId, settings.hotkeyModifiers | MOD_NOREPEAT,
                                       settings.hotkeyKey) != 0;
        keyLabel(keyRegistered ? L"등록됨" : L"등록 실패: 다른 키를 적용하십시오.");
        NOTIFYICONDATAW n{sizeof(n)};
        n.hWnd = window;
        n.uID = 1;
        n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        n.uCallbackMessage = TrayMessage;
        n.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_REPLAYCAPTURE));
        wcscpy_s(n.szTip, L"ReplayCapture — 녹화 중지");
        Shell_NotifyIconW(NIM_ADD, &n);
        WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION);
        SetTimer(window, 1, 300, nullptr);
    }
    void keyLabel(std::wstring const &info) {
        text(KeyStatus, L"현재 적용 키: " + hotkeyName(settings.hotkeyModifiers, settings.hotkeyKey) + L"\n" +
                            info + L"\n단축키 저장 길이: " + std::to_wstring(settings.saveSeconds) + L"초");
    }
    void refreshDevices(Settings const &s) {
        screens = monitors();
        devices = audioDevices();
        auto m = get(MonitorChoice), a = get(AudioChoice);
        SendMessageW(m, CB_RESETCONTENT, 0, 0);
        int selected = 0;
        for (size_t i = 0; i < screens.size(); ++i) {
            SendMessageW(m, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(screens[i].label.c_str()));
            if (screens[i].id == s.monitor)
                selected = int(i);
        }
        SendMessageW(m, CB_SETCURSEL, selected, 0);
        SendMessageW(a, CB_RESETCONTENT, 0, 0);
        SendMessageW(a, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"기본 출력 장치를 따라감"));
        selected = 0;
        for (size_t i = 0; i < devices.size(); ++i) {
            SendMessageW(a, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(devices[i].label.c_str()));
            if (devices[i].id == s.audioDevice)
                selected = int(i) + 1;
        }
        SendMessageW(a, CB_SETCURSEL, selected, 0);
    }
    void loadFields(Settings const &s) {
        refreshDevices(s);
        number(Retention, s.retention);
        number(DefaultSeconds, s.saveSeconds);
        text(Folder, s.folder);
        number(Width, s.width);
        number(Height, s.height);
        number(Fps, s.fps);
        number(Bitrate, s.bitrate);
        number(BufferLimit, s.bufferMiB);
        number(TotalLimit, s.totalMiB);
        number(QueueLimit, s.queueLimit);
        checked(SystemAudio, s.systemAudio);
        checked(VideoOnly, s.allowVideoOnly);
        checked(FullOnly, s.requireFull);
        checked(Notify, s.notifications);
        checked(AutoStart, s.autoStart);
        UINT flags = 0;
        if (s.hotkeyModifiers & MOD_CONTROL)
            flags |= HOTKEYF_CONTROL;
        if (s.hotkeyModifiers & MOD_ALT)
            flags |= HOTKEYF_ALT;
        if (s.hotkeyModifiers & MOD_SHIFT)
            flags |= HOTKEYF_SHIFT;
        SendMessageW(get(KeyField), HKM_SETHOTKEY, MAKEWORD(s.hotkeyKey, flags), 0);
    }
    Settings readFields() {
        auto s = settings;
        s.retention = number(Retention);
        s.saveSeconds = number(DefaultSeconds);
        s.folder = text(Folder);
        s.width = number(Width);
        s.height = number(Height);
        s.fps = number(Fps);
        s.bitrate = number(Bitrate);
        s.bufferMiB = number(BufferLimit);
        s.totalMiB = number(TotalLimit);
        s.queueLimit = number(QueueLimit);
        s.systemAudio = checked(SystemAudio);
        s.allowVideoOnly = checked(VideoOnly);
        s.requireFull = checked(FullOnly);
        s.notifications = checked(Notify);
        s.autoStart = checked(AutoStart);
        int m = int(SendMessageW(get(MonitorChoice), CB_GETCURSEL, 0, 0)),
            a = int(SendMessageW(get(AudioChoice), CB_GETCURSEL, 0, 0));
        if (m < 0 || m >= int(screens.size()))
            throw std::runtime_error("모니터를 선택하십시오.");
        s.monitor = screens[m].id;
        s.audioDevice = a > 0 && a <= int(devices.size()) ? devices[a - 1].id : L"";
        return s;
    }
    void autoRun(bool enable) {
        HKEY key{};
        auto r = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                                 nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (r != ERROR_SUCCESS)
            check(HRESULT_FROM_WIN32(r));
        if (enable) {
            wchar_t path[32768]{};
            GetModuleFileNameW(nullptr, path, 32768);
            std::wstring value = L"\"" + std::wstring(path) + L"\"";
            r = RegSetValueExW(key, L"ReplayCapture", 0, REG_SZ,
                               reinterpret_cast<BYTE const *>(value.c_str()), DWORD((value.size() + 1) * 2));
        } else {
            r = RegDeleteValueW(key, L"ReplayCapture");
            if (r == ERROR_FILE_NOT_FOUND)
                r = ERROR_SUCCESS;
        }
        RegCloseKey(key);
        check(HRESULT_FROM_WIN32(r));
    }
    void apply() {
        auto s = readFields();
        auto invalid = s.validate();
        if (!invalid.empty())
            throw std::runtime_error(winrt::to_string(invalid));
        bool running = engine.status().state == L"녹화 중";
        if (running &&
            MessageBoxW(window,
                        L"설정 적용 시 미저장 버퍼가 초기화되고 녹화가 다시 시작됩니다. 적용하시겠습니까?",
                        L"설정 적용", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        autoRun(s.autoStart);
        try {
            s.save();
        } catch (...) {
            autoRun(settings.autoStart);
            throw;
        }
        settings = s;
        number(ThisSeconds, s.saveSeconds);
        if (running)
            engine.start(settings);
        keyLabel(keyRegistered ? L"등록됨" : L"단축키 등록 실패");
        alert(L"설정이 저장되었습니다.");
    }
    void applyKey() {
        auto raw = static_cast<WORD>(SendMessageW(get(KeyField), HKM_GETHOTKEY, 0, 0));
        UINT key = LOBYTE(raw), flags = HIBYTE(raw), mod = 0;
        if (flags & HOTKEYF_CONTROL)
            mod |= MOD_CONTROL;
        if (flags & HOTKEYF_ALT)
            mod |= MOD_ALT;
        if (flags & HOTKEYF_SHIFT)
            mod |= MOD_SHIFT;
        Settings s = settings;
        s.hotkeyKey = key;
        s.hotkeyModifiers = mod;
        auto invalid = s.validate();
        if (!invalid.empty())
            throw std::runtime_error(winrt::to_string(invalid));
        if (keyRegistered && key == settings.hotkeyKey && mod == settings.hotkeyModifiers) {
            keyLabel(L"이미 적용된 키입니다.");
            return;
        }
        int next = hotkeyId == 1 ? 2 : 1;
        if (!RegisterHotKey(window, next, mod | MOD_NOREPEAT, key))
            throw std::runtime_error("단축키가 사용 중이거나 등록할 수 없습니다. 기존 키는 유지됩니다.");
        try {
            s.save();
        } catch (...) {
            UnregisterHotKey(window, next);
            throw;
        }
        if (keyRegistered)
            UnregisterHotKey(window, hotkeyId);
        hotkeyId = next;
        keyRegistered = true;
        settings = s;
        keyLabel(L"새 단축키가 적용되었습니다.");
    }
    void browse() {
        ComPtr<IFileOpenDialog> d;
        check(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)));
        DWORD o{};
        check(d->GetOptions(&o));
        check(d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM));
        if (d->Show(window) == HRESULT_FROM_WIN32(ERROR_CANCELLED))
            return;
        ComPtr<IShellItem> i;
        check(d->GetResult(&i));
        PWSTR p{};
        check(i->GetDisplayName(SIGDN_FILESYSPATH, &p));
        text(Folder, p);
        CoTaskMemFree(p);
    }
    void requestSave(int seconds) {
        try {
            auto id = engine.save(seconds, settings.requireFull);
            balloon(L"최근 기록 저장 접수 · 작업 " + std::to_wstring(id));
        } catch (...) {
            auto e = errorText();
            text(StatusText, e);
            balloon(e);
            if (IsWindowVisible(window))
                alert(e, true);
        }
    }
    std::vector<JobView> currentJobs;
    void tick() {
        auto st = engine.status();
        std::wostringstream summary;
        summary << L"보관된 기록  " << std::fixed << std::setprecision(1) << st.available << L" / "
                << settings.retention << L"초\n소리: " << st.audio << L"     저장 단축키: "
                << hotkeyName(settings.hotkeyModifiers, settings.hotkeyKey) << L"\n압축 버퍼 "
                << st.bytes / MiB << L" MiB   ·   저장 참조 " << st.pinned / MiB << L" MiB";
        text(Summary, summary.str());
        text(StatusText, closing ? L"저장 작업을 마친 뒤 종료합니다 (최대 10초)."
                                 : st.state + (st.error.empty() ? L"" : L" — " + st.error));
        SendMessageW(get(Progress), PBM_SETPOS, std::min(1000, int(st.available / settings.retention * 1000)),
                     0);
        bool running = st.state == L"녹화 중", paused = st.state == L"일시정지";
        EnableWindow(get(Start), !running && st.state != L"시작 중");
        EnableWindow(get(Pause), running || paused);
        text(Pause, paused ? L"재개" : L"일시정지");
        EnableWindow(get(Stop), running || paused || st.state == L"오류");
        EnableWindow(get(Clear), running);
        int seconds = settings.saveSeconds;
        try {
            seconds = number(ThisSeconds);
        } catch (...) {
        }
        text(Save, L"최근 " + std::to_wstring(seconds) + L"초 저장");
        EnableWindow(get(Save),
                     running && st.available > 0 && (!settings.requireFull || st.available >= seconds));
        std::wostringstream diag;
        diag << L"ReplayCapture 0.1.0\r\n상태: " << st.state << L"\r\n인코더: " << st.encoder << L"\r\n영상: "
             << settings.width << L"×" << settings.height << L" @ " << settings.fps << L"fps\r\n입력 프레임: "
             << st.frames << L" / 누락: " << st.drops << L"\r\n압축 메모리: " << st.bytes
             << L" bytes\r\n저장 참조: " << st.pinned << L" bytes\r\n실제 보관: " << st.available
             << L"초\r\n오디오: " << st.audio << L"\r\n오류: " << st.error << L"\r\n단축키: "
             << (keyRegistered ? L"등록됨" : L"등록 실패")
             << L"\r\n환경 제한: SDR / 현재 선택한 출력 장치의 소리\r\n";
        text(Diagnostics, diag.str());
        auto jobs = engine.jobs();
        int selected = ListView_GetNextItem(get(Jobs), -1, LVNI_SELECTED);
        uint64_t selectedId =
            selected >= 0 && selected < int(currentJobs.size()) ? currentJobs[selected].id : 0;
        bool changed = jobs.size() != currentJobs.size();
        if (!changed)
            for (size_t i = 0; i < jobs.size(); ++i)
                if (jobs[i].state != currentJobs[i].state)
                    changed = true;
        if (changed) {
            ListView_DeleteAllItems(get(Jobs));
            for (size_t i = 0; i < jobs.size(); ++i) {
                auto const &j = jobs[i];
                std::wstring id = std::to_wstring(j.id);
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = int(i);
                item.pszText = id.data();
                ListView_InsertItem(get(Jobs), &item);
                ListView_SetItemText(get(Jobs), int(i), 1, const_cast<LPWSTR>(j.state.c_str()));
                std::wostringstream lengths;
                lengths << j.requested << L" / " << std::fixed << std::setprecision(1) << j.actual;
                auto t = lengths.str();
                ListView_SetItemText(get(Jobs), int(i), 2, t.data());
                auto description =
                    j.path.empty() ? j.message : std::filesystem::path(j.path).filename().wstring();
                ListView_SetItemText(get(Jobs), int(i), 3, description.data());
                if (j.id == selectedId)
                    ListView_SetItemState(get(Jobs), int(i), LVIS_SELECTED, LVIS_SELECTED);
            }
        }
        currentJobs = std::move(jobs);
        if (closing) {
            for (auto const &c : controls)
                if (c.page != 3 && c.page != -1)
                    EnableWindow(c.handle, FALSE);
            bool pending = std::any_of(currentJobs.begin(), currentJobs.end(), [](auto const &j) {
                return j.state == L"대기" || j.state == L"저장 중" || j.state == L"접수";
            });
            if (!pending || clockNow() > exitDeadline) {
                DestroyWindow(window);
                return;
            }
        }
        for (auto const &j : currentJobs)
            if ((j.state == L"완료" || j.state == L"실패") && announced.insert(j.id).second)
                balloon(L"작업 " + std::to_wstring(j.id) + L" " + j.state + L" · " + j.message);
        NOTIFYICONDATAW n{sizeof(n)};
        n.hWnd = window;
        n.uID = 1;
        n.uFlags = NIF_TIP;
        auto tip = L"ReplayCapture — " + st.state;
        wcsncpy_s(n.szTip, tip.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &n);
    }
    JobView selectedJob() {
        int i = ListView_GetNextItem(get(Jobs), -1, LVNI_SELECTED);
        if (i < 0 || i >= int(currentJobs.size()))
            throw std::runtime_error("작업을 먼저 선택하십시오.");
        return currentJobs[i];
    }
    void copy() {
        auto s = text(Diagnostics);
        if (!OpenClipboard(window))
            return;
        EmptyClipboard();
        HGLOBAL m = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
        if (m) {
            void *p = GlobalLock(m);
            memcpy(p, s.c_str(), (s.size() + 1) * 2);
            GlobalUnlock(m);
            if (!SetClipboardData(CF_UNICODETEXT, m))
                GlobalFree(m);
        }
        CloseClipboard();
    }
    void exportDiag() {
        ComPtr<IFileSaveDialog> d;
        check(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)));
        d->SetFileName(L"ReplayCapture-diagnostics.txt");
        if (d->Show(window) == HRESULT_FROM_WIN32(ERROR_CANCELLED))
            return;
        ComPtr<IShellItem> i;
        check(d->GetResult(&i));
        PWSTR path{};
        check(i->GetDisplayName(SIGDN_FILESYSPATH, &path));
        std::filesystem::path p(path);
        CoTaskMemFree(path);
        std::ofstream f(p, std::ios::binary);
        f << winrt::to_string(text(Diagnostics));
        if (!f)
            throw std::runtime_error("진단 파일을 저장하지 못했습니다.");
    }
    void command(int id) {
        if (closing && id != CancelJob)
            return;
        try {
            switch (id) {
            case Start:
                engine.start(settings);
                break;
            case Pause:
                if (engine.status().state == L"일시정지")
                    engine.start(settings);
                else
                    engine.stop(true);
                break;
            case Stop:
                engine.stop();
                break;
            case Clear:
                engine.clear();
                break;
            case Save:
                requestSave(number(ThisSeconds));
                break;
            case ResetSeconds:
                number(ThisSeconds, settings.saveSeconds);
                break;
            case Apply:
                apply();
                break;
            case Browse:
                browse();
                break;
            case OpenFolder:
                std::filesystem::create_directories(text(Folder));
                ShellExecuteW(window, L"open", text(Folder).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case RefreshDevices:
                refreshDevices(settings);
                break;
            case Defaults:
                loadFields(Settings{});
                break;
            case ApplyKey:
                applyKey();
                break;
            case ResetKey:
                SendMessageW(get(KeyField), HKM_SETHOTKEY, MAKEWORD(VK_F9, HOTKEYF_CONTROL | HOTKEYF_SHIFT),
                             0);
                keyLabel(L"기본 키를 불러왔습니다. 적용을 눌러 저장하십시오.");
                break;
            case TestKey:
                keyTest = !keyTest;
                keyLabel(keyTest ? L"테스트 중: 현재 적용된 단축키를 누르십시오. 영상은 저장되지 않습니다."
                                 : L"테스트 종료");
                break;
            case OpenVideo: {
                auto j = selectedJob();
                if (j.state != L"완료")
                    throw std::runtime_error("완료된 영상을 선택하십시오.");
                ShellExecuteW(window, L"open", j.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            }
            case RevealVideo: {
                auto j = selectedJob();
                if (j.path.empty())
                    throw std::runtime_error("파일 경로가 없습니다.");
                auto args = L"/select,\"" + j.path + L"\"";
                ShellExecuteW(window, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
                break;
            }
            case CancelJob:
                engine.cancel(selectedJob().id);
                break;
            case CopyDiagnostics:
                copy();
                break;
            case ExportDiagnostics:
                exportDiag();
                break;
            case Reconnect:
                engine.start(settings);
                break;
            case Exit:
                closing = true;
                engine.stop();
                exitDeadline = clockNow() + 10 * Second;
                show();
                break;
            default:
                break;
            }
        } catch (...) {
            alert(errorText(), true);
        }
    }
    void trayMenu() {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 600, L"창 열기");
        auto save = L"최근 " + std::to_wstring(settings.saveSeconds) + L"초 저장";
        AppendMenuW(menu, MF_STRING, 601, save.c_str());
        AppendMenuW(menu, MF_STRING, Start, L"녹화 시작");
        AppendMenuW(menu, MF_STRING, Pause, L"일시정지 / 재개");
        AppendMenuW(menu, MF_STRING, Stop, L"녹화 중지");
        AppendMenuW(menu, MF_STRING, 602, L"설정");
        AppendMenuW(menu, MF_STRING, Exit, L"앱 종료");
        POINT p;
        GetCursorPos(&p);
        SetForegroundWindow(window);
        int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (id == 600)
            show();
        else if (id == 601)
            requestSave(settings.saveSeconds);
        else if (id == 602) {
            show();
            selectPage(1);
        } else if (id)
            command(id);
    }
};
LRESULT CALLBACK windowProc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    auto app = reinterpret_cast<App *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW *>(lp);
        app = static_cast<App *>(cs->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(window, msg, wp, lp);
    try {
        switch (msg) {
        case WM_CREATE:
            app->create();
            return 0;
        case WM_COMMAND:
            // Focus/highlight notifications must never execute a button action.
            if (HIWORD(wp) == BN_CLICKED)
                app->command(LOWORD(wp));
            return 0;
        case WM_NOTIFY: {
            auto n = reinterpret_cast<NMHDR *>(lp);
            if (n->idFrom == Tab && n->code == TCN_SELCHANGE)
                app->selectPage(TabCtrl_GetCurSel(app->tab));
            if (n->idFrom == Jobs && n->code == LVN_ITEMCHANGED) {
                try {
                    auto j = app->selectedJob();
                    app->text(JobDetail, j.message + L"\n" + j.path);
                } catch (...) {
                }
            }
            return 0;
        }
        case WM_TIMER:
            app->tick();
            return 0;
        case WM_HOTKEY:
            if (int(wp) == app->hotkeyId) {
                if (app->page == 2 && IsWindowVisible(window) && !IsIconic(window)) {
                    app->keyLabel(app->keyTest ? L"키 입력을 정상 수신했습니다. 테스트 성공!"
                                               : L"단축키 편집 화면에서는 저장하지 않습니다.");
                } else
                    app->requestSave(app->settings.saveSeconds);
            }
            return 0;
        case TrayMessage:
            if (lp == WM_LBUTTONUP)
                app->show();
            else if (lp == WM_RBUTTONUP)
                app->trayMenu();
            return 0;
        case WM_CLOSE:
            ShowWindow(window, SW_HIDE);
            app->balloon(L"트레이에서 계속 실행합니다. 종료하려면 메뉴의 앱 종료를 선택하십시오.");
            return 0;
        case WM_WTSSESSION_CHANGE:
            if (wp == WTS_SESSION_LOCK) {
                app->wasLocked = app->engine.status().state == L"녹화 중";
                app->engine.stop(true);
            } else if (wp == WTS_SESSION_UNLOCK && app->wasLocked) {
                app->wasLocked = false;
                app->engine.start(app->settings);
            }
            return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMSUSPEND) {
                app->wasLocked = app->engine.status().state == L"녹화 중";
                app->engine.stop(true);
            } else if (wp == PBT_APMRESUMEAUTOMATIC && app->wasLocked) {
                app->wasLocked = false;
                app->engine.start(app->settings);
            }
            return TRUE;
        case WM_DPICHANGED: {
            app->fitDpi();
            app->fonts();
            auto r = reinterpret_cast<RECT *>(lp);
            SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER);
            app->layout();
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, RGB(26, 38, 57));
            SetBkColor(dc, RGB(246, 248, 252));
            return reinterpret_cast<LRESULT>(app->background);
        }
        case WM_ERASEBKGND: {
            RECT r;
            GetClientRect(window, &r);
            FillRect(reinterpret_cast<HDC>(wp), &r, app->background);
            return 1;
        }
        case WM_DESTROY: {
            KillTimer(window, 1);
            UnregisterHotKey(window, app->hotkeyId);
            WTSUnRegisterSessionNotification(window);
            NOTIFYICONDATAW n{sizeof(n)};
            n.hWnd = window;
            n.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &n);
            PostQuitMessage(0);
            return 0;
        }
        default:
            break;
        }
    } catch (...) {
        app->alert(errorText(), true);
    }
    return DefWindowProcW(window, msg, wp, lp);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        Runtime runtime(winrt::apartment_type::single_threaded);
        HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\ReplayCapture.Application");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            auto existing = FindWindowW(L"ReplayCapture.Window", nullptr);
            if (existing) {
                ShowWindow(existing, SW_RESTORE);
                SetForegroundWindow(existing);
            }
            if (mutex)
                CloseHandle(mutex);
            return 0;
        }
        INITCOMMONCONTROLSEX init{sizeof(init), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&init);
        App app;
        app.settings = Settings::load(app.warning);
        WNDCLASSEXW c{sizeof(c)};
        c.hInstance = instance;
        c.lpszClassName = L"ReplayCapture.Window";
        c.lpfnWndProc = windowProc;
        c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        c.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_REPLAYCAPTURE));
        c.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_REPLAYCAPTURE), IMAGE_ICON,
                                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                                  LR_DEFAULTCOLOR));
        RegisterClassExW(&c);
        UINT dpi = GetDpiForSystem();
        RECT rect{0, 0, MulDiv(960, dpi, 96), MulDiv(700, dpi, 96)};
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
        HWND window =
            CreateWindowExW(0, c.lpszClassName, L"ReplayCapture", style, CW_USEDEFAULT, CW_USEDEFAULT,
                            rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, &app);
        if (!window)
            check(HRESULT_FROM_WIN32(GetLastError()));
        rect = {0, 0, app.px(960), app.px(700)};
        AdjustWindowRectExForDpi(&rect, style, FALSE, 0, GetDpiForWindow(window));
        SetWindowPos(window, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOMOVE | SWP_NOZORDER);
        ShowWindow(window, show);
        UpdateWindow(window);
        if (!app.warning.empty())
            app.alert(app.warning, true);
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(window, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        CloseHandle(mutex);
        return 0;
    } catch (...) {
        MessageBoxW(nullptr, errorText().c_str(), L"ReplayCapture 시작 오류", MB_OK | MB_ICONERROR);
        return 1;
    }
}
