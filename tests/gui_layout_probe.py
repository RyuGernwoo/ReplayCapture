"""Geometry, validation and keyboard focus checks in an isolated owned window.

DPI messages exercise layout calculations; they do not emulate real monitors.
"""
import ui_probe as p
from ui_probe import C, W, u, field, text, click, page, settext, capture, wait

u.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
u.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
u.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
u.MapWindowPoints.argtypes = [W.HWND, W.HWND, C.c_void_p, W.UINT]
u.IsWindowEnabled.argtypes = [W.HWND]
u.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
class GTI(C.Structure):
    _fields_ = [('cbSize', W.DWORD), ('flags', W.DWORD), ('active', W.HWND),
                ('focus', W.HWND), ('capture', W.HWND), ('menu', W.HWND),
                ('move', W.HWND), ('caret', W.HWND), ('rect', W.RECT)]
u.GetGUIThreadInfo.argtypes = [W.DWORD, C.POINTER(GTI)]
if p.find(): raise SystemExit('Close existing ReplayCapture first.')
env = dict(p.os.environ, REPLAYCAPTURE_DATA_DIR=str(p.out/'settings'))
(p.out/'settings').mkdir(exist_ok=True)
(p.out/'settings/settings.json').write_text(p.json.dumps(dict(schemaVersion=1,
    folder=r'C:\Users\Public\Videos\ReplayCapture', notifications=False)), encoding='utf-8')
proc = p.subprocess.Popen([str(p.args.exe)], env=env)
results = []
try:
    h=wait(p.find); p.time.sleep(1)
    capture(h,'dashboard-wide.png')
    page(h,1)
    capture(h,'settings-basic.png')
    assert not u.IsWindowVisible(field(h,208)), 'Advanced field must start collapsed'
    click(h,555)
    assert u.IsWindowVisible(field(h,208)), 'Advanced field not expanded'
    settext(h,208,321); click(h,220)
    assert '짝수' in text(field(h,1208)), 'Inline width validation missing'
    click(h,554)
    assert text(field(h,208))=='1920', 'Revert did not restore applied value: ' + text(field(h,208))
    settext(h,203,120); page(h,0); page(h,1)
    assert text(field(h,203))=='120', 'Draft lost during navigation'
    click(h,554)
    results.append('advanced, inline validation, revert, draft preservation')
    for dpi in (96,120,144,168,192):
        rect=W.RECT(20,20,1020,740)
        u.SendMessageW(h,0x02E0,(dpi<<16)|dpi,C.addressof(rect))
        page(h,0)
        client=W.RECT();u.GetClientRect(h,C.byref(client))
        for i in (107,104,101,102,103,105,557):
            r=W.RECT();u.GetWindowRect(field(h,i),C.byref(r))
            u.MapWindowPoints(None,h,C.byref(r),2)
            assert 0 <= r.left < r.right <= client.right, (dpi,i,tuple((r.left,r.right,client.right)))
        capture(h,f'dpi-{dpi}.png')
        results.append(f'synthetic DPI {dpi}: horizontal bounds')
    # Tab traversal must scroll focused controls into view, even on a short window.
    page(h,1)
    u.SendMessageW(h,0x0028,field(h,203),1)  # WM_NEXTDLGCTL: focus retention field
    p.time.sleep(.3)
    gti=GTI();gti.cbSize=C.sizeof(gti)
    thread=u.GetWindowThreadProcessId(h,None)
    seen=set()
    for _ in range(55):
        u.SendMessageW(h,0x0028,0,0)
        p.time.sleep(.04)
        assert u.GetGUIThreadInfo(thread,C.byref(gti))
        if gti.focus:
            seen.add(gti.focus)
            r=W.RECT();u.GetWindowRect(gti.focus,C.byref(r))
            u.MapWindowPoints(None,h,C.byref(r),2)
            client=W.RECT();u.GetClientRect(h,C.byref(client))
            assert r.top>=0 and r.bottom<=client.bottom, ('Focus outside viewport',r.top,r.bottom,client.bottom)
    assert field(h,220) in seen and field(h,106) in seen, 'Apply/exit not keyboard reachable'
    results.append('dialog keyboard traversal and focus scrolling')
    page(h,0);settext(h,107,0);p.time.sleep(.4)
    assert not u.IsWindowEnabled(field(h,104))
    assert '1초' in text(field(h,550))
    results.append('invalid save length disabled with explanation')
    (p.out/'layout-result.json').write_text(p.json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    print('Layout and state checks passed:',len(results))
finally:
    h=p.find()
    if h: click(h,106)
    proc.wait(timeout=20)
