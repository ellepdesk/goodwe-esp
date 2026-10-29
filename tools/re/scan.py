import sys, pefile, capstone, pickle
pe = pefile.PE(sys.argv[1])
base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True
md.skipdata = True
insns = []
for s in pe.sections:
    if not s.Characteristics & 0x20000000: continue
    code = s.get_data(); addr = base + s.VirtualAddress
    for i in md.disasm(code, addr):
        insns.append((i.address, i.size, i.mnemonic, i.op_str))
pickle.dump(insns, open(sys.argv[2], 'wb'))
print(len(insns))
