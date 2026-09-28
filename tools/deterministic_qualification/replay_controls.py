"""Bounded Windows-input driver for production indexed replay controls."""
import re
import time


class IndexedControlEvents:
    """Retain only control receipts while consuming the owned log once."""
    keys=(b"DX11 overlay initialised ",b"replay button name=",b"indexed seek resumed run_id=",
          b"playback controls held run_id=",b"numeric replay seek submitted target=",
          b"indexed seek already held tick=",b"replay resume control hwnd=",
          b"private replay output displayed tick=",b"playback controls resumed run_id=",
          b"indexed numeric entry waiting run_id=",b"indexed numeric entry accepted run_id=",b"native menu exit waiting run_id=")
    def __init__(self):
        self.offset=0;self.partial=b"";self.events=[];self.event_bytes=0;self.prefix=None

    def read(self,path):
        with path.open("rb") as stream:
            prefix=stream.read(64)
            if self.prefix is None and len(prefix)==64:self.prefix=prefix
            if self.prefix is not None and prefix!=self.prefix:raise RuntimeError("owned UI log was replaced")
            stream.seek(0,2);end=stream.tell()
            if end<self.offset or end-self.offset>8*1024*1024:raise RuntimeError("owned UI log changed or exceeded incremental budget")
            stream.seek(self.offset);data=stream.read(end-self.offset);self.offset+=len(data)
        lines=(self.partial+data).split(b"\n");self.partial=lines.pop()
        if len(self.partial)>1024*1024:raise RuntimeError("owned UI log line exceeds bounds")
        for line in lines:
            if not any(key in line for key in self.keys):continue
            if self.event_bytes+len(line)+1>65536:raise RuntimeError("owned UI event retention exceeds bounds")
            self.events.append(line.decode("utf-8",errors="replace"));self.event_bytes+=len(line)+1
        return "\n".join(self.events)

    def witness(self):
        return {"bytes_read":self.offset,"retained_event_bytes":self.event_bytes,"events":len(self.events),"incremental":True}


def capture_owned_window(hwnd,size):
    """Request HWND rendering. Never fall back to a desktop rectangle."""
    import ctypes
    from ctypes import wintypes as w
    import win32gui,win32ui
    from PIL import Image
    user=ctypes.WinDLL('user32',use_last_error=True)
    user.PrintWindow.argtypes=[w.HWND,w.HDC,w.UINT];user.PrintWindow.restype=w.BOOL
    dc=win32gui.GetWindowDC(hwnd)
    if not dc:raise RuntimeError('replay window capture DC unavailable')
    source=memory=bitmap=previous=None
    try:
        source=win32ui.CreateDCFromHandle(dc);memory=source.CreateCompatibleDC()
        bitmap=win32ui.CreateBitmap();bitmap.CreateCompatibleBitmap(source,*size)
        previous=memory.SelectObject(bitmap)
        # Try compositor-backed content, then the window's own WM_PRINT path.
        # Never substitute desktop pixels when either path is unavailable.
        for flags in (2,0):
            memory.FillSolidRect((0,0,*size),0)
            if not user.PrintWindow(hwnd,memory.GetSafeHdc(),flags):continue
            image=Image.frombuffer('RGB',size,bitmap.GetBitmapBits(True),'raw','BGRX',0,1).copy()
            if image.getbbox() is not None:return image
        raise RuntimeError('replay window rendering capture unavailable or empty')
    finally:
        if previous is not None:memory.SelectObject(previous)
        if bitmap is not None:win32gui.DeleteObject(bitmap.GetHandle())
        if memory is not None:memory.DeleteDC()
        # The window DC is borrowed; ReleaseDC owns its release (not DeleteDC).
        source=None
        win32gui.ReleaseDC(hwnd,dc)


def capture_indexed_control_hold(report,report_path,pid,text):
    """Capture the retained replay-output HWND, never the foreground window."""
    import ctypes
    from ctypes import wintypes as w
    from .artifacts import sha256_file
    state=report.get("indexed_ui",{});tick=state.get("tick")
    if tick is None:
        held=re.findall(r"playback controls held run_id="+re.escape(report["run_id"])+r" tick=(\d+) resume_baseline=\d+",text)
        if len(held)>1:raise RuntimeError("multiple playback control image holds")
        if held:tick=int(held[0])
    if tick is None:return
    label="submitted" if "submit_input" in state else "held"
    images=state.setdefault("images",{})
    if label in images:return
    rows=re.findall(r"private replay output displayed tick="+str(tick)+r" hwnd=(\d+) native_target_untouched=true",text)
    if not rows:return
    hwnd=int(rows[-1]);user=ctypes.WinDLL("user32",use_last_error=True)
    user.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)]
    user.GetWindowRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]
    owner=w.DWORD();rect=w.RECT();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
    if owner.value!=pid or not user.GetWindowRect(hwnd,ctypes.byref(rect)):
        raise RuntimeError("indexed UI image lost its replay-output owner")
    size=(rect.right-rect.left,rect.bottom-rect.top)
    if min(size)<128 or size[0]*size[1]>4*1024*1024:raise RuntimeError("indexed UI image descriptor exceeds bounds")
    report['desktop_capture_timing_perturbed']=True
    image=capture_owned_window(hwnd,size)
    valid=user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
    if not valid or owner.value!=pid or image.size!=size:raise RuntimeError("indexed UI image owner/extent changed during capture")
    path=report_path.with_name(report_path.stem+f"-controls-{tick}-{label}.png")
    image.save(path)
    images[label]={"path":str(path),"sha256":sha256_file(path),"hwnd":hwnd,"tick":tick,"size":size,"source":"retained replay-output window","method":"PrintWindow owned rendering"}


def drive_indexed_controls(report, pid, text, click, focus):
    run=report["run_id"]
    state=report.setdefault("indexed_ui",{})
    resumed=f"indexed seek resumed run_id={run} " in text
    if "focus" not in state and not resumed and "[GameImGui] DX11 overlay initialised " in text:
        value=focus(pid)
        if value:state["focus"]=value
    def control(name,tick=None):
        rows=re.findall(r"replay button name="+re.escape(name)+r" hwnd=(\d+) screen_x=(-?\d+) screen_y=(-?\d+) tick=(\d+)",text)
        rows=[tuple(map(int,row)) for row in rows if tick is None or int(row[3])==tick]
        return rows[-1][:3] if rows else None
    if report.get('indexed_control_protocol')=='retained_menu_exit_v1':
        waiting=re.findall(r'native menu exit waiting run_id='+re.escape(run)+r' B=(\d+) no_transaction=true observer_request=false',text)
        if len(waiting)>1:raise RuntimeError('duplicate native UI exit request')
        if waiting and 'exit_input' not in state:
            button=control('exit',int(waiting[0]))
            if button:state['exit_input']=click(pid,*button)
        return
    entry=re.findall(r"indexed numeric entry waiting run_id="+re.escape(run)+r" origin=(\d+) target=(\d+)",text)
    if entry and not resumed and f"indexed numeric entry accepted run_id={run} " not in text:
        if len(entry)!=1 or int(entry[0][1])!=report['index_seek_target'] or int(entry[0][0])<=int(entry[0][1]):
            raise RuntimeError('indexed numeric entry target/origin mismatch')
        if 'focus' not in state:return
        origin,target=map(int,entry[0]);pending=state.setdefault('entry',{'origin':origin,'target':target,'started_at':time.monotonic()})
        if time.monotonic()-pending['started_at']>10:raise RuntimeError('indexed numeric entry was not accepted within 10 seconds')
        for receipt,name in (('open_input','number_open'),('clear_input','number_clear')):
            if receipt not in pending:
                button=control(name,origin)
                if button:pending[receipt]=click(pid,*button)
                return
        digits=pending.setdefault('digits',[])
        if len(digits)<len(str(target)):
            digit=str(target)[len(digits)];button=control(digit,origin)
            if button:digits.append({'digit':digit,'input':click(pid,*button)})
            return
        if 'submit_input' not in pending:
            button=control('number_submit',origin)
            if button:pending['submit_input']=click(pid,*button)
        return
    if not resumed:return
    if "focus" not in state:raise RuntimeError("indexed UI lacks pre-hold window focus")
    if f"playback controls resumed run_id={run} " not in text:
        progress=tuple(key in state for key in ("pause_input","tick","open_input","clear_input","submit_input","resume_input"))+(len(state.get("digits",[])),)
        previous=state.get("progress")
        now=time.monotonic()
        if previous is None or tuple(previous["phase"])!=progress:
            state["progress"]={"phase":progress,"changed_at":now}
        elif now-previous["changed_at"]>10:
            raise RuntimeError("indexed UI made no control progress for 10 seconds")
    if "pause_input" not in state:
        button=control("pause")
        if button:state["pause_input"]=click(pid,*button)
        return
    held=re.findall(r"playback controls held run_id="+re.escape(run)+r" tick=(\d+) resume_baseline=(\d+)",text)
    if not held:return
    if len(held)!=1:raise RuntimeError("multiple playback control holds")
    tick=int(held[0][0]);state["tick"]=tick
    for receipt,name in (("open_input","number_open"),("clear_input","number_clear")):
        if receipt not in state:
            button=control(name,tick)
            if button:state[receipt]=click(pid,*button)
            return
    digits=state.setdefault("digits",[])
    if len(digits)<len(str(tick)):
        digit=str(tick)[len(digits)];button=control(digit,tick)
        if button:digits.append({"digit":digit,"input":click(pid,*button)})
        return
    if "submit_input" not in state:
        button=control("number_submit",tick)
        if button:state["submit_input"]=click(pid,*button)
        return
    if (f"numeric replay seek submitted target={tick}" not in text
            or f"indexed seek already held tick={tick}" not in text):return
    if "resume_input" not in state:
        rows=re.findall(r"replay resume control hwnd=(\d+) screen_x=(-?\d+) screen_y=(-?\d+)",text)
        if rows:state["resume_input"]=click(pid,*map(int,rows[-1]))


def validate_indexed_controls(report,text):
    if report.get("indexed_control_protocol")=="retained_menu_exit_v1":
        return validate_indexed_menu_exit(report,text)
    state=report.get("indexed_ui",{});run=report["run_id"]
    initial=report.get('indexed_control_protocol')=='numeric_initial_seek_v2'
    if initial:
        entry=state.get('entry',{});target=report['index_seek_target'];origin=entry.get('origin',0)
        if origin<=target or entry.get('target')!=target or ''.join(x['digit'] for x in entry.get('digits',[]))!=str(target):
            raise RuntimeError('initial numeric seek entry lacks a different target')
        expected=[f'indexed numeric entry waiting run_id={run} origin={origin} target={target}',
                  f'numeric replay seek submitted target={target}',
                  f'indexed numeric entry accepted run_id={run} origin={origin} target={target} production_seek=true',
                  f'indexed seek resumed run_id={run} tick={target} target_tails_complete=true']
        if any(text.count(x)!=1 for x in expected) or [text.index(x) for x in expected]!=sorted(text.index(x) for x in expected):
            raise RuntimeError('initial numeric seek submission/production acceptance/completion order disagrees')
    held=re.findall(r"playback controls held run_id="+re.escape(run)+r" tick=(\d+) resume_baseline=(\d+)",text)
    resumed=re.findall(r"playback controls resumed run_id="+re.escape(run)+r" tick=(\d+) held_us=(\d+) frames=(\d+) unchanged=true application_complete=true",text)
    if len(held)!=1 or len(resumed)!=1 or held[0][0]!=resumed[0][0]:
        raise RuntimeError("indexed controls lack unique unchanged hold/resume")
    tick=int(held[0][0]);last=report["index_seek_target"]+report["index_seek_continuation"]
    if (state.get("tick")!=tick or tick<=report["index_seek_target"] or last<tick+120
            or int(resumed[0][1])<500000 or int(resumed[0][2])<30
            or "".join(x["digit"] for x in state.get("digits",[]))!=str(tick)):
        raise RuntimeError("indexed controls lack numeric target or hold/continuation coverage")
    receipts=[state.get(key,{}) for key in ("pause_input","open_input","clear_input","submit_input","resume_input")]
    receipts += [x.get("input",{}) for x in state["digits"]]
    if initial:
        receipts += [entry.get(key,{}) for key in ('open_input','clear_input','submit_input')]
        receipts += [x.get('input',{}) for x in entry['digits']]
    focus=state.get("focus",{})
    outputs={int(h) for h in re.findall(r"private replay output displayed tick=\d+ hwnd=(\d+) native_target_untouched=true",text)}
    def owned_window(row):
        if row.get('hwnd')==focus.get('hwnd'):return True
        return (row.get('hwnd') in outputs and row.get('window_class')=='HorseModReplayOutputUi'
                and row.get('foreground_verified') is True)
    if not focus.get("pid") or not focus.get("hwnd") or any(row.get("method")!="Windows SendInput" or not row.get("button_released")
            or row.get("pid")!=focus["pid"] or not owned_window(row) for row in receipts):
        raise RuntimeError("indexed control Windows input/release receipts missing")
    markers=[f"playback controls held run_id={run} tick={tick} ",
        f"numeric replay seek submitted target={tick}",f"indexed seek already held tick={tick}",
        f"playback controls resumed run_id={run} tick={tick} "]
    if any(text.count(marker)!=1 for marker in markers) or [text.index(x) for x in markers]!=sorted(text.index(x) for x in markers):
        raise RuntimeError("indexed numeric submission/host acceptance/resume order disagrees")
    rate=re.findall(r"indexed seek resume rate run_id="+re.escape(run)+r" target=\d+ ticks=120 elapsed_us=(\d+) origin=(\d+)",text)
    if len(rate)!=1 or int(rate[0][1])!=tick or int(rate[0][0])<=0:
        raise RuntimeError("indexed controls resume rate must start after the deliberate pause")
    return {"result":"pass","paused_tick":tick,"hold_us":int(resumed[0][1]),"held_frames":int(resumed[0][2]),
        "initial_different_target_numeric_seek":initial,
        "timing_scope":"includes Windows numeric-entry and desktop capture overhead" if initial else "legacy controls",
        "numeric_same_tick_submission":True,"independent_continuation_available":last-tick,
        "resume_rate_origin":tick,"scope":"Windows numeric seek, pause, same-tick request and resume; visual coherence requires separate image review" if initial else "Windows pause, numeric same-tick request and resume; different-target numeric seeking and desktop coherence require separate evidence"}


def validate_indexed_menu_exit(report,text):
    state=report.get('indexed_ui',{});run=report['run_id'];receipt=state.get('exit_input',{});focus=state.get('focus',{})
    waiting=re.findall(r'native menu exit waiting run_id='+re.escape(run)+r' B=(\d+) no_transaction=true observer_request=false',text)
    if len(waiting)!=1:raise RuntimeError('native menu exit lacks unique observer wait')
    tick=int(waiting[0]);hwnd=receipt.get('hwnd',0)
    if (not hwnd or not focus.get('pid') or receipt.get('pid')!=focus['pid']
            or receipt.get('method')!='Windows SendInput' or not receipt.get('button_released')
            or receipt.get('window_class')!='HorseModReplayOutputUi' or receipt.get('foreground_verified') is not True
            or f'private replay output displayed tick={tick} hwnd={hwnd} native_target_untouched=true' not in text):
        raise RuntimeError('native menu exit lacks owned held-window input evidence')
    markers=[f'native menu exit waiting run_id={run} B={tick}',f'native replay exit deferred tick={tick} admitted=true original_invocations=0',
             f'replay menu exit submitted tick={tick} accepted=true B_transaction=false native_stop_guarded=true',
             f'native replay exit cleanup completed requested_tick={tick}',f'native session exit completed run_id={run}']
    if any(text.count(m)!=1 for m in markers) or [text.index(m) for m in markers]!=sorted(text.index(m) for m in markers):
        raise RuntimeError('native menu exit publication/cleanup order disagrees')
    return {'result':'pass','tick':tick,'input':receipt,'scope':'visible native replay-list exit from recovered completed endpoint; no active B transaction'}


