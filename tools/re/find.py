import sys, pickle, re
insns = pickle.load(open(sys.argv[1], 'rb'))
def has(op, v): return re.search(r'(?<![0-9a-fx])%s\b' % re.escape(v), op) is not None
win = 12
for idx, (a, sz, m, op) in enumerate(insns):
    if m.startswith('mov') and 'byte ptr' in op and has(op, '0xaa'):
        near = insns[idx:idx+win]
        if any('byte ptr' in o and has(o, '0x55') for _,_,_,o in near):
            tag = 'CC99' if any(has(o,'0xcc') for _,_,_,o in insns[idx-win:idx+win]) else ''
            print(hex(a), tag)
