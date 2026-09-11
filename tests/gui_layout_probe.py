"""Fixed-window geometry, sidebar repaint and embedded font regression checks."""
import ui_probe as p
from ui_probe import C, W, u, field, text, click, page, settext, capture, wait

u.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
u.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
u.MapWindowPoints.argtypes = [W.HWND, W.HWND, C.c_void_p, W.UINT]
u.GetWindowLongW.argtypes = [W.HWND, C.c_int]
u.IsWindowEnabled.argtypes = [W.HWND]

def rect(h, parent=None):
    r=W.RECT(); u.GetWindowRect(h,C.byref(r))
    if parent: u.MapWindowPoints(None,parent,C.byref(r),2)
    return r.left,r.top,r.right,r.bottom

env=dict(p.os.environ, REPLAYCAPTURE_DATA_DIR=str(p.out/'settings'),
         REPLAYCAPTURE_TEST_INSTANCE=p.test_instance)
(p.out/'settings').mkdir(exist_ok=True)
(p.out/'settings/settings.json').write_text(p.json.dumps(dict(schemaVersion=1,
    folder=r'C:\Users\Public\Videos\ReplayCapture', notifications=False)),encoding='utf-8')
if p.find(): raise SystemExit('This test instance is already running')
proc=p.subprocess.Popen([str(p.args.exe.resolve())],env=env)
results=[]
try:
    h=wait(p.find); p.time.sleep(1)
    style=u.GetWindowLongW(h,-16)
    assert not style & (0x40000|0x10000|0x200000|0x100000), hex(style)
    original=rect(h)
    u.SendMessageW(h,0x112,0xF030,0)
    assert rect(h)==original
    results.append('fixed size: resize/maximize/parent scroll styles absent')
    for selected in (0,1,2,3,4,5,0,5,4,2,1,0):
        click(h,560+selected);p.time.sleep(.15)
        client=W.RECT();u.GetClientRect(h,C.byref(client))
        for i in range(101,565):
            c=field(h,i)
            if c and u.IsWindowVisible(c):
                l,t,r,b=rect(c,h)
                assert 0<=l<r<=client.right and 0<=t<b<=client.bottom,(selected,i,(l,t,r,b))
        before=[rect(field(h,560+i)) for i in range(6)]
        for delta in (120,-120,120,-120):
            u.SendMessageW(h,0x20A,(delta & 0xffff)<<16,0)
            u.SendMessageW(h,0x115,3 if delta<0 else 2,0)
        assert before==[rect(field(h,560+i)) for i in range(6)]
        capture(h,'sidebar-check.png')
        image=p.Image.open(p.out/'sidebar-check.png')
        left,top,_,_=rect(h)
        for i in range(6):
            l,t,r,b=rect(field(h,560+i))
            red,green,blue=image.getpixel((l-left+12,(t+b)//2-top))
            assert (blue>red+50 and blue>green+50)==(i==selected),(selected,i,(red,green,blue))
    results.append('12 sidebar transitions: exactly one blue selection; scroll preserves geometry')
    assert text(field(h,106))=='종료'
    for removed in (102,105,216,503,556): assert not field(h,removed),removed
    page(h,5);assert '소스 코드' in text(field(h,570));capture(h,'about.png')
    page(h,0)
    capture(h,'dashboard-wide.png')
    page(h,1);capture(h,'settings-basic.png')
    assert not u.IsWindowVisible(field(h,208))
    click(h,555)
    assert u.IsWindowVisible(field(h,208)) and not u.IsWindowVisible(field(h,203))
    capture(h,'settings-advanced.png')
    settext(h,208,321);click(h,220)
    assert '짝수' in text(field(h,553))
    click(h,554);assert text(field(h,208))=='1920'
    click(h,555);settext(h,203,120);page(h,0);page(h,1)
    assert text(field(h,203))=='120'
    click(h,554)
    results.append('basic/advanced replacement, visible validation, revert, draft preservation')
    page(h,4);p.time.sleep(1.2)
    assert '글꼴: Pretendard' in text(field(h,500)),text(field(h,500))
    capture(h,'diagnostics.png')
    results.append('GDI resolved embedded Pretendard font')
    page(h,0);settext(h,107,0);p.time.sleep(.4)
    assert not u.IsWindowEnabled(field(h,104)) and '1초' in text(field(h,550))
    results.append('invalid save duration disables save and explains error')
    (p.out/'layout-result.json').write_text(p.json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    print('Layout and state checks passed:',len(results))
finally:
    h=p.find()
    if h: click(h,106)
    proc.wait(timeout=20)
