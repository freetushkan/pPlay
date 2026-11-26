#!/usr/bin/env python3
"""Headless libmpv EOF/replay contract test (not a Switch integration test).
Requires libmpv.so.2 and ffmpeg. Run: python3 tests/mpv_keep_open_smoke.py
"""
import subprocess, tempfile
from pathlib import Path
_tmp = tempfile.TemporaryDirectory(prefix="pplay-eof-")
media = str(Path(_tmp.name) / "eof.mp4")
subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                "color=c=blue:s=160x90:r=24:d=0.5", "-c:v", "mpeg4", "-y", media], check=True)
import ctypes as C,time
m=C.CDLL('libmpv.so.2')
m.mpv_create.restype=C.c_void_p
for n,args,ret in [('mpv_set_option_string',[C.c_void_p,C.c_char_p,C.c_char_p],C.c_int),('mpv_initialize',[C.c_void_p],C.c_int),('mpv_command_string',[C.c_void_p,C.c_char_p],C.c_int),('mpv_get_property',[C.c_void_p,C.c_char_p,C.c_int,C.c_void_p],C.c_int),('mpv_terminate_destroy',[C.c_void_p],None)]:
 f=getattr(m,n);f.argtypes=args;f.restype=ret
h=m.mpv_create()
for k,v in [('vo','null'),('ao','null'),('keep-open','yes'),('terminal','no')]:assert m.mpv_set_option_string(h,k.encode(),v.encode())>=0
assert m.mpv_initialize(h)>=0
assert m.mpv_command_string(h, ('loadfile ' + media).encode())>=0
def flag(n):
 v=C.c_int();r=m.mpv_get_property(h,n.encode(),3,C.byref(v));return v.value if r>=0 else None
for _ in range(100):
 if flag('eof-reached'):break
 time.sleep(.03)
assert flag('eof-reached')==1
assert flag('idle-active')==0
assert flag('pause')==1
assert m.mpv_command_string(h,b'seek 0 absolute+exact')>=0
assert m.mpv_command_string(h,b'set pause no')>=0
time.sleep(.08)
assert flag('eof-reached')==0
assert flag('idle-active')==0
assert flag('pause')==0
assert m.mpv_command_string(h,b'stop')>=0
time.sleep(.05)
assert flag('idle-active')==1
m.mpv_terminate_destroy(h)
print('PASS: keep-open EOF stays loaded/paused; replay seek resumes; explicit stop goes idle')
