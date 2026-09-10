"""Isolated, repeatable GUI performance run; no user settings are modified."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import statistics
import subprocess
import time
import psutil

parser = argparse.ArgumentParser()
parser.add_argument('--exe', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--seconds', type=int, default=90)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
u = C.WinDLL('user32', use_last_error=True)
u.FindWindowW.argtypes = [W.LPCWSTR, W.LPCWSTR]
u.FindWindowW.restype = W.HWND
u.GetDlgItem.argtypes = [W.HWND, C.c_int]
u.GetDlgItem.restype = W.HWND
u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.SendMessageW.restype = W.LPARAM
u.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
u.GetGuiResources.argtypes = [W.HANDLE, W.DWORD]
u.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
k = C.WinDLL('kernel32')
k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
k.OpenProcess.restype = W.HANDLE
k.CloseHandle.argtypes = [W.HANDLE]
if u.FindWindowW('ReplayCapture.Window', None):
    raise SystemExit('An existing ReplayCapture must be closed first.')
settings = args.output.resolve() / 'settings'
settings.mkdir(exist_ok=True)
(settings/'settings.json').write_text(json.dumps(dict(schemaVersion=1,
    folder=str(args.output.resolve()/'recordings'), retentionSeconds=60,
    saveSeconds=10, width=1920, height=1080, fps=30, notifications=False)), encoding='utf-8')
env = dict(os.environ, REPLAYCAPTURE_DATA_DIR=str(settings))
p = subprocess.Popen([str(args.exe.resolve())], env=env)
proc = psutil.Process(p.pid)
handle = k.OpenProcess(0x400, False, p.pid)
samples, latency = [], []
h = None
def text(i):
    b = C.create_unicode_buffer(8192)
    u.SendMessageW(u.GetDlgItem(h, i), 0x000D, len(b), C.addressof(b))
    return b.value
try:
    for _ in range(100):
        h = u.FindWindowW('ReplayCapture.Window', None)
        if h: break
        time.sleep(.1)
    assert h, 'GUI did not start'
    time.sleep(1)
    u.SendMessageW(u.GetDlgItem(h, 101), 0xF5, 0, 0)
    proc.cpu_percent()
    for i in range(args.seconds):
        time.sleep(1)
        start = time.perf_counter()
        u.SendMessageW(h, 0, 0, 0)
        latency.append((time.perf_counter()-start)*1000)
        samples.append(dict(second=i+1, cpu_one_core_percent=proc.cpu_percent(),
            rss=proc.memory_info().rss, private=proc.memory_info().private,
            handles=proc.num_handles(), gdi=u.GetGuiResources(handle, 0), user=u.GetGuiResources(handle, 1)))
        if i > 10 and i % 10 == 0:
            u.SendMessageW(u.GetDlgItem(h, 100), 0x1330, (i//10) % 5, 0)
            u.SetWindowPos(h, None, 0, 0, 920 + (i%20)*12, 720, 0x16)
        if i == args.seconds - 15:
            u.SendMessageW(u.GetDlgItem(h, 100), 0x1330, 0, 0)
            for _ in range(3): u.SendMessageW(u.GetDlgItem(h, 104), 0xF5, 0, 0)
    report = dict(duration_seconds=args.seconds, logical_cpus=psutil.cpu_count(),
        cpu_mean_one_core_percent=statistics.mean(x['cpu_one_core_percent'] for x in samples),
        cpu_mean_machine_percent=statistics.mean(x['cpu_one_core_percent'] for x in samples)/psutil.cpu_count(),
        peak_rss_bytes=max(x['rss'] for x in samples),
        peak_private_bytes=max(x['private'] for x in samples),
        latency_p95_ms=sorted(latency)[int(len(latency)*.95)-1], latency_max_ms=max(latency),
        diagnostics=text(500), samples=samples)
    assert '녹화 중' in text(109), text(109)
    assert len(list((args.output/'recordings').glob('*.mp4'))) == 3, 'Exports missing'
    (args.output/'performance.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({a:b for a,b in report.items() if a not in ('samples','diagnostics')}, indent=2))
finally:
    if h: u.SendMessageW(u.GetDlgItem(h, 106), 0xF5, 0, 0)
    try: p.wait(timeout=20)
    except subprocess.TimeoutExpired: p.terminate(); p.wait()
    k.CloseHandle(handle)
