import sys, pickle, bisect, re, collections
insns = pickle.load(open(sys.argv[1], 'rb'))
addrs = [i[0] for i in insns]
def func_start(a):
    k = bisect.bisect_left(addrs, a)
    while k > 0:
        k -= 1
        if insns[k][2] == 'push' and insns[k][3] == 'ebp' and insns[k+1][2] == 'mov' and insns[k+1][3] == 'ebp, esp':
            return insns[k][0]
calls = collections.defaultdict(list)
for ad, sz, m, op in insns:
    if m in ('call', 'jmp') and re.fullmatch(r'0x[0-9a-f]+', op): calls[int(op,16)].append(ad)
def callers(f):
    res = set(calls.get(f, []))
    # incremental-link thunks: jmp f
    for c in list(res):
        k = bisect.bisect_left(addrs, c)
        if insns[k][2] == 'jmp': res |= set(calls.get(c, []))
    return sorted(x for x in res if insns[bisect.bisect_left(addrs,x)][2]=='call')
def ctx(c, back=25):
    k = bisect.bisect_left(addrs, c)
    return [(a,m,o) for a,_,m,o in insns[max(0,k-back):k+1]]
for site in sys.argv[2:]:
    site = int(site, 16); f = func_start(site)
    cs = callers(f)
    print(f'== site {site:#x} in func {f:#x}, callers: {[hex(c) for c in cs]}')
    for c in cs:
        cf = func_start(c)
        pushes = [o for a,m,o in ctx(c,60) if m=='push' and re.fullmatch(r'0x[0-9a-f]+|\d+', o)]
        print(f'   caller {c:#x} in {cf:#x}: imm pushes {pushes[-12:]}')
