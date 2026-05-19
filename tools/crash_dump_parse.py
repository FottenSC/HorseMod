"""Minimal Windows minidump (.dmp) parser — extracts the exception record,
faulting RIP, and a stack-scan pseudo-callstack, resolving addresses to
module + RVA.  Written for diagnosing the SC6 / HorseMod seek crashes when
no debugger (cdb/windbg) is installed.

Usage:  python crash_dump_parse.py <dump1.dmp> [dump2.dmp ...]
"""
import struct, sys, os

EXC = {
    0xC0000005: "ACCESS_VIOLATION",
    0xC0000374: "HEAP_CORRUPTION",
    0xC00000FD: "STACK_OVERFLOW",
    0xC000001D: "ILLEGAL_INSTRUCTION",
    0xC0000409: "FAST_FAIL / STACK_BUFFER_OVERRUN",
    0x80000003: "BREAKPOINT",
    0xE06D7363: "C++ EXCEPTION",
}

def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def u64(b, o): return struct.unpack_from('<Q', b, o)[0]

def parse(path):
    with open(path, 'rb') as f:
        d = f.read()
    print(f"\n{'='*70}\n{os.path.basename(path)}   ({len(d)/1024/1024:.1f} MB)\n{'='*70}")
    if d[:4] != b'MDMP':
        print("  not a minidump (bad signature)"); return
    nstreams = u32(d, 8)
    dirrva   = u32(d, 12)
    streams = {}
    for i in range(nstreams):
        o = dirrva + i * 12
        st, ds, rva = u32(d, o), u32(d, o + 4), u32(d, o + 8)
        streams.setdefault(st, (ds, rva))

    # ---- modules (stream type 4) ----
    modules = []  # (name, base, size)
    if 4 in streams:
        _, rva = streams[4]
        nmod = u32(d, rva)
        for i in range(nmod):
            mo = rva + 4 + i * 108          # MINIDUMP_MODULE = 108 bytes
            if mo + 24 > len(d):
                break
            base   = u64(d, mo)
            size   = u32(d, mo + 8)
            nameRva = u32(d, mo + 20)
            if nameRva + 4 > len(d):
                continue
            ln = u32(d, nameRva)
            if nameRva + 4 + ln > len(d):
                continue
            name = d[nameRva + 4: nameRva + 4 + ln].decode('utf-16-le', 'replace')
            modules.append((os.path.basename(name), base, size))

    def modof(addr):
        for n, b, s in modules:
            if b <= addr < b + s:
                return f"{n}+0x{addr-b:X}"
        return None

    # ---- memory ranges (for the stack scan) ----
    # list of (start, bytes)
    mem = []
    if 9 in streams:                       # Memory64ListStream
        _, rva = streams[9]
        nranges = u64(d, rva)
        baseRva = u64(d, rva + 8)
        cur = baseRva
        for i in range(nranges):
            ro = rva + 16 + i * 16
            start = u64(d, ro)
            sz    = u64(d, ro + 8)
            mem.append((start, d[cur:cur + sz]))
            cur += sz
    if 5 in streams:                       # MemoryListStream
        _, rva = streams[5]
        nranges = u32(d, rva)
        for i in range(nranges):
            ro = rva + 4 + i * 16
            start = u64(d, ro)
            dsz   = u32(d, ro + 8)
            drva  = u32(d, ro + 12)
            mem.append((start, d[drva:drva + dsz]))

    def readmem(addr, n):
        for start, blob in mem:
            if start <= addr < start + len(blob):
                off = addr - start
                return blob[off:off + n]
        return None

    # ---- exception (stream type 6) ----
    if 6 not in streams:
        print("  no ExceptionStream");
    else:
        _, rva = streams[6]
        eo = rva + 8                       # skip ThreadId + alignment
        ecode  = u32(d, eo)
        eaddr  = u64(d, eo + 16)
        nparams = u32(d, eo + 24)
        einfo  = [u64(d, eo + 32 + j * 8) for j in range(min(nparams, 15))]
        co = rva + 8 + 152                 # ThreadContext location descriptor
        ctx_size = u32(d, co)
        ctx_rva  = u32(d, co + 4)
        # x64 CONTEXT register offsets.
        REGOFF = {'Rax':0x78,'Rcx':0x80,'Rdx':0x88,'Rbx':0x90,'Rsp':0x98,
                  'Rbp':0xA0,'Rsi':0xA8,'Rdi':0xB0,'R8':0xB8,'R9':0xC0,
                  'R10':0xC8,'R11':0xD0,'R12':0xD8,'R13':0xE0,'R14':0xE8,
                  'R15':0xF0,'Rip':0xF8}
        reg = {k: u64(d, ctx_rva + o) for k, o in REGOFF.items()}
        rip = reg['Rip']; rsp = reg['Rsp']; rbp = reg['Rbp']

        print(f"  ExceptionCode = 0x{ecode:08X}  {EXC.get(ecode,'?')}")
        em = modof(eaddr)
        print(f"  ExceptionAddr = 0x{eaddr:016X}" + (f"  [{em}]" if em else ""))
        if ecode == 0xC0000005 and nparams >= 2:
            acc = {0: 'READ', 1: 'WRITE', 8: 'EXECUTE'}.get(einfo[0], str(einfo[0]))
            tm = modof(einfo[1])
            print(f"  --> Access violation: {acc} at 0x{einfo[1]:016X}"
                  + (f"  [{tm}]" if tm else "  [unmapped]"))
        rm = modof(rip)
        print(f"  RIP = 0x{rip:016X}" + (f"  [{rm}]" if rm else "  [unmapped!]"))
        # registers, annotated with the module they point into (if any)
        for k in ('Rax','Rbx','Rcx','Rdx','Rsi','Rdi','Rbp','Rsp',
                  'R8','R9','R10','R11','R12','R13','R14','R15'):
            v = reg[k]; m = modof(v)
            print(f"    {k:4s}= 0x{v:016X}" + (f"  [{m}]" if m else ""))
        # faulting instruction bytes (if the code page is in the dump)
        code = readmem(rip, 24)
        if code:
            print("  RIP bytes: " + ' '.join(f'{b:02X}' for b in code))
        else:
            print("  RIP bytes: (code page not in dump)")

        # ---- stack scan: u64s on the stack that point into a module ----
        print("  --- stack scan (return-address candidates) ---")
        stk = readmem(rsp, 0x2800)
        if stk:
            seen = 0
            for off in range(0, len(stk) - 8, 8):
                v = struct.unpack_from('<Q', stk, off)[0]
                m = modof(v)
                if m and ('.exe' in m or '.dll' in m):
                    print(f"    rsp+0x{off:04X}: 0x{v:016X}  [{m}]")
                    seen += 1
                    if seen >= 40:
                        break
        else:
            print("    (stack memory not in dump)")

    print("\n  --- loaded modules of interest ---")
    for n, b, s in modules:
        if n.lower() in ('soulcaliburvi.exe', 'ue4ss.dll', 'main.dll'):
            print(f"    {n:24s} base=0x{b:016X} size=0x{s:X}")

if __name__ == '__main__':
    for p in sys.argv[1:]:
        parse(p)
