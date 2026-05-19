"""Ad-hoc minidump memory peek — dumps memory at given addresses, resolving
module names.  Usage: python dmp_peek.py <dump.dmp> <addr-hex> [addr-hex ...]"""
import struct, sys, os

def main():
    path = sys.argv[1]
    addrs = [int(a, 16) for a in sys.argv[2:]]
    d = open(path, 'rb').read()
    ns = struct.unpack_from('<I', d, 8)[0]
    dr = struct.unpack_from('<I', d, 12)[0]
    streams = {}
    for i in range(ns):
        o = dr + i * 12
        st, ds, rva = struct.unpack_from('<III', d, o)
        streams.setdefault(st, (ds, rva))

    mem = []
    if 9 in streams:
        _, rva = streams[9]
        nr = struct.unpack_from('<Q', d, rva)[0]
        base = struct.unpack_from('<Q', d, rva + 8)[0]
        cur = base
        for i in range(nr):
            ro = rva + 16 + i * 16
            s, sz = struct.unpack_from('<QQ', d, ro)
            mem.append((s, d[cur:cur + sz]))
            cur += sz
    if 5 in streams:
        _, rva = streams[5]
        nr = struct.unpack_from('<I', d, rva)[0]
        for i in range(nr):
            ro = rva + 4 + i * 16
            s, dsz, drva = struct.unpack_from('<QII', d, ro)
            mem.append((s, d[drva:drva + dsz]))

    def rd(a, n):
        for s, b in mem:
            if s <= a < s + len(b):
                return b[a - s:a - s + n]
        return None

    modrva = streams[4][1]
    nm = struct.unpack_from('<I', d, modrva)[0]
    mods = []
    for i in range(nm):
        mo = modrva + 4 + i * 108
        base = struct.unpack_from('<Q', d, mo)[0]
        size = struct.unpack_from('<I', d, mo + 8)[0]
        nr2 = struct.unpack_from('<I', d, mo + 20)[0]
        ln = struct.unpack_from('<I', d, nr2)[0]
        nm2 = d[nr2 + 4:nr2 + 4 + ln].decode('utf-16-le', 'replace')
        mods.append((os.path.basename(nm2.replace('\\', '/')), base, size))

    def mof(a):
        for n, b, s in mods:
            if b <= a < b + s:
                return '[%s+0x%X]' % (n, a - b)
        return ''

    for addr in addrs:
        bs = rd(addr, 0x80)
        print('0x%016X:' % addr)
        if bs is None:
            print('  (not in dump)')
            continue
        for off in range(0, len(bs), 8):
            v = struct.unpack_from('<Q', bs, off)[0]
            print('  +0x%02X: 0x%016X  %s' % (off, v, mof(v)))

if __name__ == '__main__':
    main()
