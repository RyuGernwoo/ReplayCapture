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
constexpr int DesignWidth = 960, DesignHeight = 650;
constexpr DWORD WindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
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
    NavDiagnostics,
    NavInfo,
    AboutText = 570
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
    std::vector<Control> groups;
    std::vector<HWND> headings;
    std::vector<HANDLE> privateFonts;
    bool advanced = false, loading = false, dirty = false;
    std::map<int, HWND> fields;
    std::set<uint64_t> announced;
    UINT dpi = 96;
    int page = 0, hotkeyId = 1;
    bool keyRegistered = false, keyTest = false, wasLocked = false, closing = false;
    bool isRecording = false;
    int64_t exitDeadline{};
    std::wstring warning;
    std::wstring fontFace;
    HBRUSH background = CreateSolidBrush(ui::background());
    ~App() {
        DeleteObject(font);
        DeleteObject(titleFont);
        DeleteObject(background);
        for (auto handle : privateFonts)
            RemoveFontMemResourceEx(handle);
    }
    int px(int n) const {
        return MulDiv(n, dpi, 96);
    }
    void fitDpi() {
        MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
        dpi =
            std::max(72u, std::min({GetDpiForWindow(window),
                                    UINT((info.rcWork.right - info.rcWork.left - 24) * 96 / DesignWidth),
                                    UINT((info.rcWork.bottom - info.rcWork.top - 56) * 96 / DesignHeight)}));
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
        page = p == 5 ? 8 : p;
        TabCtrl_SetCurSel(tab, p);
        keyTest = false;
        text(TestKey, L"단축키 테스트");
        layout();
    }
    void layout() {
        for (auto const &c : controls) {
            bool visible = c.page < 0 || (c.page == page && !(page == 1 && advanced)) ||
                           (page == 1 && ((c.page == 5 && advanced) || c.page == 6));
            if (c.handle == tab)
                visible = false; // Retained only as the navigation notification bridge.
            if (GetDlgCtrlID(c.handle) >= 1000)
                visible = false; // Validation uses the fixed feedback area.
            MoveWindow(c.handle, px(c.x), px(c.y), px(c.w), px(c.h), FALSE);
            ShowWindow(c.handle, visible ? SW_SHOWNA : SW_HIDE);
        }
        for (int i = 0; i < 3; ++i)
            ListView_SetColumnWidth(get(Jobs), i, px(i == 2 ? 132 : 68));
        ListView_SetColumnWidth(get(Jobs), 3, px(388));
        // A page change must repaint *both* old and new navigation buttons.
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    void loadFonts() {
        for (int id : {IDR_FONT_REGULAR, IDR_FONT_SEMIBOLD}) {
            auto module = GetModuleHandleW(nullptr);
            auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
            DWORD count = 0;
            if (resource) {
                auto memory = LoadResource(module, resource);
                auto handle = AddFontMemResourceEx(LockResource(memory), SizeofResource(module, resource),
                                                   nullptr, &count);
                if (handle)
                    privateFonts.push_back(handle);
            }
        }
    }
    void fonts() {
        if (font)
            DeleteObject(font);
        if (titleFont)
            DeleteObject(titleFont);
        auto face = privateFonts.size() == 2 ? L"Pretendard" : L"맑은 고딕";
        font = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, face);
        titleFont = CreateFontW(-px(22), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                CLEARTYPE_QUALITY, 0, face);
        auto dc = GetDC(window);
        auto previous = SelectObject(dc, font);
        wchar_t resolved[LF_FACESIZE]{};
        GetTextFaceW(dc, LF_FACESIZE, resolved);
        fontFace = resolved;
        SelectObject(dc, previous);
        ReleaseDC(window, dc);
        for (auto const &c : controls)
            SendMessageW(c.handle, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for (auto h : headings)
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
    }
    void group(std::wstring const &t, int p, int x, int y, int w, int h) {
        groups.push_back({nullptr, p, x, y + 10, w, h - 10});
        label(t, p, x + 12, y, int(t.size()) * 14 + 12, 22);
    }
    void paintGroups(HDC dc) {
        auto pen = CreatePen(PS_SOLID, 1, ui::highContrast() ? ui::foreground() : RGB(218, 223, 230));
        auto oldPen = SelectObject(dc, pen);
        auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        for (auto const &g : groups)
            if ((g.page == page && !(page == 1 && advanced)) || (page == 1 && advanced && g.page == 5))
                Rectangle(dc, px(g.x), px(g.y), px(g.x + g.w), px(g.y + g.h));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    void fixedLabel(std::wstring const &t, int id, int p, int x, int y, int w, int h = 22,
                    bool heading = false) {
        auto handle = control(L"STATIC", t, id, p, x, y, w, h);
        if (heading) {
            headings.push_back(handle);
            SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
        }
    }
    void fixedField(std::wstring const &t, int id, int p, int x, int y, int labelWidth = 180,
                    int inputWidth = 88) {
        label(t, p, x, y + 3, labelWidth - 8, 22);
        edit(id, p, x + labelWidth, y, inputWidth);
        // Preserve a distinct accessible error label per field; visible error text is at the fixed footer.
        control(L"STATIC", L"", id + 1000, p, 0, 0, 0, 0);
    }
    void fixedCheck(std::wstring const &t, int id, int p, int x, int y, int w = 330) {
        control(L"BUTTON", t, id, p, x, y, w, 24, WS_TABSTOP | BS_AUTOCHECKBOX);
    }
    void create() {
        fitDpi();
        loadFonts();
        fonts();
        fixedLabel(L"ReplayCapture", 0, -1, 24, 20, 400, 34, true);
        control(L"STATIC", L"", 0, -1, 20, 66, 916, 2, SS_ETCHEDHORZ);
        control(L"STATIC", L"", 0, -1, 188, 82, 2, 548, SS_ETCHEDVERT);
        tab = control(WC_TABCONTROLW, L"", Tab, -2, 0, 0, 0, 0);
        wchar_t const *names[] = {L"녹화", L"설정", L"단축키", L"저장 작업", L"도움말·진단", L"정보"};
        for (int i = 0; i < 6; ++i) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<LPWSTR>(names[i]);
            TabCtrl_InsertItem(tab, i, &item);
            button(names[i], NavRecord + i, -1, 20, 94 + i * 46, 150, 34);
        }
        button(L"종료", Exit, -1, 20, 594, 150, 30);

        group(L"녹화 상태", 0, 208, 84, 728, 144);
        fixedLabel(L"중지", StatusText, 0, 226, 108, 200, 30, true);
        fixedLabel(L"", Summary, 0, 226, 145, 690, 62);
        control(PROGRESS_CLASSW, L"", Progress, 0, 226, 210, 690, 6);
        SendMessageW(get(Progress), PBM_SETRANGE32, 0, 1000);

        group(L"최근 기록 저장", 0, 208, 240, 728, 174);
        label(L"저장 길이", 0, 226, 271, 105);
        edit(ThisSeconds, 0, 336, 266, 76);
        label(L"초", 0, 420, 271, 22);
        button(L"기본 시간으로", ResetSeconds, 0, 456, 266, 112, 28);
        button(L"최근 60초 저장", Save, 0, 226, 312, 190, 34);
        fixedLabel(L"", SaveHint, 0, 226, 365, 530, 38);
        button(L"저장 폴더 열기", DashboardFolder, 0, 784, 363, 132, 28);

        group(L"녹화 제어", 0, 208, 426, 728, 96);
        button(L"녹화 시작", Start, 0, 226, 450, 110, 30);
        button(L"녹화 중지", Stop, 0, 346, 450, 110, 30);
        label(L"녹화를 중지하면 아직 저장하지 않은 기록이 지워집니다.", 0, 226, 491, 680, 22);
        group(L"저장 위치와 최근 작업", 0, 208, 534, 728, 96);
        fixedLabel(L"", PathHint, 0, 226, 556, 684, 32);
        fixedLabel(L"아직 저장 작업이 없습니다.", RecentJob, 0, 226, 599, 684, 22);

        // Basic and advanced settings occupy the same fixed content area.
        group(L"화면과 소리", 1, 208, 84, 728, 150);
        label(L"모니터", 1, 226, 116, 86);
        control(WC_COMBOBOXW, L"", MonitorChoice, 1, 318, 110, 458, 230,
                WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
        button(L"새로고침", RefreshDevices, 1, 794, 110, 122, 28);
        label(L"소리 출력 장치", 1, 226, 156, 90);
        control(WC_COMBOBOXW, L"", AudioChoice, 1, 318, 150, 598, 230,
                WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
        fixedCheck(L"시스템 소리 포함", SystemAudio, 1, 226, 194, 200);

        group(L"기록과 저장", 1, 208, 246, 728, 182);
        fixedField(L"보관 시간 (5~600초)", Retention, 1, 226, 273, 170, 82);
        fixedField(L"기본 저장 길이 (초)", DefaultSeconds, 1, 568, 273, 178, 82);
        label(L"보관 시간은 최대 기록 길이, 기본 저장은 단축키와 트레이의 저장 길이입니다.", 1, 226, 313, 684,
              22);
        fixedCheck(L"전체 길이가 쌓인 경우만 저장", FullOnly, 1, 226, 341, 600);
        label(L"저장 폴더", 1, 226, 387, 82);
        control(L"EDIT", L"", Folder, 1, 312, 380, 394, 28, WS_TABSTOP | ES_AUTOHSCROLL);
        control(L"STATIC", L"", Folder + 1000, 1, 0, 0, 0, 0);
        button(L"찾아보기", Browse, 1, 718, 380, 92, 28);
        button(L"열기", OpenFolder, 1, 822, 380, 94, 28);
        group(L"일반", 1, 208, 440, 728, 74);
        fixedCheck(L"저장 완료·오류 알림", Notify, 1, 226, 469, 246);
        fixedCheck(L"Windows 로그인 시 앱 자동 실행", AutoStart, 1, 490, 469, 426);

        group(L"영상 품질", 5, 208, 84, 728, 180);
        fixedField(L"출력 폭 (짝수)", Width, 5, 226, 118, 164);
        fixedField(L"출력 높이 (짝수)", Height, 5, 568, 118, 176);
        fixedField(L"FPS (10~60)", Fps, 5, 226, 166, 164);
        fixedField(L"영상 Mbps (1~50)", Bitrate, 5, 568, 166, 176);
        label(L"SDR · H.264 영상 / AAC 오디오\n앞쪽 키프레임부터 저장하므로 실제 파일 길이는 요청보다 조금 "
              L"길 수 있습니다.",
              5, 226, 215, 684, 40);
        group(L"메모리와 저장 작업", 5, 208, 276, 728, 238);
        fixedField(L"최근 기록 메모리 (MiB)", BufferLimit, 5, 226, 312, 250, 100);
        fixedField(L"녹화·저장 총 예산 (MiB)", TotalLimit, 5, 226, 356, 250, 100);
        fixedField(L"저장 대기 수 (1~10)", QueueLimit, 5, 226, 400, 250, 100);
        label(L"총 예산은 최근 기록 한도보다 128 MiB 이상 커야 합니다.\n저장 참조와 예약량을 포함하며 전체 "
              L"프로세스 메모리 상한은 아닙니다.",
              5, 226, 453, 684, 42);

        fixedLabel(L"설정이 적용되어 있습니다.", SettingsHint, 6, 216, 526, 712, 40);
        label(L"설정 적용 시 미저장 기록이 비워집니다. 진행 중인 저장 작업은 유지됩니다.", 6, 216, 570, 712,
              22);
        button(L"설정 적용", Apply, 6, 216, 602, 106, 28);
        button(L"변경 되돌리기", Revert, 6, 334, 602, 116, 28);
        button(L"기본값 불러오기", Defaults, 6, 462, 602, 126, 28);
        button(L"고급 설정", Advanced, 6, 790, 602, 136, 28);

        group(L"최근 기록 저장 단축키", 2, 208, 84, 728, 212);
        label(L"입력칸에서 Ctrl / Alt / Shift + 키 조합을 누르세요.", 2, 226, 116, 684, 22);
        control(HOTKEY_CLASSW, L"", KeyField, 2, 226, 155, 280, 30, WS_TABSTOP);
        button(L"단축키 적용", ApplyKey, 2, 522, 155, 116, 30);
        button(L"기본 키 불러오기", ResetKey, 2, 650, 155, 144, 30);
        button(L"단축키 테스트", TestKey, 2, 226, 210, 136, 30);
        label(L"테스트 화면에서는 영상이 저장되지 않습니다.", 2, 378, 216, 524, 22);
        group(L"등록 상태", 2, 208, 308, 728, 152);
        fixedLabel(L"", KeyStatus, 2, 226, 341, 684, 104);
        group(L"사용 안내", 2, 208, 472, 728, 158);
        label(L"녹화 화면으로 돌아가거나 창을 숨기면 기본 저장 길이로 저장합니다.\n다른 앱이 사용하는 "
              L"조합이면 기존 단축키를 유지합니다.\n기본 조합: Ctrl + Shift + F9",
              2, 226, 504, 684, 100);

        group(L"이번 실행의 저장 작업", 3, 208, 84, 728, 368);
        control(WC_LISTVIEWW, L"", Jobs, 3, 222, 109, 700, 282,
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
        button(L"영상 열기", OpenVideo, 3, 226, 410, 106, 28);
        button(L"폴더에서 보기", RevealVideo, 3, 344, 410, 126, 28);
        button(L"작업 취소", CancelJob, 3, 806, 410, 110, 28);
        group(L"선택 작업 상세", 3, 208, 464, 728, 166);
        fixedLabel(L"아직 저장 작업이 없습니다. 녹화 화면에서 최근 기록을 저장하세요.", JobDetail, 3, 226,
                   491, 684, 122);

        group(L"문제 해결", 4, 208, 84, 728, 112);
        label(L"소리가 없으면 설정에서 실제 재생 중인 출력 장치를 확인하세요.\n저장이 실패하면 저장 폴더의 "
              L"쓰기 권한과 남은 공간을 확인하세요.",
              4, 226, 115, 684, 60);
        group(L"진단 정보", 4, 208, 208, 728, 338);
        control(L"EDIT", L"", Diagnostics, 4, 222, 232, 700, 256,
                WS_TABSTOP | ES_MULTILINE | ES_READONLY | WS_VSCROLL);
        button(L"진단 복사", CopyDiagnostics, 4, 226, 504, 108, 28);
        button(L"진단 파일 저장", ExportDiagnostics, 4, 346, 504, 132, 28);
        group(L"ReplayCapture 0.1.0", 8, 208, 84, 728, 160);
        label(L"최근 화면과 시스템 소리를 MP4로 저장하는 Windows 프로그램\n\n"
              L"개발자: RyuGernwoo    |    qesadgun@gmail.com\n"
              L"GitHub: https://github.com/RyuGernwoo\n"
              L"Copyright © 2026 RyuGernwoo. 개인·비영리 목적 무료 사용.",
              8, 226, 112, 690, 122);
        group(L"소프트웨어 사용권 계약 및 안내", 8, 208, 260, 728, 370);
        control(
            L"EDIT",
            L"사용권 계약 (2026-09-11)\r\n\r\n"
            L"1. 사용 허락\r\n개인·비영리 목적으로 이 프로그램을 무료로 설치하고 사용할 수 있습니다. 상업적 "
            L"이용은 개발자의 사전 허락이 필요합니다.\r\n\r\n"
            L"2. 재배포\r\n변경하지 않은 공식 배포본은 이 안내와 포함된 라이선스를 유지하여 무료로 재배포할 "
            L"수 있습니다. 유료 판매는 허용하지 않습니다.\r\n\r\n"
            L"3. 권리와 책임\r\n프로그램의 저작권은 개발자에게 있습니다. 화면과 소리를 기록할 권한을 "
            L"확인하고 타인의 개인정보와 저작권을 존중해야 합니다.\r\n\r\n"
            L"4. 보증\r\n프로그램은 현 상태로 제공됩니다. 법률이 허용하는 범위에서 특정 목적 적합성이나 "
            L"기록의 완전성을 보증하지 않으며, 사용으로 발생한 손해에 대한 책임을 제한합니다.\r\n\r\n"
            L"5. 소스 코드 및 제3자 구성요소\r\n이 계약은 배포 프로그램의 사용과 재배포에 적용됩니다. 소스 "
            L"코드 수정·재배포 권한은 별도 허락을 확인하세요. Pretendard 글꼴에는 SIL Open Font License "
            L"1.1이 별도로 적용됩니다. 전문은 assets/fonts/OFL.txt에 포함되어 있습니다.\r\n\r\n"
            L"6. 개인정보 및 문의\r\n화면·소리는 로컬에서 처리하고 영상은 지정 폴더에 저장합니다. 진단 "
            L"내용을 공유할 때는 개인정보를 확인하세요. 문의: qesadgun@gmail.com\r\n\r\n"
            L"사용권 전문: SOFTWARE_LICENSE.ko.md\r\n프로젝트: https://github.com/RyuGernwoo/ReplayCapture",
            AboutText, 8, 226, 288, 690, 318, WS_TABSTOP | ES_MULTILINE | ES_READONLY | WS_VSCROLL);

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
        text(SettingsHint, message);
        if (id >= Width && id <= QueueLimit) {
            advanced = true;
            text(Advanced, L"기본 설정");
        } else {
            advanced = false;
            text(Advanced, L"고급 설정");
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
        s.allowVideoOnly = true;
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
            InvalidateRect(get(Stop), nullptr, TRUE);
        }
        EnableWindow(get(Start), !running && st.state != L"시작 중");
        EnableWindow(get(Stop), running || paused || st.state == L"오류");
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
        text(SaveHint, !st.error.empty() ? L"도움말·진단에서 녹화 상태를 확인하세요."
                       : (seconds < 1 || seconds > settings.retention) ? saveView.explanation
                                                                       : L"");
        text(PathHint, L"저장 위치: " + settings.folder);
        std::wostringstream diag;
        diag << L"ReplayCapture 0.1.0\r\n글꼴: " << fontFace << L"\r\n상태: " << st.state << L"\r\n인코더: "
             << st.encoder << L"\r\n영상: " << settings.width << L"×" << settings.height << L" @ "
             << settings.fps << L"fps\r\n입력 프레임: " << st.frames << L" / 누락: " << st.drops
             << L"\r\n압축 메모리: " << st.bytes << L" bytes\r\n저장 참조: " << st.pinned
             << L" bytes\r\n실제 보관: " << st.available << L"초\r\n오디오: " << st.audio << L"\r\n오류: "
             << st.error << L"\r\n단축키: " << (keyRegistered ? L"등록됨" : L"등록 실패")
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
            if (id >= NavRecord && id <= NavInfo) {
                selectPage(id - NavRecord);
                return;
            }
            switch (id) {
            case Advanced:
                advanced = !advanced;
                text(Advanced, advanced ? L"기본 설정" : L"고급 설정");
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
            case DashboardFolder:
                std::filesystem::create_directories(settings.folder);
                ShellExecuteW(window, L"open", settings.folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case Start:
                engine.start(settings);
                break;
            case Stop:
                engine.stop();
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
        AppendMenuW(menu, MF_STRING, Stop, L"녹화 중지");
        AppendMenuW(menu, MF_STRING, 602, L"설정");
        AppendMenuW(menu, MF_STRING, Exit, L"종료");
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
            if (app->fields.contains(Exit) && app->fields.contains(Jobs))
                app->layout();
            return 0;
        case WM_NEXTDLGCTL: {
            HWND next = lp ? reinterpret_cast<HWND>(wp) : GetNextDlgTabItem(window, GetFocus(), wp != 0);
            if (next)
                SetFocus(next);
            return 0;
        }
        case WM_VSCROLL:
        case WM_MOUSEWHEEL:
            return 0; // The fixed main window never scrolls; child lists keep their own scrollbars.
        case WM_NOTIFY: {
            auto n = reinterpret_cast<NMHDR *>(lp);
            if (n->code == NM_CUSTOMDRAW) {
                bool navigation = n->idFrom >= NavRecord && n->idFrom <= NavInfo;
                bool danger = n->idFrom == Stop && app->isRecording;
                bool primary = n->idFrom == Save || (n->idFrom == Start && !app->isRecording) || danger;
                auto d = reinterpret_cast<NMCUSTOMDRAW *>(lp);
                if ((navigation || primary) && d->dwDrawStage == CDDS_PREPAINT && !ui::highContrast()) {
                    bool enabled = IsWindowEnabled(n->hwndFrom);
                    bool selected =
                        navigation ? int(n->idFrom) - NavRecord == (app->page == 8 ? 5 : app->page) : enabled;
                    COLORREF fill = selected ? ui::accent() : RGB(239, 242, 247);
                    if (enabled && (d->uItemState & CDIS_SELECTED))
                        fill = selected ? RGB(28, 65, 162) : RGB(215, 224, 239);
                    else if (enabled && (d->uItemState & CDIS_HOT))
                        fill = selected ? RGB(30, 75, 190) : RGB(226, 233, 245);
                    if (danger)
                        fill = (d->uItemState & CDIS_SELECTED) ? RGB(153, 27, 27)
                               : (d->uItemState & CDIS_HOT)    ? RGB(185, 28, 28)
                                                               : RGB(220, 38, 38);
                    auto brush = CreateSolidBrush(fill);
                    FillRect(d->hdc, &d->rc, brush);
                    DeleteObject(brush);
                    SetBkMode(d->hdc, TRANSPARENT);
                    SetTextColor(d->hdc, !enabled ? GetSysColor(COLOR_GRAYTEXT)
                                                  : (selected ? ui::accentText() : ui::foreground()));
                    auto oldFont = SelectObject(d->hdc, app->font);
                    auto value = app->text(int(n->idFrom));
                    RECT r = d->rc;
                    InflateRect(&r, -6, -2);
                    DrawTextW(d->hdc, value.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(d->hdc, oldFont);
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
            app->balloon(L"트레이에서 계속 실행합니다. 종료하려면 메뉴의 종료를 선택하십시오.");
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
            RECT size{0, 0, app->px(DesignWidth), app->px(DesignHeight)};
            AdjustWindowRectExForDpi(&size, WindowStyle, FALSE, 0, GetDpiForWindow(window));
            SetWindowPos(window, nullptr, r->left, r->top, size.right - size.left, size.bottom - size.top,
                         SWP_NOZORDER);
            app->layout();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto limits = reinterpret_cast<MINMAXINFO *>(lp);
            RECT size{0, 0, app->px(DesignWidth), app->px(DesignHeight)};
            AdjustWindowRectExForDpi(&size, WindowStyle, FALSE, 0, GetDpiForWindow(window));
            limits->ptMinTrackSize = {size.right - size.left, size.bottom - size.top};
            limits->ptMaxTrackSize = limits->ptMinTrackSize;
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_MAXIMIZE || (wp & 0xFFF0) == SC_SIZE)
                return 0;
            break;
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
            app->paintGroups(reinterpret_cast<HDC>(wp));
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            auto dc = BeginPaint(window, &ps);
            app->paintGroups(dc);
            EndPaint(window, &ps);
            return 0;
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
        std::wstring windowClass = L"ReplayCapture.Window", mutexName = L"Local\\ReplayCapture.Application";
        wchar_t testInstance[65]{};
        auto count = GetEnvironmentVariableW(L"REPLAYCAPTURE_TEST_INSTANCE", testInstance, 65);
        if (count > 0 && count < 65) {
            std::wstring suffix(testInstance);
            if (suffix.find_first_not_of(
                    L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
                std::wstring::npos)
                throw std::runtime_error("Invalid test instance identifier");
            windowClass += L".Test." + suffix;
            mutexName += L".Test." + suffix;
        }
        HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            auto existing = FindWindowW(windowClass.c_str(), nullptr);
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
        c.lpszClassName = windowClass.c_str();
        c.lpfnWndProc = windowProc;
        c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        c.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_REPLAYCAPTURE));
        c.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_REPLAYCAPTURE), IMAGE_ICON,
                                                  GetSystemMetrics(SM_CXSMICON),
                                                  GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        RegisterClassExW(&c);
        UINT dpi = GetDpiForSystem();
        RECT rect{0, 0, MulDiv(DesignWidth, dpi, 96), MulDiv(DesignHeight, dpi, 96)};
        DWORD style = WindowStyle;
        AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
        HWND window =
            CreateWindowExW(0, c.lpszClassName, L"ReplayCapture", style, CW_USEDEFAULT, CW_USEDEFAULT,
                            rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, &app);
        if (!window)
            check(HRESULT_FROM_WIN32(GetLastError()));
        rect = {0, 0, app.px(DesignWidth), app.px(DesignHeight)};
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
