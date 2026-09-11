"""Owned-window GUI integration probe. Uses an isolated settings directory."""
import ctypes as C
from ctypes import wintypes as W
import os, subprocess, time, json, argparse
import winreg
from contextlib import contextmanager
from pathlib import Path
from PIL import Image

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--exe',type=Path,default=root/'build/Release/ReplayCapture.exe')
parser.add_argument('--run-name',default='ui')
args=parser.parse_args()
if not args.run_name.replace('-','').isalnum():raise SystemExit('Invalid run name')
out=root/'artifacts'/args.run_name;out.mkdir(parents=True,exist_ok=True)
u=C.WinDLL('user32',use_last_error=True);g=C.WinDLL('gdi32',use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes=[C.c_void_p];u.SetProcessDpiAwarenessContext(C.c_void_p(-4))
u.FindWindowW.argtypes=[W.LPCWSTR,W.LPCWSTR];u.FindWindowW.restype=W.HWND
u.GetDlgItem.argtypes=[W.HWND,C.c_int];u.GetDlgItem.restype=W.HWND
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=W.LPARAM
u.GetWindowTextW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
u.GetLastActivePopup.argtypes=[W.HWND];u.GetLastActivePopup.restype=W.HWND
u.IsWindowVisible.argtypes=[W.HWND];u.IsWindow.argtypes=[W.HWND]
u.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
u.RegisterHotKey.argtypes=[W.HWND,C.c_int,W.UINT,W.UINT]
u.UnregisterHotKey.argtypes=[W.HWND,C.c_int]
u.GetDC.argtypes=[W.HWND];u.GetDC.restype=W.HDC
g.CreateCompatibleDC.argtypes=[W.HDC];g.CreateCompatibleDC.restype=W.HDC
g.CreateCompatibleBitmap.argtypes=[W.HDC,C.c_int,C.c_int];g.CreateCompatibleBitmap.restype=W.HBITMAP
g.SelectObject.argtypes=[W.HDC,W.HGDIOBJ];g.SelectObject.restype=W.HGDIOBJ
g.DeleteObject.argtypes=[W.HGDIOBJ];g.DeleteDC.argtypes=[W.HDC]
u.ReleaseDC.argtypes=[W.HWND,W.HDC]
u.PrintWindow.argtypes=[W.HWND,W.HDC,W.UINT]
g.GetDIBits.argtypes=[W.HDC,W.HBITMAP,W.UINT,W.UINT,C.c_void_p,C.c_void_p,W.UINT]

test_instance = 'probe-' + args.run_name
def find():return u.FindWindowW('ReplayCapture.Window.Test.' + test_instance,None)
def wait(fn,seconds=15):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        x=fn()
        if x:return x
        time.sleep(.1)
    raise AssertionError('Timed out')
def text(h):
    s=C.create_unicode_buffer(8192)
    u.SendMessageW(h,0x000D,len(s),C.addressof(s))
    return s.value
def field(h,i):return u.GetDlgItem(h,i)
def settext(h,i,value):
    s=C.create_unicode_buffer(str(value));u.SendMessageW(field(h,i),0xC,0,C.cast(s,C.c_void_p).value)
def click(h,i):u.SendMessageW(field(h,i),0xF5,0,0)
def click_with_dialog(h,i):
    u.PostMessageW(field(h,i),0xF5,0,0)
    def current_dialog():
        d=u.GetLastActivePopup(h)
        if not d or d==h or not u.IsWindowVisible(d):return None
        name=C.create_unicode_buffer(128);u.GetClassNameW(d,name,128)
        button=field(d,1) or field(d,2)
        return (d,button) if name.value=='#32770' and button and u.IsWindowVisible(button) else None
    dialog,button=wait(current_dialog)
    u.SendMessageW(button,0xF5,0,0)
    wait(lambda:not u.IsWindow(dialog))
    u.SendMessageW(h,0,0,0)
def page(h,index):
    u.SendMessageW(field(h,100),0x1330,index,0)
    time.sleep(.3)
def key_combo(key,ctrl=True,shift=True,alt=False):
    modifiers=([0x11] if ctrl else [])+([0x10] if shift else [])+([0x12] if alt else [])
    for k in modifiers:u.keybd_event(k,0,0,0)
    u.keybd_event(key,0,0,0);u.keybd_event(key,0,2,0)
    for k in reversed(modifiers):u.keybd_event(k,0,2,0)
def capture(h,name):
    r=W.RECT();u.GetWindowRect(h,C.byref(r));w,hgt=r.right-r.left,r.bottom-r.top
    dc=u.GetDC(h);mem=g.CreateCompatibleDC(dc);bmp=g.CreateCompatibleBitmap(dc,w,hgt);old=g.SelectObject(mem,bmp)
    u.PrintWindow(h,mem,2)
    class BI(C.Structure):_fields_=[('size',W.DWORD),('width',W.LONG),('height',W.LONG),('planes',W.WORD),('bits',W.WORD),('compression',W.DWORD),('image',W.DWORD),('x',W.LONG),('y',W.LONG),('used',W.DWORD),('important',W.DWORD)]
    bi=BI(40,w,-hgt,1,32,0,0,0,0,0,0);data=C.create_string_buffer(w*hgt*4)
    g.GetDIBits(mem,bmp,0,hgt,data,C.byref(bi),0)
    Image.frombuffer('RGB',(w,hgt),data,'raw','BGRX',0,1).save(out/name)
    g.SelectObject(mem,old);g.DeleteObject(bmp);g.DeleteDC(mem);u.ReleaseDC(h,dc)

def main():
    if find():raise SystemExit('Close existing ReplayCapture before running the isolated probe.')
    env=os.environ.copy();env['REPLAYCAPTURE_DATA_DIR']=str(out/'settings')
    env['REPLAYCAPTURE_TEST_INSTANCE']=test_instance
    settings_dir=out/'settings';settings_dir.mkdir(exist_ok=True)
    (settings_dir/'settings.json').write_text(json.dumps({'schemaVersion':1,'folder':str(out/'recordings'),'retentionSeconds':10,'saveSeconds':3,'width':1280,'height':720,'notifications':False}),encoding='utf-8')
    p=subprocess.Popen([str(args.exe.resolve())],env=env)
    try:
        h=wait(find);time.sleep(1);capture(h,'dashboard.png')
        page(h,1);settext(h,203,12);click_with_dialog(h,220);capture(h,'settings.png')
        page(h,2);u.SendMessageW(field(h,300),0x401,0x0600|ord('R'),0);click(h,301)
        assert 'Ctrl+Alt+R' in text(field(h,304))
        capture(h,'hotkeys.png');click(h,303);time.sleep(.3);key_combo(ord('R'),shift=False,alt=True)
        wait(lambda:'성공' in text(field(h,304)))
        assert u.RegisterHotKey(None,987,3,ord('T'))
        try:
            u.SendMessageW(field(h,300),0x401,0x0600|ord('T'),0)
            click_with_dialog(h,301)
            assert 'Ctrl+Alt+R' in text(field(h,304)), 'Collision changed active key'
        finally:u.UnregisterHotKey(None,987)
        page(h,0)
        click(h,101);wait(lambda:'녹화 중' in text(field(h,109)),30)
        time.sleep(6);settext(h,107,3);click(h,104);time.sleep(2)
        capture(h,'recording.png')
        u.GetWindowRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
        outer=W.RECT();stop=W.RECT()
        u.GetWindowRect(h,C.byref(outer));u.GetWindowRect(field(h,103),C.byref(stop))
        red,green,blue=Image.open(out/'recording.png').getpixel((stop.left-outer.left+12,(stop.top+stop.bottom)//2-outer.top))
        assert red>green+80 and red>blue+80, 'Recording stop button must be red'
        before=len(list((out/'recordings').glob('*.mp4')))
        u.ShowWindow(h,0);key_combo(ord('R'),shift=False,alt=True)
        wait(lambda:len(list((out/'recordings').glob('*.mp4')))>before)
        u.ShowWindow(h,9);page(h,3);capture(h,'jobs.png');page(h,4);capture(h,'diagnostics.png');page(h,0)
        summary=text(field(h,111));assert '보관된 기록' in summary
        assert not field(h,102) and not field(h,105) and not field(h,556)
        click(h,103);wait(lambda:'중지' in text(field(h,109)))
        result={'gui_start_stop':'PASS','removed_controls':'PASS','gui_save':'PASS','gui_settings_apply':'PASS','hotkey_collision_rollback':'PASS','custom_hotkey_hidden_window':'PASS','hotkey_test_mode':'PASS','summary':summary}
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    finally:
        h=find()
        if h:click(h,106)
        p.wait(timeout=15)
    persisted=json.loads((settings_dir/'settings.json').read_text(encoding='utf-8'))
    assert persisted['hotkeyKey']==ord('R') and persisted['retentionSeconds']==12
    p=subprocess.Popen([str(args.exe.resolve())],env=env)
    try:
        h=wait(find);wait(lambda:'Ctrl+Alt+R' in text(field(h,304)))
        result['restart_settings_and_hotkey']='PASS'
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    finally:
        h=find()
        if h:click(h,106)
        p.wait(timeout=15)
    persisted.update(systemAudio=True,allowVideoOnly=False,audioDevice='ReplayCapture-test-missing-device')
    (settings_dir/'settings.json').write_text(json.dumps(persisted),encoding='utf-8')
    p=subprocess.Popen([str(args.exe.resolve())],env=env)
    try:
        h=wait(find);click(h,101);wait(lambda:'녹화 중' in text(field(h,109)),30)
        wait(lambda:'영상만 기록' in text(field(h,500)))
        time.sleep(4);before=len(list((out/'recordings').glob('*.mp4')))
        settext(h,107,2);click(h,104)
        wait(lambda:len(list((out/'recordings').glob('*.mp4')))>before)
        result['audio_failure_video_fallback']='PASS'
        result['recording_stop_red']='PASS'
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    finally:
        h=find()
        if h:click(h,106)
        p.wait(timeout=15)
    print('GUI probe complete')

@contextmanager
def preserve_auto_run():
    path = r'Software\Microsoft\Windows\CurrentVersion\Run'
    previous = None
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
            previous = winreg.QueryValueEx(key, 'ReplayCapture')
    except FileNotFoundError:
        pass
    try:
        yield
    finally:
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, path) as key:
            if previous is not None:
                winreg.SetValueEx(key, 'ReplayCapture', 0, previous[1], previous[0])
            else:
                try: winreg.DeleteValue(key, 'ReplayCapture')
                except FileNotFoundError: pass

if __name__ == '__main__':
    with preserve_auto_run():
        main()
