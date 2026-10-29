import sys, pickle, bisect, re, pefile
insns = pickle.load(open(sys.argv[1], 'rb'))
pe = pefile.PE(sys.argv[2]); base = pe.OPTIONAL_HEADER.ImageBase
addrs = [i[0] for i in insns]
starts = [a for k,(a,_,m,o) in enumerate(insns[:-1]) if m=='push' and o=='ebp' and insns[k+1][2]=='mov' and insns[k+1][3]=='ebp, esp']
def s_at(va):
    try: off = pe.get_offset_from_rva(va - base)
    except Exception: return None
    d = pe.__data__[off:off+400]
    w = d.split(b'\0\0')[0]
    if len(w) >= 4 and len(w) % 2 == 1: w += b'\0'
    try:
        t = w.decode('utf-16le')
        if len(t) >= 3 and all(32 <= ord(c) < 0x3000 for c in t): return 'W:' + t[:90]
    except Exception: pass
    a = d.split(b'\0')[0]
    if len(a) >= 4 and all(32 <= c < 127 for c in a): return 'A:' + a[:90].decode()
for f in sys.argv[3:]:
    f = int(f, 16); k = bisect.bisect_left(addrs, f); j = bisect.bisect_right(starts, f)
    end = starts[j] if j < len(starts) else f + 0x4000
    seen = []
    for a,_,m,o in insns[k:]:
        if a >= end: break
        for x in re.findall(r'0x[0-9a-f]{6}', o):
            s = s_at(int(x,16))
            if s and s not in seen: seen.append(s)
    print(f'== {f:#x}..{end:#x}'); [print('   ', s) for s in seen[:25]]
