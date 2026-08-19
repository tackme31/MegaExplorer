#!/usr/bin/env python
"""MegaExplorer QML style-tuning helper.

Drives the screenshot -> edit -> build -> screenshot loop from a single CLI so
an agent can run one Bash call per iteration instead of a dozen.  Every
subcommand prints a one-to-few-line summary; raw build logs are never echoed.

Windows only.  Requires Pillow; everything else is stdlib (ctypes/winreg).
"""

from __future__ import annotations

import argparse
import base64
import ctypes
import json
import os
import re
import shlex
import subprocess
import sys
import time
import winreg
from ctypes import wintypes
from pathlib import Path

from PIL import Image, ImageDraw

# --------------------------------------------------------------------------
# Paths / constants
# --------------------------------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parents[4]
BUILD_DIR = REPO_ROOT / "build" / "msvc-debug"
EXE_PATH = BUILD_DIR / "Debug" / "appMegaExplorer.exe"
VCPKG_BIN = BUILD_DIR / "vcpkg_installed" / "x64-windows-mega" / "debug" / "bin"
OUT_DIR = REPO_ROOT / ".screenshots"
SESSION_FILE = OUT_DIR / ".session.json"
REG_BACKUP_FILE = OUT_DIR / ".regbackup.json"

# The `cmake` on PATH is Strawberry Perl's and is wrong for this project.
CMAKE = Path(os.environ.get("UI_SHOT_CMAKE", r"C:/Qt/Tools/CMake_64/bin/cmake.exe"))
QT_BIN = Path(os.environ.get("UI_SHOT_QT_BIN", r"C:/Qt/6.11.1/msvc2022_64/bin"))

# QML `Settings` in Main.qml persists window geometry (and sort/column state)
# here.  The loop resizes the window, so this whole key is snapshotted before a
# run and rewound afterwards -- otherwise style tuning silently rewrites the
# user's real saved layout.
REG_PATH = r"Software\MegaExplorer\MegaExplorer"

PRESET = "msvc-debug"
DEFAULT_TARGET = "appMegaExplorer"

# --------------------------------------------------------------------------
# Win32 plumbing
# --------------------------------------------------------------------------

if ctypes.sizeof(ctypes.c_void_p) == 8:
    ULONG_PTR = ctypes.c_ulonglong
else:
    ULONG_PTR = ctypes.c_ulong

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
dwmapi = ctypes.WinDLL("dwmapi", use_last_error=True)

WM_CLOSE = 0x0010
SRCCOPY = 0x00CC0020
CAPTUREBLT = 0x40000000
BI_RGB = 0
DIB_RGB_COLORS = 0
PW_CLIENTONLY = 0x01
PW_RENDERFULLCONTENT = 0x02
DWMWA_EXTENDED_FRAME_BOUNDS = 9
SWP_NOZORDER = 0x0004
SWP_NOMOVE = 0x0002
SWP_NOSIZE = 0x0001
SWP_NOACTIVATE = 0x0010
SW_RESTORE = 9
INPUT_MOUSE = 0
INPUT_KEYBOARD = 1
KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_UNICODE = 0x0004
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
MOUSEEVENTF_RIGHTDOWN = 0x0008
MOUSEEVENTF_RIGHTUP = 0x0010
MOUSEEVENTF_MIDDLEDOWN = 0x0020
MOUSEEVENTF_MIDDLEUP = 0x0040
MOUSEEVENTF_WHEEL = 0x0800
PROCESS_SYNCHRONIZE = 0x00100000
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wintypes.DWORD * 3)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [
        ("dx", wintypes.LONG),
        ("dy", wintypes.LONG),
        ("mouseData", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ULONG_PTR),
    ]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk", wintypes.WORD),
        ("wScan", wintypes.WORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ULONG_PTR),
    ]


class _INPUTUNION(ctypes.Union):
    _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT)]


class INPUT(ctypes.Structure):
    _anonymous_ = ("u",)
    _fields_ = [("type", wintypes.DWORD), ("u", _INPUTUNION)]


WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

user32.EnumWindows.argtypes = [WNDENUMPROC, wintypes.LPARAM]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.IsWindow.argtypes = [wintypes.HWND]
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
user32.GetDC.argtypes = [wintypes.HWND]
user32.GetDC.restype = wintypes.HDC
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
user32.SetWindowPos.argtypes = [
    wintypes.HWND,
    wintypes.HWND,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    wintypes.UINT,
]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.SetForegroundWindow.argtypes = [wintypes.HWND]
user32.GetForegroundWindow.restype = wintypes.HWND
user32.SendInput.argtypes = [wintypes.UINT, ctypes.POINTER(INPUT), ctypes.c_int]
user32.VkKeyScanW.argtypes = [wintypes.WCHAR]

kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
kernel32.QueryFullProcessImageNameW.argtypes = [
    wintypes.HANDLE,
    wintypes.DWORD,
    wintypes.LPWSTR,
    ctypes.POINTER(wintypes.DWORD),
]

gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi32.CreateCompatibleDC.restype = wintypes.HDC
gdi32.CreateDIBSection.argtypes = [
    wintypes.HDC,
    ctypes.POINTER(BITMAPINFO),
    wintypes.UINT,
    ctypes.POINTER(ctypes.c_void_p),
    wintypes.HANDLE,
    wintypes.DWORD,
]
gdi32.CreateDIBSection.restype = wintypes.HBITMAP
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.SelectObject.restype = wintypes.HGDIOBJ
gdi32.BitBlt.argtypes = [
    wintypes.HDC,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    wintypes.HDC,
    ctypes.c_int,
    ctypes.c_int,
    wintypes.DWORD,
]
gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
gdi32.DeleteDC.argtypes = [wintypes.HDC]


def _make_dpi_aware() -> None:
    """Physical-pixel coordinates everywhere; without this Windows lies about
    window rects and cursor positions on a scaled display."""
    try:
        # DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
        return
    except Exception:
        pass
    try:
        ctypes.WinDLL("shcore").SetProcessDpiAwareness(2)
    except Exception:
        try:
            user32.SetProcessDPIAware()
        except Exception:
            pass


# --------------------------------------------------------------------------
# Session state
# --------------------------------------------------------------------------


def load_session() -> dict:
    if SESSION_FILE.exists():
        try:
            return json.loads(SESSION_FILE.read_text(encoding="utf-8"))
        except Exception:
            return {}
    return {}


def save_session(data: dict) -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    SESSION_FILE.write_text(json.dumps(data, indent=1), encoding="utf-8")


# --------------------------------------------------------------------------
# Window lookup
# --------------------------------------------------------------------------


def _window_title(hwnd: int) -> str:
    buf = ctypes.create_unicode_buffer(512)
    user32.GetWindowTextW(hwnd, buf, 512)
    return buf.value


def _window_class(hwnd: int) -> str:
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, 256)
    return buf.value


def _pid_of(hwnd: int) -> int:
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return pid.value


def _image_name_of(pid: int) -> str:
    handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return ""
    buf = ctypes.create_unicode_buffer(32768)
    size = wintypes.DWORD(len(buf))
    ok = kernel32.QueryFullProcessImageNameW(handle, 0, buf, ctypes.byref(size))
    kernel32.CloseHandle(handle)
    return Path(buf.value).name.lower() if ok else ""


def _client_size(hwnd: int) -> tuple[int, int]:
    rect = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    return rect.right - rect.left, rect.bottom - rect.top


def enum_candidate_windows(pid: int | None = None) -> list[int]:
    """Visible top-level windows that look like the app's main window."""
    found: list[int] = []

    def cb(hwnd, _lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        w, h = _client_size(hwnd)
        if w < 100 or h < 100:
            return True
        if pid is not None:
            if _pid_of(hwnd) != pid:
                return True
        else:
            # Match the exe, not the title: Qt Creator with this project open is
            # also a "Qt*" window whose title contains "MegaExplorer".
            if _image_name_of(_pid_of(hwnd)) != EXE_PATH.name.lower():
                return True
        found.append(hwnd)
        return True

    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return found


def popup_windows(main_hwnd: int) -> list[int]:
    """Other visible top-level windows of the same process. A Qt Quick Menu
    defaults to Popup.Window on desktop, so it lives in its own window and
    PrintWindow on the main hwnd returns a shot with the menu missing -- an
    image that is not uniform, so _is_blank cannot catch it."""
    pid = _pid_of(main_hwnd)
    found: list[int] = []

    def cb(hwnd, _lparam):
        if hwnd == main_hwnd or not user32.IsWindowVisible(hwnd):
            return True
        if _pid_of(hwnd) != pid:
            return True
        w, h = _client_size(hwnd)
        if w < 8 or h < 8:
            return True
        found.append(hwnd)
        return True

    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return found


def find_window(required: bool = True) -> int | None:
    """Resolve the app window: remembered hwnd first, then pid, then a scan."""
    session = load_session()

    hwnd = session.get("hwnd")
    if (
        hwnd
        and user32.IsWindow(hwnd)
        and user32.IsWindowVisible(hwnd)
        and _image_name_of(_pid_of(hwnd)) == EXE_PATH.name.lower()
    ):
        return hwnd

    pid = session.get("pid")
    if pid and _process_alive(pid) and _image_name_of(pid) == EXE_PATH.name.lower():
        candidates = enum_candidate_windows(pid)
        if candidates:
            session["hwnd"] = candidates[0]
            save_session(session)
            return candidates[0]

    candidates = enum_candidate_windows(None)
    if candidates:
        hwnd = candidates[0]
        session["hwnd"] = hwnd
        session["pid"] = _pid_of(hwnd)
        save_session(session)
        return hwnd

    if required:
        die("app window not found -- run `launch` first (or `info` to check)")
    return None


def _process_alive(pid: int) -> bool:
    handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return False
    code = wintypes.DWORD()
    ok = kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
    kernel32.CloseHandle(handle)
    return bool(ok) and code.value == 259  # STILL_ACTIVE


def dpi_scale(hwnd: int) -> float:
    try:
        return user32.GetDpiForWindow(hwnd) / 96.0
    except Exception:
        return 1.0


# --------------------------------------------------------------------------
# Capture
# --------------------------------------------------------------------------


def _dib_to_image(bits_ptr, width: int, height: int) -> Image.Image:
    buf = ctypes.string_at(bits_ptr, width * height * 4)
    return Image.frombuffer("RGBA", (width, height), buf, "raw", "BGRA", 0, 1).convert("RGB")


def _is_blank(img: Image.Image) -> bool:
    """PrintWindow against a D3D-composited Qt window sometimes yields a solid
    fill; treat a uniform image as a failed capture and let the caller retry."""
    lo, hi = img.convert("L").resize((64, 64)).getextrema()
    return (hi - lo) < 4


def capture_printwindow(hwnd: int, full_window: bool) -> Image.Image | None:
    if full_window:
        rect = wintypes.RECT()
        if dwmapi.DwmGetWindowAttribute(
            wintypes.HWND(hwnd),
            wintypes.DWORD(DWMWA_EXTENDED_FRAME_BOUNDS),
            ctypes.byref(rect),
            ctypes.sizeof(rect),
        ) != 0:
            user32.GetWindowRect(hwnd, ctypes.byref(rect))
        width, height = rect.right - rect.left, rect.bottom - rect.top
        flags = PW_RENDERFULLCONTENT
    else:
        width, height = _client_size(hwnd)
        flags = PW_CLIENTONLY | PW_RENDERFULLCONTENT

    if width <= 0 or height <= 0:
        return None

    window_dc = user32.GetDC(hwnd)
    mem_dc = gdi32.CreateCompatibleDC(window_dc)
    info = BITMAPINFO()
    info.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    info.bmiHeader.biWidth = width
    info.bmiHeader.biHeight = -height  # top-down
    info.bmiHeader.biPlanes = 1
    info.bmiHeader.biBitCount = 32
    info.bmiHeader.biCompression = BI_RGB
    bits = ctypes.c_void_p()
    bitmap = gdi32.CreateDIBSection(
        mem_dc, ctypes.byref(info), DIB_RGB_COLORS, ctypes.byref(bits), None, 0
    )
    try:
        if not bitmap:
            return None
        old = gdi32.SelectObject(mem_dc, bitmap)
        ok = user32.PrintWindow(hwnd, mem_dc, flags)
        gdi32.SelectObject(mem_dc, old)
        if not ok:
            return None
        return _dib_to_image(bits, width, height)
    finally:
        if bitmap:
            gdi32.DeleteObject(bitmap)
        gdi32.DeleteDC(mem_dc)
        user32.ReleaseDC(hwnd, window_dc)


def capture_screen(hwnd: int, full_window: bool) -> Image.Image | None:
    """Fallback: read the composited desktop over the window's rectangle.
    Needs the window unobstructed, so it is raised first."""
    bring_to_front(hwnd)
    time.sleep(0.25)

    if full_window:
        rect = wintypes.RECT()
        if dwmapi.DwmGetWindowAttribute(
            wintypes.HWND(hwnd),
            wintypes.DWORD(DWMWA_EXTENDED_FRAME_BOUNDS),
            ctypes.byref(rect),
            ctypes.sizeof(rect),
        ) != 0:
            user32.GetWindowRect(hwnd, ctypes.byref(rect))
        left, top = rect.left, rect.top
        width, height = rect.right - rect.left, rect.bottom - rect.top
    else:
        width, height = _client_size(hwnd)
        origin = wintypes.POINT(0, 0)
        user32.ClientToScreen(hwnd, ctypes.byref(origin))
        left, top = origin.x, origin.y

    if width <= 0 or height <= 0:
        return None

    screen_dc = user32.GetDC(None)
    mem_dc = gdi32.CreateCompatibleDC(screen_dc)
    info = BITMAPINFO()
    info.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    info.bmiHeader.biWidth = width
    info.bmiHeader.biHeight = -height
    info.bmiHeader.biPlanes = 1
    info.bmiHeader.biBitCount = 32
    info.bmiHeader.biCompression = BI_RGB
    bits = ctypes.c_void_p()
    bitmap = gdi32.CreateDIBSection(
        mem_dc, ctypes.byref(info), DIB_RGB_COLORS, ctypes.byref(bits), None, 0
    )
    try:
        if not bitmap:
            return None
        old = gdi32.SelectObject(mem_dc, bitmap)
        ok = gdi32.BitBlt(mem_dc, 0, 0, width, height, screen_dc, left, top, SRCCOPY | CAPTUREBLT)
        gdi32.SelectObject(mem_dc, old)
        if not ok:
            return None
        return _dib_to_image(bits, width, height)
    finally:
        if bitmap:
            gdi32.DeleteObject(bitmap)
        gdi32.DeleteDC(mem_dc)
        user32.ReleaseDC(None, screen_dc)


def capture(hwnd: int, method: str, full_window: bool) -> tuple[Image.Image, str]:
    label = "screen"
    if method in ("auto", "print"):
        if method == "auto" and popup_windows(hwnd):
            label = "screen-popup"
        else:
            img = capture_printwindow(hwnd, full_window)
            if img is not None and not _is_blank(img):
                return img, "print"
            if method == "print":
                die("PrintWindow capture failed or came back blank (try --method screen)")
    img = capture_screen(hwnd, full_window)
    if img is None:
        die("screen capture failed")
    return img, label


def bring_to_front(hwnd: int) -> bool:
    """Returns True if the window had to be raised."""
    if user32.GetForegroundWindow() == hwnd:
        return False
    user32.ShowWindow(hwnd, SW_RESTORE)
    user32.SetForegroundWindow(hwnd)
    return True


# --------------------------------------------------------------------------
# Registry snapshot / restore
# --------------------------------------------------------------------------


def _encode_value(value, kind):
    if kind == winreg.REG_BINARY:
        return {"b64": base64.b64encode(bytes(value)).decode("ascii")}
    if isinstance(value, (list, tuple)):
        return list(value)
    return value


def _decode_value(value, kind):
    if kind == winreg.REG_BINARY and isinstance(value, dict):
        return base64.b64decode(value["b64"])
    return value


def _dump_key(root, path: str) -> dict | None:
    try:
        key = winreg.OpenKey(root, path, 0, winreg.KEY_READ)
    except FileNotFoundError:
        return None
    values = []
    subkeys = {}
    with key:
        i = 0
        while True:
            try:
                name, value, kind = winreg.EnumValue(key, i)
            except OSError:
                break
            values.append({"name": name, "type": kind, "value": _encode_value(value, kind)})
            i += 1
        i = 0
        while True:
            try:
                sub = winreg.EnumKey(key, i)
            except OSError:
                break
            dumped = _dump_key(root, path + "\\" + sub)
            if dumped is not None:
                subkeys[sub] = dumped
            i += 1
    return {"values": values, "subkeys": subkeys}


def _restore_key(root, path: str, snapshot: dict) -> None:
    key = winreg.CreateKeyEx(root, path, 0, winreg.KEY_READ | winreg.KEY_SET_VALUE)
    with key:
        wanted = {v["name"] for v in snapshot["values"]}
        current = []
        i = 0
        while True:
            try:
                name, _v, _k = winreg.EnumValue(key, i)
            except OSError:
                break
            current.append(name)
            i += 1
        for name in current:
            if name not in wanted:
                try:
                    winreg.DeleteValue(key, name)
                except OSError:
                    pass
        for entry in snapshot["values"]:
            winreg.SetValueEx(
                key,
                entry["name"],
                0,
                entry["type"],
                _decode_value(entry["value"], entry["type"]),
            )
    for sub, sub_snapshot in snapshot["subkeys"].items():
        _restore_key(root, path + "\\" + sub, sub_snapshot)


def backup_settings() -> bool:
    """Snapshot the app's settings key.  Never overwrites an existing backup --
    a crashed previous run leaves a dirty registry, and re-snapshotting there
    would freeze the polluted values in as the 'original'."""
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    if REG_BACKUP_FILE.exists():
        return False
    snapshot = _dump_key(winreg.HKEY_CURRENT_USER, REG_PATH) or {"values": [], "subkeys": {}}
    REG_BACKUP_FILE.write_text(json.dumps(snapshot, indent=1), encoding="utf-8")
    return True


def restore_settings() -> bool:
    if not REG_BACKUP_FILE.exists():
        return False
    snapshot = json.loads(REG_BACKUP_FILE.read_text(encoding="utf-8"))
    _restore_key(winreg.HKEY_CURRENT_USER, REG_PATH, snapshot)
    REG_BACKUP_FILE.unlink()
    return True


def preset_window_size(width: int, height: int) -> None:
    """Write the geometry the app will read back on startup.  Presetting beats
    resizing after launch: the window opens already laid out at the target
    size, with no reflow frame to accidentally capture."""
    key = winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, REG_PATH, 0, winreg.KEY_READ | winreg.KEY_SET_VALUE)
    with key:
        for name, value in (("windowWidth", width), ("windowHeight", height)):
            kind = winreg.REG_DWORD
            try:
                _existing, kind = winreg.QueryValueEx(key, name)
            except FileNotFoundError:
                pass
            if kind == winreg.REG_SZ:
                winreg.SetValueEx(key, name, 0, winreg.REG_SZ, str(value))
            else:
                winreg.SetValueEx(key, name, 0, winreg.REG_DWORD, value)


# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------


def die(message: str, code: int = 1):
    print("ERROR: " + message, file=sys.stderr)
    sys.exit(code)


def parse_size(text: str) -> tuple[int, int]:
    match = re.fullmatch(r"(\d+)\s*[xX,]\s*(\d+)", text.strip())
    if not match:
        die(f"bad size {text!r} (expected WxH, e.g. 1200x800)")
    return int(match.group(1)), int(match.group(2))


def parse_point(text: str) -> tuple[int, int]:
    match = re.fullmatch(r"(-?\d+)\s*,\s*(-?\d+)", text.strip())
    if not match:
        die(f"bad point {text!r} (expected X,Y)")
    return int(match.group(1)), int(match.group(2))


def next_index() -> int:
    session = load_session()
    index = int(session.get("counter", 0)) + 1
    session["counter"] = index
    save_session(session)
    return index


# --------------------------------------------------------------------------
# shot
# --------------------------------------------------------------------------


def do_shot(args) -> Path:
    hwnd = find_window()
    if args.delay:
        time.sleep(args.delay / 1000.0)

    img, method = capture(hwnd, args.method, args.window)
    full_size = img.size

    note = ""
    if args.crop:
        x, y, w, h = args.crop
        box = (
            max(0, x),
            max(0, y),
            min(img.width, x + w),
            min(img.height, y + h),
        )
        if box[2] <= box[0] or box[3] <= box[1]:
            die(f"crop {args.crop} lies outside the {full_size[0]}x{full_size[1]} capture")
        img = img.crop(box)
        note += f" crop={box[0]},{box[1]},{box[2] - box[0]},{box[3] - box[1]}"

    if args.max_width and img.width > args.max_width:
        ratio = args.max_width / img.width
        img = img.resize((args.max_width, max(1, round(img.height * ratio))), Image.LANCZOS)
        note += f" scaled={ratio:.2f}"

    if args.grid:
        img = draw_grid(img, args.grid)
        note += f" grid={args.grid}"

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    name = re.sub(r"[^\w.-]+", "-", args.name or "shot")
    path = OUT_DIR / f"{next_index():03d}-{name}.png"
    img.save(path)

    rel = path.relative_to(REPO_ROOT).as_posix()
    print(f"saved {rel} {img.width}x{img.height} method={method}{note}")
    return path


def draw_grid(img: Image.Image, step: int) -> Image.Image:
    """Overlay a measuring grid -- eyeballing 4px vs 8px padding off a raw
    screenshot is guesswork otherwise."""
    out = img.convert("RGB")
    draw = ImageDraw.Draw(out, "RGBA")
    minor = (255, 0, 128, 38)
    major = (255, 0, 128, 105)
    for x in range(0, out.width, step):
        strong = (x // step) % 5 == 0
        draw.line([(x, 0), (x, out.height)], fill=major if strong else minor, width=1)
        if strong and x:
            draw.text((x + 2, 2), str(x), fill=(255, 0, 128, 220))
    for y in range(0, out.height, step):
        strong = (y // step) % 5 == 0
        draw.line([(0, y), (out.width, y)], fill=major if strong else minor, width=1)
        if strong and y:
            draw.text((2, y + 2), str(y), fill=(255, 0, 128, 220))
    return out


# --------------------------------------------------------------------------
# launch / close / resize / move / info
# --------------------------------------------------------------------------


def _launch_env(theme: str | None = None) -> dict:
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([str(QT_BIN), str(VCPKG_BIN), env.get("PATH", "")])
    # main.cpp turns this into QStyleHints::setColorScheme(), so light/dark can
    # be checked without flipping the real Windows theme.  "system" means "no
    # override", which also has to clear an inherited value from the shell.
    env.pop("MEGAEXPLORER_COLOR_SCHEME", None)
    if theme in ("light", "dark"):
        env["MEGAEXPLORER_COLOR_SCHEME"] = theme
    return env


def _sample(hwnd: int):
    img = capture_printwindow(hwnd, False)
    if img is None:
        return None
    return img.convert("L").resize((48, 48)).tobytes()


def wait_until_settled(hwnd: int, timeout: float) -> str:
    """The app auto-restores its session on startup, so the first frames are a
    login spinner.  Wait for the scene to stop changing rather than guessing a
    fixed delay."""
    deadline = time.time() + timeout
    previous = None
    stable = 0
    while time.time() < deadline:
        time.sleep(0.4)
        current = _sample(hwnd)
        if current is None:
            continue
        if current == previous:
            stable += 1
            if stable >= 3:
                return "settled"
        else:
            stable = 0
        previous = current
    return "timeout"


def do_launch(args) -> None:
    if not EXE_PATH.exists():
        die(f"{EXE_PATH.relative_to(REPO_ROOT).as_posix()} not found -- run `build` first")

    existing = find_window(required=False)
    if existing:
        die("an app window is already open -- `close` it first (or use `cycle`)")

    fresh_backup = backup_settings()
    session = load_session()

    size = None
    if args.size:
        size = parse_size(args.size)
    elif session.get("size"):
        size = tuple(session["size"])
    if size:
        preset_window_size(*size)
        session["size"] = list(size)

    # Remembered like --size, so a follow-up `cycle` keeps the same theme
    # instead of silently reverting to the OS one mid-comparison.
    theme = args.theme or session.get("theme")
    session["theme"] = theme

    started = time.time()
    process = subprocess.Popen(
        [str(EXE_PATH)],
        cwd=str(EXE_PATH.parent),
        env=_launch_env(theme),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    session["pid"] = process.pid
    session["hwnd"] = None
    save_session(session)

    hwnd = None
    appear_deadline = time.time() + args.timeout
    while time.time() < appear_deadline:
        if process.poll() is not None:
            die(f"app exited immediately (code {process.returncode})")
        candidates = enum_candidate_windows(process.pid)
        if candidates:
            hwnd = candidates[0]
            break
        time.sleep(0.2)
    if hwnd is None:
        die(f"no window appeared within {args.timeout}s")

    session["hwnd"] = hwnd
    save_session(session)

    if args.pos:
        x, y = parse_point(args.pos)
        user32.SetWindowPos(hwnd, None, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)

    state = wait_until_settled(hwnd, args.timeout)
    width, height = _client_size(hwnd)
    backup_note = "settings-backed-up" if fresh_backup else "settings-backup-reused"
    print(
        f"launched pid={process.pid} hwnd=0x{hwnd:X} client={width}x{height} "
        f"dpi={dpi_scale(hwnd):.2f} theme={theme or 'system'} ready={state} "
        f"in {time.time() - started:.1f}s ({backup_note})"
    )


def do_close(args) -> None:
    hwnd = find_window(required=False)
    session = load_session()
    pid = session.get("pid") or (_pid_of(hwnd) if hwnd else None)

    closed = "not-running"
    if hwnd:
        user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
        closed = "closed"
        deadline = time.time() + 12
        while time.time() < deadline and pid and _process_alive(pid):
            time.sleep(0.2)

    if pid and _process_alive(pid):
        if args.force:
            subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
            closed = "killed"
        else:
            closed = "still-running (use --force)"

    session["hwnd"] = None
    session["pid"] = None
    save_session(session)

    if args.keep_settings:
        restored = "settings-kept-dirty"
    else:
        restored = "settings-restored" if restore_settings() else "no-settings-backup"
    print(f"{closed} {restored}")


def do_resize(args) -> None:
    hwnd = find_window()
    width, height = parse_size(args.size)
    # SetWindowPos takes the window rect; add the non-client delta so the
    # client area (what gets captured) really ends up at the requested size.
    client_w, client_h = _client_size(hwnd)
    win_rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(win_rect))
    dx = (win_rect.right - win_rect.left) - client_w
    dy = (win_rect.bottom - win_rect.top) - client_h
    user32.SetWindowPos(hwnd, None, 0, 0, width + dx, height + dy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)
    time.sleep(0.4)
    now_w, now_h = _client_size(hwnd)
    session = load_session()
    session["size"] = [now_w, now_h]
    save_session(session)
    print(f"resized client={now_w}x{now_h}")


def do_move(args) -> None:
    hwnd = find_window()
    x, y = parse_point(args.pos)
    user32.SetWindowPos(hwnd, None, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)
    print(f"moved to {x},{y}")


def do_info(args) -> None:
    hwnd = find_window(required=False)
    if not hwnd:
        session = load_session()
        print("window: none")
        print(f"pid: {session.get('pid') or '-'} (not running)")
        print(f"exe: {'present' if EXE_PATH.exists() else 'MISSING -- build first'}")
        print(f"settings-backup: {'present (restore-settings to rewind)' if REG_BACKUP_FILE.exists() else 'none'}")
        return
    width, height = _client_size(hwnd)
    origin = wintypes.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(origin))
    print(f"window: 0x{hwnd:X} '{_window_title(hwnd)}' class={_window_class(hwnd)}")
    print(f"pid: {_pid_of(hwnd)}")
    print(f"client: {width}x{height} at screen {origin.x},{origin.y}")
    print(f"dpi-scale: {dpi_scale(hwnd):.2f}  (screenshot px = QML logical px * scale)")
    print(f"foreground: {user32.GetForegroundWindow() == hwnd}")
    print(f"settings-backup: {'present' if REG_BACKUP_FILE.exists() else 'none'}")


# --------------------------------------------------------------------------
# build
# --------------------------------------------------------------------------

_ISSUE_RE = re.compile(r"\b(fatal error|error|warning)\s+(?:C|LNK|MSB|D)\d+", re.IGNORECASE)


def summarize_build(output: str, limit: int = 20) -> tuple[list[str], int, int]:
    """MSBuild repeats every diagnostic once per referencing project and buries
    them in progress noise, so keep only the first copy of each app-level
    diagnostic."""
    seen: list[str] = []
    errors = warnings = 0
    for raw in output.splitlines():
        line = raw.strip()
        match = _ISSUE_RE.search(line)
        if not match:
            continue
        if "third_party/" in line.lower().replace("\\", "/"):
            continue
        if line in seen:
            continue
        seen.append(line)
        if match.group(1).lower() == "warning":
            warnings += 1
        else:
            errors += 1
    return seen[:limit], errors, warnings


def do_build(args) -> bool:
    if not CMAKE.exists():
        die(f"cmake not found at {CMAKE} (override with UI_SHOT_CMAKE)")
    started = time.time()

    if args.reconfigure:
        result = subprocess.run(
            [str(CMAKE), "--preset", PRESET],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            errors="replace",
        )
        if result.returncode != 0:
            tail = "\n".join((result.stdout + result.stderr).strip().splitlines()[-15:])
            print(f"reconfigure FAILED after {time.time() - started:.1f}s\n{tail}")
            return False

    result = subprocess.run(
        [str(CMAKE), "--build", str(BUILD_DIR), "--config", "Debug", "--target", args.target],
        cwd=str(REPO_ROOT),
        capture_output=True,
        text=True,
        errors="replace",
    )
    elapsed = time.time() - started
    issues, errors, warnings = summarize_build(result.stdout + result.stderr)

    if result.returncode == 0:
        print(f"build OK {elapsed:.1f}s (warnings: {warnings})")
    else:
        print(f"build FAILED {elapsed:.1f}s (errors: {errors}, warnings: {warnings})")
    for line in issues:
        print("  " + line)
    if not issues and result.returncode != 0:
        tail = "\n".join((result.stdout + result.stderr).strip().splitlines()[-15:])
        print(tail)
    return result.returncode == 0


# --------------------------------------------------------------------------
# cycle
# --------------------------------------------------------------------------


def do_cycle(args) -> None:
    close_args = argparse.Namespace(force=True, keep_settings=False)
    do_close(close_args)

    if not args.no_build:
        build_args = argparse.Namespace(target=args.target, reconfigure=args.reconfigure)
        if not do_build(build_args):
            die("build failed -- app not relaunched, fix the errors above")

    launch_args = argparse.Namespace(
        size=args.size, pos=args.pos, timeout=args.timeout, theme=args.theme
    )
    do_launch(launch_args)

    shot_args = argparse.Namespace(
        name=args.name,
        crop=args.crop,
        max_width=args.max_width,
        grid=args.grid,
        method=args.method,
        window=args.window,
        delay=args.delay,
    )
    do_shot(shot_args)


# --------------------------------------------------------------------------
# drive (input injection)
# --------------------------------------------------------------------------

VK_MAP = {
    "esc": 0x1B, "escape": 0x1B, "enter": 0x0D, "return": 0x0D, "tab": 0x09,
    "space": 0x20, "backspace": 0x08, "delete": 0x2E, "del": 0x2E, "insert": 0x2D,
    "home": 0x24, "end": 0x23, "pageup": 0x21, "pagedown": 0x22,
    "left": 0x25, "up": 0x26, "right": 0x27, "down": 0x28,
    "ctrl": 0x11, "control": 0x11, "shift": 0x10, "alt": 0x12, "win": 0x5B,
    "apps": 0x5D, "menu": 0x5D,
}
for _i in range(1, 25):
    VK_MAP[f"f{_i}"] = 0x6F + _i


def _send(inputs: list[INPUT]) -> None:
    array = (INPUT * len(inputs))(*inputs)
    user32.SendInput(len(inputs), array, ctypes.sizeof(INPUT))


def _mouse_input(flags: int, data: int = 0) -> INPUT:
    item = INPUT()
    item.type = INPUT_MOUSE
    item.mi = MOUSEINPUT(0, 0, data, flags, 0, 0)
    return item


def _key_input(vk: int, up: bool) -> INPUT:
    item = INPUT()
    item.type = INPUT_KEYBOARD
    item.ki = KEYBDINPUT(vk, 0, KEYEVENTF_KEYUP if up else 0, 0, 0)
    return item


def _unicode_input(char: str, up: bool) -> INPUT:
    item = INPUT()
    item.type = INPUT_KEYBOARD
    item.ki = KEYBDINPUT(0, ord(char), KEYEVENTF_UNICODE | (KEYEVENTF_KEYUP if up else 0), 0, 0)
    return item


def _to_screen(hwnd: int, x: int, y: int) -> tuple[int, int]:
    point = wintypes.POINT(x, y)
    user32.ClientToScreen(hwnd, ctypes.byref(point))
    return point.x, point.y


def _resolve_key(name: str) -> int:
    key = name.strip().lower()
    if key in VK_MAP:
        return VK_MAP[key]
    if len(key) == 1:
        scan = user32.VkKeyScanW(key)
        if scan != -1:
            return scan & 0xFF
    die(f"unknown key {name!r}")


def do_drive(args) -> None:
    hwnd = find_window()
    steps = [s.strip() for s in args.steps.split(";") if s.strip()]
    if not steps:
        die("no steps given")

    raised = bring_to_front(hwnd)
    saved_cursor = wintypes.POINT()
    user32.GetCursorPos(ctypes.byref(saved_cursor))

    if not args.no_countdown:
        for remaining in (2, 1):
            print(f"taking over mouse/keyboard in {remaining}s...", file=sys.stderr)
            time.sleep(1)

    done: list[str] = []
    try:
        for step in steps:
            done.append(_run_step(hwnd, step, args))
    finally:
        user32.SetCursorPos(saved_cursor.x, saved_cursor.y)

    note = " (window raised)" if raised else ""
    print(f"drive: {' | '.join(done)}{note}; cursor restored")


def _run_step(hwnd: int, step: str, args) -> str:
    parts = shlex.split(step)
    verb = parts[0].lower()
    rest = parts[1:]

    if verb == "wait":
        ms = int(rest[0]) if rest else 300
        time.sleep(ms / 1000.0)
        return f"wait {ms}ms"

    if verb in ("move", "hover"):
        x, y = parse_point(rest[0])
        sx, sy = _to_screen(hwnd, x, y)
        user32.SetCursorPos(sx, sy)
        time.sleep(0.08)
        return f"{verb} {x},{y}"

    if verb == "click":
        x, y = parse_point(rest[0])
        flags = [f.lower() for f in rest[1:]]
        sx, sy = _to_screen(hwnd, x, y)
        user32.SetCursorPos(sx, sy)
        time.sleep(0.08)
        if "right" in flags:
            down, up = MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP
        elif "middle" in flags:
            down, up = MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP
        else:
            down, up = MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP
        repeats = 2 if "double" in flags else 1
        for _ in range(repeats):
            _send([_mouse_input(down), _mouse_input(up)])
            time.sleep(0.05)
        return f"click {x},{y}{' ' + ' '.join(flags) if flags else ''}"

    if verb == "drag":
        x1, y1 = parse_point(rest[0])
        x2, y2 = parse_point(rest[1])
        sx1, sy1 = _to_screen(hwnd, x1, y1)
        sx2, sy2 = _to_screen(hwnd, x2, y2)
        user32.SetCursorPos(sx1, sy1)
        time.sleep(0.1)
        _send([_mouse_input(MOUSEEVENTF_LEFTDOWN)])
        # Qt needs several motion events to start a drag, not one teleport.
        for i in range(1, 13):
            user32.SetCursorPos(
                sx1 + round((sx2 - sx1) * i / 12), sy1 + round((sy2 - sy1) * i / 12)
            )
            time.sleep(0.03)
        _send([_mouse_input(MOUSEEVENTF_LEFTUP)])
        return f"drag {x1},{y1}->{x2},{y2}"

    if verb == "scroll":
        x, y = parse_point(rest[0])
        amount = int(rest[1]) if len(rest) > 1 else -3
        sx, sy = _to_screen(hwnd, x, y)
        user32.SetCursorPos(sx, sy)
        time.sleep(0.05)
        _send([_mouse_input(MOUSEEVENTF_WHEEL, ctypes.c_int32(amount * 120).value & 0xFFFFFFFF)])
        return f"scroll {amount}"

    if verb == "key":
        combo = rest[0]
        names = [n for n in combo.split("+") if n]
        vks = [_resolve_key(n) for n in names]
        events = [_key_input(vk, False) for vk in vks]
        events += [_key_input(vk, True) for vk in reversed(vks)]
        _send(events)
        time.sleep(0.05)
        return f"key {combo}"

    if verb == "type":
        text = " ".join(rest)
        events = []
        for char in text:
            events.append(_unicode_input(char, False))
            events.append(_unicode_input(char, True))
        if events:
            _send(events)
        time.sleep(0.05)
        return f"type {text!r}"

    if verb == "shot":
        name = rest[0] if rest else "drive"
        shot_args = argparse.Namespace(
            name=name,
            crop=args.crop,
            max_width=args.max_width,
            grid=args.grid,
            method=args.method,
            window=False,
            delay=0,
        )
        path = do_shot(shot_args)
        return f"shot {path.name}"

    die(f"unknown step {step!r} (see reference.md)")


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


def crop_type(text: str):
    parts = [p for p in re.split(r"[,\s]+", text.strip()) if p]
    if len(parts) != 4 or not all(p.lstrip("-").isdigit() for p in parts):
        raise argparse.ArgumentTypeError("expected x,y,w,h")
    return tuple(int(p) for p in parts)


def add_theme_option(parser) -> None:
    parser.add_argument(
        "--theme",
        choices=["light", "dark", "system"],
        help="force the app's colour scheme (default: last used, else the OS one)",
    )


def add_shot_options(parser, default_method: str = "auto") -> None:
    parser.add_argument("--crop", type=crop_type, help="x,y,w,h in screenshot pixels")
    parser.add_argument("--max-width", type=int, default=0, help="downscale if wider than this")
    parser.add_argument("--grid", type=int, default=0, help="overlay a measuring grid every N px")
    parser.add_argument(
        "--method", choices=["auto", "print", "screen"], default=default_method
    )
    parser.add_argument("--delay", type=int, default=0, help="wait N ms before capturing")


def main() -> None:
    _make_dpi_aware()

    # Build diagnostics quote source lines, which carry non-ASCII (QML strings,
    # MSVC's U+FFFD for bytes it could not decode). Printing those to a cp932
    # console raises UnicodeEncodeError and loses the whole build summary.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass

    parser = argparse.ArgumentParser(prog="ui_shot", description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    p = subparsers.add_parser("launch", help="start the app at a given size")
    p.add_argument("--size", help="WxH client size (default: last used)")
    p.add_argument("--pos", help="X,Y screen position")
    p.add_argument("--timeout", type=float, default=45.0)
    add_theme_option(p)
    p.set_defaults(func=do_launch)

    p = subparsers.add_parser("shot", help="capture the window")
    p.add_argument("name", nargs="?", default="shot")
    p.add_argument("--window", action="store_true", help="include the DWM frame/shadow")
    add_shot_options(p)
    p.set_defaults(func=do_shot)

    p = subparsers.add_parser("close", help="close the app and rewind its settings")
    p.add_argument("--force", action="store_true")
    p.add_argument("--keep-settings", action="store_true", help="do not rewind the registry")
    p.set_defaults(func=do_close)

    p = subparsers.add_parser("build", help="build and summarize errors/warnings")
    p.add_argument("--target", default=DEFAULT_TARGET)
    p.add_argument("--reconfigure", action="store_true", help="needed after adding/removing QML files")
    p.set_defaults(func=do_build)

    p = subparsers.add_parser("cycle", help="close + build + launch + shot")
    p.add_argument("name", nargs="?", default="shot")
    p.add_argument("--size")
    p.add_argument("--pos")
    p.add_argument("--timeout", type=float, default=45.0)
    p.add_argument("--target", default=DEFAULT_TARGET)
    p.add_argument("--reconfigure", action="store_true")
    p.add_argument("--no-build", action="store_true")
    p.add_argument("--window", action="store_true")
    add_theme_option(p)
    add_shot_options(p)
    p.set_defaults(func=do_cycle)

    p = subparsers.add_parser("resize", help="resize the running window")
    p.add_argument("size")
    p.set_defaults(func=do_resize)

    p = subparsers.add_parser("move", help="move the running window")
    p.add_argument("pos")
    p.set_defaults(func=do_move)

    p = subparsers.add_parser("info", help="report window/dpi/backup state")
    p.set_defaults(func=do_info)

    p = subparsers.add_parser(
        "drive",
        help="inject mouse/keyboard steps -- ASK THE USER BEFORE RUNNING",
    )
    p.add_argument("steps", help='";"-separated, e.g. "click 400,300 right; wait 400; shot menu"')
    p.add_argument("--no-countdown", action="store_true")
    # Driving already requires the window to be frontmost for SendInput, so
    # capture_screen's one drawback does not apply here.
    add_shot_options(p, default_method="screen")
    p.set_defaults(func=do_drive)

    p = subparsers.add_parser("restore-settings", help="rewind the registry from a stale backup")
    p.set_defaults(func=lambda a: print("settings-restored" if restore_settings() else "no-settings-backup"))

    args = parser.parse_args()
    if args.func(args) is False:
        sys.exit(1)


if __name__ == "__main__":
    main()
