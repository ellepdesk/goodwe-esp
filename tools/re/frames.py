import sys, pickle, re, bisect, collections
insns = pickle.load(open(sys.argv[1], 'rb'))
store = re.compile(r'byte ptr \[(\w+)(?: ([+-]) (0x[0-9a-f]+|\d+))?\], (.+)$')
out = []
for idx, (a, sz, m, op) in enumerate(insns):
    if not m.startswith('mov'): continue
    s = store.match(op)
    if not s or s.group(4) != '0xaa': continue
    disp = int(s.group(3), 0) * (-1 if s.group(2) == '-' else 1) if s.group(3) else 0
    nxt = insns[idx+1:idx+3]
    if not any(re.search(r', 0x55$', o) for _,_,_,o in nxt): continue
    vals = {}
    for ad, _, mm, oo in insns[idx:idx+120]:
        t = store.match(oo) if mm.startswith('mov') else None
        if not t: continue
        d = int(t.group(3), 0) * (-1 if t.group(2) == '-' else 1) if t.group(3) else 0
        off = d - disp
        if 0 <= off < 40 and off not in vals: vals[off] = t.group(4)
    hdr = [vals.get(i, '?') for i in range(7)]
    ln = hdr[6]
    data = [vals.get(7+i, '?') for i in range(int(ln,0))] if re.fullmatch(r'0x[0-9a-f]+|\d+', ln) and int(ln,0) < 30 else []
    out.append((a, hdr, data))
for a, h, d in out:
    print(hex(a), ' '.join(h[2:]), '|', ' '.join(d))
