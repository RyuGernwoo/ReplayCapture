#include "engine.h"
#include "resource.h"
#include "ui/theme.h"
#include "ui/view_state.h"
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
    Reconnect,
    SaveHint = 550,
    PathHint,
    RecentJob,
    SettingsHint,
    Revert,
    Advanced,
    JumpKey,
    DashboardFolder,
    NavRecord = 560,
    NavSettings,
    NavKey,
    NavJobs,
    NavDiagnostics
};
struct Control {
    HWND handle;
    int page, x, y, w, h;
};
struct Row {
    int page, height;
    std::vector<HWND> items;
};
struct App {
    HWND window{}, tab{};
    HFONT font{}, titleFont{};
    Engine engine;
    Settings settings;
    std::vector<Monitor> screens;
    std::vector<AudioDevice> devices;
    std::vector<Control> controls;
    std::vector<Row> rows;
    std::vector<HWND> headings;
    HWND title{}, subtitle{};
    int scroll = 0, contentHeight = 0;
    bool advanced = false, loading = false, dirty = false;
    std::map<int, HWND> fields;
    std::set<uint64_t> announced;
    UINT dpi = 96;
    int page = 0, hotkeyId = 1;
    bool keyRegistered = false, keyTest = false, wasLocked = false, closing = false;
    bool isRecording = false;
    int64_t exitDeadline{};
    std::wstring warning;
    HBRUSH background = CreateSolidBrush(ui::background());
    ~App() {
        DeleteObject(font);
        DeleteObject(titleFont);
        DeleteObject(background);
    }
    int px(int n) const {
        return MulDiv(n, dpi, 96);
    }
    void fitDpi() {
        dpi = GetDpiForWindow(window);
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
        SetWindowSubclass(
            c,
            [](HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) -> LRESULT {
                auto app = reinterpret_cast<App *>(data);
                if (msg == WM_SETFOCUS)
                    SendMessageW(app->window, WM_APP + 2, reinterpret_cast<WPARAM>(h), 0);
                return DefSubclassProc(h, msg, wp, lp);
            },
            1, reinterpret_cast<DWORD_PTR>(this));
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
        if (text(id) != s)
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
        scroll = 0;
        layout();
        keyTest = false;
        if (fields.contains(TestKey))
            text(TestKey, L"단축키 테스트");
    }
    void layout() {
        RECT client{};
        GetClientRect(window, &client);
        int width = MulDiv(client.right, 96, dpi), height = MulDiv(client.bottom, 96, dpi);
        bool wide = width >= 1000;
        int left = wide ? 208 : 24, area = std::max(240, width - left - 24);
        auto move = [&](HWND h, int x, int y, int w, int ht) {
            MoveWindow(h, px(x), px(y - scroll), px(w), px(ht), TRUE);
        };
        for (auto const &c : controls)
            ShowWindow(c.handle, c.page == page || (c.page == 5 && page == 1 && advanced) || c.page < 0
                                     ? SW_SHOW
                                     : SW_HIDE);
        move(title, 24, 18, width - 48, 38);
        move(subtitle, 24, 60, width - 48, 26);
        ShowWindow(tab, wide ? SW_HIDE : SW_SHOW);
        move(tab, 24, 100, width - 48, 36);
        for (int i = 0; i < 5; ++i) {
            auto h = get(NavRecord + i);
            ShowWindow(h, wide ? SW_SHOW : SW_HIDE);
            move(h, 24, 112 + i * 54, 164, 44);
        }
        int y = wide ? 110 : 160;
        for (auto const &row : rows) {
            if (row.page != page && !(row.page == 5 && page == 1 && advanced))
                continue;
            if (row.items.size() == 1 && GetDlgCtrlID(row.items[0]) >= 1000 &&
                GetWindowTextLengthW(row.items[0]) == 0) {
                ShowWindow(row.items[0], SW_HIDE);
                continue;
            }
            int count = int(row.items.size());
            int cell = (area - (count - 1) * 12) / count;
            bool stack = cell < 180 && count > 1;
            for (int i = 0; i < count; ++i) {
                auto h = row.items[i];
                wchar_t klass[64]{};
                GetClassNameW(h, klass, 64);
                int ht = wcscmp(klass, WC_COMBOBOXW) == 0 ? 250 : row.height;
                move(h, stack ? left : left + i * (cell + 12), y, stack ? area : cell, ht);
                if (stack)
                    y += row.height + 12;
            }
            if (!stack)
                y += row.height + 12;
        }
        move(get(Exit), left, y + 12, 164, 40);
        contentHeight = y + 76;
        int bounded = std::clamp(scroll, 0, std::max(0, contentHeight - height));
        if (bounded != scroll) {
            scroll = bounded;
            layout();
            return;
        }
        SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
        si.nMax = contentHeight - 1;
        si.nPage = height;
        si.nPos = scroll;
        SetScrollInfo(window, SB_VERT, &si, TRUE);
        int listWidth = px(area);
        for (int i = 0; i < 3; ++i)
            ListView_SetColumnWidth(get(Jobs), i, px(i == 2 ? 150 : 76));
        ListView_SetColumnWidth(get(Jobs), 3, std::max(px(120), listWidth - px(302)));
        InvalidateRect(window, nullptr, TRUE);
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
        for (auto h : headings)
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
    }
    void row(int p, int height, std::initializer_list<HWND> items) {
        rows.push_back({p, height, items});
    }
    HWND uiLabel(std::wstring const &value, int p, int height = 24, bool heading = false, int id = 0) {
        auto h = control(L"STATIC", value, id, p, 0, 0, 600, height);
        if (heading) {
            headings.push_back(h);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
        }
        row(p, height, {h});
        return h;
    }
    HWND uiButton(std::wstring const &value, int id, int p) {
        button(value, id, p, 0, 0);
        return get(id);
    }
    void uiField(std::wstring const &value, int id, int p, bool numeric = true) {
        uiLabel(value, p);
        edit(id, p, 0, 0, 500, numeric);
        row(p, 36, {get(id)});
        uiLabel(L"", p, 44, false, id + 1000);
    }
    void uiCheck(std::wstring const &value, int id, int p) {
        auto h = control(L"BUTTON", value, id, p, 0, 0, 600, 36, WS_TABSTOP | BS_AUTOCHECKBOX);
        row(p, 36, {h});
    }
    void create() {
        fitDpi();
        fonts();
        title = control(L"STATIC", L"ReplayCapture", 0, -1, 0, 0, 600, 38);
        headings.push_back(title);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
        subtitle = control(L"STATIC", L"지나간 순간을, 화면과 소리 그대로", 0, -1, 0, 0, 600, 26);
        tab = control(WC_TABCONTROLW, L"", Tab, -1, 0, 0, 600, 36, WS_TABSTOP);
        wchar_t const *names[] = {L"녹화", L"설정", L"단축키", L"저장 작업", L"도움말·진단"};
        for (int i = 0; i < 5; ++i) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<LPWSTR>(names[i]);
            TabCtrl_InsertItem(tab, i, &item);
            uiButton(names[i], NavRecord + i, -1);
        }
        uiLabel(L"녹화를 시작해 보세요", 0, 40, true, StatusText);
        uiLabel(L"", 0, 60, false, Summary);
        control(PROGRESS_CLASSW, L"", Progress, 0, 0, 0, 600, 8);
        SendMessageW(get(Progress), PBM_SETRANGE32, 0, 1000);
        row(0, 8, {get(Progress)});
        uiLabel(L"이번에 저장할 길이 (초) · 보관 시간 이내", 0);
        edit(ThisSeconds, 0, 0, 0);
        row(0, 36, {get(ThisSeconds), uiButton(L"기본 시간으로", ResetSeconds, 0)});
        row(0, 56, {uiButton(L"최근 60초 저장", Save, 0)});
        uiLabel(L"", 0, 40, false, SaveHint);
        row(0, 40, {uiButton(L"단축키 변경", JumpKey, 0), uiButton(L"저장 폴더 열기", DashboardFolder, 0)});
        row(0, 40, {uiButton(L"녹화 시작", Start, 0), uiButton(L"일시정지", Pause, 0)});
        row(0, 36, {uiButton(L"녹화 중지", Stop, 0), uiButton(L"버퍼 비우기", Clear, 0)});
        uiLabel(L"일시정지·중지·버퍼 비우기는 아직 저장하지 않은 기록을 지웁니다.", 0, 40);
        uiLabel(L"", 0, 48, false, PathHint);
        uiLabel(L"아직 저장 작업이 없습니다.", 0, 40, false, RecentJob);

        uiLabel(L"화면과 소리", 1, 36, true);
        uiLabel(L"녹화할 모니터", 1);
        control(WC_COMBOBOXW, L"", MonitorChoice, 1, 0, 0, 600, 250,
                WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
        row(1, 36, {get(MonitorChoice), uiButton(L"장치 새로고침", RefreshDevices, 1)});
        uiLabel(L"소리가 재생되는 출력 장치", 1);
        control(WC_COMBOBOXW, L"", AudioChoice, 1, 0, 0, 600, 250,
                WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
        row(1, 36, {get(AudioChoice)});
        uiCheck(L"시스템 소리 포함", SystemAudio, 1);
        uiCheck(L"소리 연결에 실패하면 영상만 저장 허용", VideoOnly, 1);
        uiLabel(L"기록과 저장", 1, 36, true);
        uiField(L"최근 기록 보관 시간 (5~600초) · 메모리에 남길 최대 길이", Retention, 1);
        uiField(L"기본 저장 길이 (초) · 단축키와 트레이에서 사용", DefaultSeconds, 1);
        uiLabel(L"예: 보관 120초 / 기본 60초 → 최대 2분을 유지하고 단축키는 최근 1분 저장", 1, 40);
        uiCheck(L"전체 길이가 쌓인 경우만 저장", FullOnly, 1);
        uiField(L"저장 폴더", Folder, 1, false);
        row(1, 36, {uiButton(L"찾아보기", Browse, 1), uiButton(L"폴더 열기", OpenFolder, 1)});
        uiLabel(L"일반", 1, 36, true);
        uiCheck(L"저장 완료·오류 알림", Notify, 1);
        uiCheck(L"Windows 로그인 시 앱 자동 실행", AutoStart, 1);
        row(1, 40, {uiButton(L"고급 설정 펼치기", Advanced, 1)});

        uiLabel(L"화질과 자원", 5, 36, true);
        uiField(L"출력 폭 (320~3840, 짝수)", Width, 5);
        uiField(L"출력 높이 (240~2160, 짝수)", Height, 5);
        uiField(L"FPS (10~60) · 초당 화면 수", Fps, 5);
        uiField(L"영상 비트레이트 (1~50 Mbps)", Bitrate, 5);
        uiField(L"최근 기록 메모리 한도 (32~2048 MiB)", BufferLimit, 5);
        uiField(L"녹화·저장 메모리 예산 (MiB)", TotalLimit, 5);
        uiLabel(L"총 예산은 저장 참조와 예약량을 포함한 제한이며, 전체 프로세스 메모리 상한은 아닙니다.", 5,
                44);
        uiField(L"저장 대기 수 (1~10)", QueueLimit, 5);
        uiLabel(L"SDR · H.264 / AAC · 앞쪽 키프레임부터 저장해 실제 길이는 조금 길어질 수 있습니다.", 5, 44);
        uiLabel(L"설정 적용 시 현재 기록이 비워집니다. 진행 중인 저장 작업은 유지됩니다.", 1, 40);
        uiLabel(L"설정이 적용되어 있습니다.", 1, 48, false, SettingsHint);
        row(1, 40, {uiButton(L"설정 적용", Apply, 1), uiButton(L"변경 되돌리기", Revert, 1)});
        row(1, 36, {uiButton(L"기본값 불러오기", Defaults, 1)});

        uiLabel(L"내 단축키", 2, 40, true);
        uiLabel(L"입력칸을 선택하고 Ctrl / Alt / Shift + 키 조합을 누르세요.", 2, 40);
        control(HOTKEY_CLASSW, L"", KeyField, 2, 0, 0, 600, 40, WS_TABSTOP);
        row(2, 44, {get(KeyField)});
        row(2, 40, {uiButton(L"단축키 적용", ApplyKey, 2), uiButton(L"기본 키 불러오기", ResetKey, 2)});
        row(2, 40, {uiButton(L"단축키 테스트", TestKey, 2)});
        uiLabel(L"", 2, 110, false, KeyStatus);
        uiLabel(L"이 화면에서는 키를 눌러도 저장하지 않습니다.\n녹화 화면으로 돌아가거나 창을 숨기면 기본 "
                L"저장 길이로 저장합니다.\n충돌 시 기존 단축키를 유지합니다.",
                2, 90);

        uiLabel(L"저장 작업", 3, 40, true);
        uiLabel(L"이번 실행에서 요청한 작업입니다. 완료한 영상을 선택해 열 수 있습니다.", 3, 40);
        control(WC_LISTVIEWW, L"", Jobs, 3, 0, 0, 600, 300,
                WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
        ListView_SetExtendedListViewStyle(get(Jobs), LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        wchar_t const *cols[] = {L"작업", L"상태", L"요청 / 실제 (초)", L"파일 / 메시지"};
        for (int i = 0; i < 4; ++i) {
            LVCOLUMNW c{};
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.pszText = const_cast<LPWSTR>(cols[i]);
            c.cx = px(140);
            ListView_InsertColumn(get(Jobs), i, &c);
        }
        row(3, 300, {get(Jobs)});
        row(3, 40, {uiButton(L"영상 열기", OpenVideo, 3), uiButton(L"폴더에서 보기", RevealVideo, 3)});
        row(3, 36, {uiButton(L"작업 취소", CancelJob, 3)});
        uiLabel(L"아직 저장 작업이 없습니다. 녹화 화면에서 최근 기록을 저장하세요.", 3, 110, false,
                JobDetail);

        uiLabel(L"도움말 및 진단", 4, 40, true);
        uiLabel(L"소리가 없나요? 설정에서 실제 재생 중인 출력 장치를 확인하세요.\n저장이 실패하나요? 저장 "
                L"폴더의 쓰기 권한과 남은 공간을 확인하세요.",
                4, 68);
        control(L"EDIT", L"", Diagnostics, 4, 0, 0, 600, 300,
                WS_TABSTOP | ES_MULTILINE | ES_READONLY | WS_VSCROLL);
        row(4, 300, {get(Diagnostics)});
        row(4, 40,
            {uiButton(L"진단 복사", CopyDiagnostics, 4), uiButton(L"진단 파일 저장", ExportDiagnostics, 4)});
        row(4, 40, {uiButton(L"장치 다시 연결", Reconnect, 4)});
        uiLabel(L"재연결하면 기존 기록을 비우고 녹화를 시작합니다.\n진단 내용을 공유하기 전에 개인 정보가 "
                L"포함되어 있는지 확인하세요.",
                4, 64);
        uiButton(L"앱 종료", Exit, -1);
        loadFields(settings);
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
        selectPage(0);
        SetTimer(window, 1, 300, nullptr);
        tick();
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
        loading = true;
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
        loading = false;
    }
    void draftChanged() {
        if (loading)
            return;
        dirty = true;
        text(SettingsHint, L"적용되지 않은 변경 사항이 있습니다. 화면을 이동해도 편집 값은 유지됩니다.");
    }
    void fieldError(int id, std::wstring const &message) {
        if (id >= Width && id <= QueueLimit) {
            advanced = true;
            text(Advanced, L"고급 설정 접기");
        }
        if (fields.contains(id + 1000))
            text(id + 1000, message);
        layout();
        SetFocus(get(id));
    }
    bool validateDraft() {
        for (auto const &[id, h] : fields)
            if (id >= 1000)
                SetWindowTextW(h, L"");
        struct Limit {
            int id, low, high;
            bool even;
        };
        for (auto f : {Limit{Retention, 5, 600, false},
                       {DefaultSeconds, 1, 600, false},
                       {Width, 320, 3840, true},
                       {Height, 240, 2160, true},
                       {Fps, 10, 60, false},
                       {Bitrate, 1, 50, false},
                       {BufferLimit, 32, 2048, false},
                       {TotalLimit, 160, 4096, false},
                       {QueueLimit, 1, 10, false}}) {
            int value = 0;
            try {
                value = number(f.id);
            } catch (...) {
            }
            if (value < f.low || value > f.high || (f.even && value % 2)) {
                fieldError(f.id,
                           std::to_wstring(f.low) + L"~" + std::to_wstring(f.high) +
                               (f.even ? L" 사이의 짝수를 입력하세요." : L" 사이의 정수를 입력하세요."));
                return false;
            }
        }
        if (number(DefaultSeconds) > number(Retention)) {
            fieldError(DefaultSeconds, L"기본 저장 길이는 보관 시간 이하여야 합니다.");
            return false;
        }
        if (number(TotalLimit) < number(BufferLimit) + 128) {
            fieldError(TotalLimit, L"총 예산은 최근 기록 메모리 한도보다 128 MiB 이상 커야 합니다.");
            return false;
        }
        if (text(Folder).empty() || !std::filesystem::path(text(Folder)).is_absolute()) {
            fieldError(Folder, L"절대 경로의 저장 폴더를 선택하세요.");
            return false;
        }
        layout();
        return true;
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
        if (!validateDraft())
            return;
        auto s = readFields();
        auto invalid = s.validate();
        if (!invalid.empty()) {
            fieldError(BufferLimit, invalid);
            return;
        }
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
        dirty = false;
        text(SettingsHint, L"설정이 저장되었습니다. 현재 적용된 값을 사용합니다.");
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
            text(RecentJob,
                 L"저장 요청 접수 · 작업 " + std::to_wstring(id) + L" — 저장 작업 화면에서 확인하세요.");
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
        summary << L"보관된 기록  " << std::fixed << std::setprecision(1)
                << std::min(st.available, double(settings.retention)) << L"초 / 목표 " << settings.retention
                << L"초\n시스템 소리: " << st.audio;
        auto monitor = std::find_if(screens.begin(), screens.end(), [&](auto const &m) {
            return settings.monitor.empty() || m.id == settings.monitor;
        });
        summary << L"\n모니터: " << (monitor == screens.end() ? L"장치를 확인하세요" : monitor->label);
        text(Summary, summary.str());
        text(StatusText, closing ? L"저장 후 종료 중" : st.state);
        SendMessageW(get(Progress), PBM_SETPOS, std::min(1000, int(st.available / settings.retention * 1000)),
                     0);
        bool running = st.state == L"녹화 중", paused = st.state == L"일시정지";
        if (isRecording != running) {
            isRecording = running;
            InvalidateRect(get(Start), nullptr, TRUE);
        }
        EnableWindow(get(Start), !running && st.state != L"시작 중");
        EnableWindow(get(Pause), running || paused);
        text(Pause, paused ? L"재개" : L"일시정지");
        EnableWindow(get(Stop), running || paused || st.state == L"오류");
        EnableWindow(get(Clear), running);
        int seconds = 0;
        try {
            seconds = number(ThisSeconds);
        } catch (...) {
        }
        text(Save, seconds > 0 && seconds <= settings.retention
                       ? L"최근 " + std::to_wstring(seconds) + L"초 저장"
                       : L"저장 길이를 확인하세요");
        auto saveView = ui::saveState(st, seconds, settings);
        EnableWindow(get(Save), saveView.enabled);
        text(SaveHint, st.error.empty()
                           ? saveView.explanation
                           : L"녹화 문제: 도움말·진단에서 원인을 확인하고 장치를 다시 연결하세요.");
        text(JumpKey, hotkeyName(settings.hotkeyModifiers, settings.hotkeyKey) + L" · 기본 " +
                          std::to_wstring(settings.saveSeconds) + L"초 · 변경");
        text(PathHint, L"저장 위치: " + settings.folder);
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
            int top = ListView_GetTopIndex(get(Jobs));
            SendMessageW(get(Jobs), WM_SETREDRAW, FALSE, 0);
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
            if (top > 0 && top < int(jobs.size()))
                ListView_EnsureVisible(get(Jobs), top, FALSE);
            SendMessageW(get(Jobs), WM_SETREDRAW, TRUE, 0);
            InvalidateRect(get(Jobs), nullptr, TRUE);
        }
        currentJobs = std::move(jobs);
        if (!currentJobs.empty()) {
            auto const &j = currentJobs.back();
            text(RecentJob,
                 L"최근 작업 " + std::to_wstring(j.id) + L" · " + j.state + L" — 저장 작업에서 확인");
        }
        int choice = ListView_GetNextItem(get(Jobs), -1, LVNI_SELECTED);
        bool chosen = choice >= 0 && choice < int(currentJobs.size());
        if (chosen)
            text(JobDetail, currentJobs[choice].state + L" · " + currentJobs[choice].message + L"\n" +
                                currentJobs[choice].path);
        EnableWindow(get(OpenVideo), chosen && currentJobs[choice].state == L"완료");
        EnableWindow(get(RevealVideo), chosen && currentJobs[choice].state == L"완료");
        EnableWindow(get(CancelJob), chosen && (currentJobs[choice].state == L"대기" ||
                                                currentJobs[choice].state == L"접수" ||
                                                currentJobs[choice].state == L"저장 중"));
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
            if (id >= NavRecord && id <= NavDiagnostics) {
                selectPage(id - NavRecord);
                return;
            }
            switch (id) {
            case Advanced:
                advanced = !advanced;
                text(Advanced, advanced ? L"고급 설정 접기" : L"고급 설정 펼치기");
                layout();
                break;
            case Revert:
                loadFields(settings);
                for (auto const &[key, h] : fields)
                    if (key >= 1000)
                        SetWindowTextW(h, L"");
                dirty = false;
                text(SettingsHint, L"변경을 되돌렸습니다. 현재 적용된 값입니다.");
                layout();
                break;
            case JumpKey:
                selectPage(2);
                SetFocus(get(KeyField));
                break;
            case DashboardFolder:
                std::filesystem::create_directories(settings.folder);
                ShellExecuteW(window, L"open", settings.folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
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
                draftChanged();
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
                text(TestKey, keyTest ? L"테스트 종료" : L"단축키 테스트");
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
            if ((LOWORD(wp) >= MonitorChoice && LOWORD(wp) <= AutoStart) &&
                (HIWORD(wp) == EN_CHANGE || HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == BN_CLICKED))
                app->draftChanged();
            return 0;
        case WM_SIZE:
            if (app->fields.contains(Exit))
                app->layout();
            return 0;
        case WM_NEXTDLGCTL: {
            HWND next = lp ? reinterpret_cast<HWND>(wp) : GetNextDlgTabItem(window, GetFocus(), wp != 0);
            if (next)
                SetFocus(next);
            return 0;
        }
        case WM_VSCROLL: {
            SCROLLINFO si{sizeof(si), SIF_ALL};
            GetScrollInfo(window, SB_VERT, &si);
            switch (LOWORD(wp)) {
            case SB_LINEUP:
                app->scroll -= 36;
                break;
            case SB_LINEDOWN:
                app->scroll += 36;
                break;
            case SB_PAGEUP:
                app->scroll -= int(si.nPage);
                break;
            case SB_PAGEDOWN:
                app->scroll += int(si.nPage);
                break;
            case SB_THUMBTRACK:
                app->scroll = si.nTrackPos;
                break;
            default:
                break;
            }
            app->scroll = std::clamp(app->scroll, 0, std::max(0, si.nMax - int(si.nPage) + 1));
            app->layout();
            return 0;
        }
        case WM_MOUSEWHEEL:
            app->scroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 72;
            app->layout();
            return 0;
        case WM_APP + 2: {
            auto h = reinterpret_cast<HWND>(wp);
            if (!IsWindowVisible(h))
                return 0;
            RECT r{}, client{};
            GetWindowRect(h, &r);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&r), 2);
            GetClientRect(window, &client);
            int delta = r.top < 0 ? r.top - app->px(12)
                                  : (r.bottom > client.bottom ? r.bottom - client.bottom + app->px(12) : 0);
            if (delta) {
                app->scroll += MulDiv(delta, 96, app->dpi);
                app->layout();
            }
            return 0;
        }
        case WM_NOTIFY: {
            auto n = reinterpret_cast<NMHDR *>(lp);
            if (n->code == NM_CUSTOMDRAW && (n->idFrom == Save || (n->idFrom == Start && !app->isRecording) ||
                                             (n->idFrom >= NavRecord && n->idFrom <= NavDiagnostics &&
                                              int(n->idFrom) - NavRecord == app->page))) {
                auto d = reinterpret_cast<NMCUSTOMDRAW *>(lp);
                if (d->dwDrawStage == CDDS_PREPAINT && IsWindowEnabled(n->hwndFrom) && !ui::highContrast()) {
                    auto brush = CreateSolidBrush(ui::accent());
                    FillRect(d->hdc, &d->rc, brush);
                    DeleteObject(brush);
                    SetBkMode(d->hdc, TRANSPARENT);
                    SetTextColor(d->hdc, ui::accentText());
                    auto value = app->text(int(n->idFrom));
                    RECT r = d->rc;
                    InflateRect(&r, -8, -2);
                    DrawTextW(d->hdc, value.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    if (d->uItemState & CDIS_FOCUS) {
                        InflateRect(&r, -3, -3);
                        DrawFocusRect(d->hdc, &r);
                    }
                    return CDRF_SKIPDEFAULT;
                }
            }
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
            app->dpi = HIWORD(wp);
            app->fonts();
            auto r = reinterpret_cast<RECT *>(lp);
            SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER);
            app->layout();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto limits = reinterpret_cast<MINMAXINFO *>(lp);
            MONITORINFO info{sizeof(info)};
            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
            limits->ptMinTrackSize.x = std::min(app->px(540), int(info.rcWork.right - info.rcWork.left));
            limits->ptMinTrackSize.y = std::min(app->px(320), int(info.rcWork.bottom - info.rcWork.top));
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, ui::foreground());
            SetBkColor(dc, ui::background());
            return reinterpret_cast<LRESULT>(app->background);
        }
        case WM_SETTINGCHANGE:
            DeleteObject(app->background);
            app->background = CreateSolidBrush(ui::background());
            InvalidateRect(window, nullptr, TRUE);
            return 0;
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
                                                  GetSystemMetrics(SM_CXSMICON),
                                                  GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        RegisterClassExW(&c);
        UINT dpi = GetDpiForSystem();
        RECT rect{0, 0, MulDiv(960, dpi, 96), MulDiv(700, dpi, 96)};
        DWORD style = WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN;
        AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
        HWND window =
            CreateWindowExW(0, c.lpszClassName, L"ReplayCapture", style, CW_USEDEFAULT, CW_USEDEFAULT,
                            rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, &app);
        if (!window)
            check(HRESULT_FROM_WIN32(GetLastError()));
        rect = {0, 0, app.px(1040), app.px(760)};
        AdjustWindowRectExForDpi(&rect, style, FALSE, 0, GetDpiForWindow(window));
        MONITORINFO monitorInfo{sizeof(monitorInfo)};
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitorInfo);
        SetWindowPos(window, nullptr, 0, 0,
                     std::min(rect.right - rect.left, monitorInfo.rcWork.right - monitorInfo.rcWork.left),
                     std::min(rect.bottom - rect.top, monitorInfo.rcWork.bottom - monitorInfo.rcWork.top),
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
