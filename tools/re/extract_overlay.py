# Unpacks the files appended to an InstallShield setup.exe (the "overlay" after the last
# PE section). Each entry is four NUL-terminated strings (name, path, version, decimal
# size) followed by that many raw bytes.
import os, sys, pefile

exe, out = sys.argv[1], sys.argv[2]
pe = pefile.PE(exe)
d = bytes(pe.__data__)
p = max(s.PointerToRawData + s.SizeOfRawData for s in pe.sections)
while p < len(d):
    fields = []
    for _ in range(4):
        e = d.index(b'\0', p)
        fields.append(d[p:e].decode('latin1'))
        p = e + 1
    name, path, ver, size = fields
    size = int(size)
    dst = os.path.join(out, path.replace('\\', '/'))
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, 'wb') as f:
        f.write(d[p:p + size])
    p += size
    print(f'{path:40} {size:>10}')
