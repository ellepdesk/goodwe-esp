import sys, pickle, bisect
insns = pickle.load(open(sys.argv[1], 'rb'))
addrs = [i[0] for i in insns]
for a in sys.argv[2:]:
    a = int(a, 16); k = bisect.bisect_left(addrs, a)
    print('-----', hex(a))
    for ad, sz, m, op in insns[max(0,k-8):k+40]:
        print(f'{ad:08x}  {m:6} {op}')
