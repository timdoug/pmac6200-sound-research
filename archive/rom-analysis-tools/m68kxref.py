#!/usr/bin/env python3
# m68kxref.py TARGET...  -- find PC-relative bsr/bra/jsr/jmp/lea/pea to TARGET (ROM file offsets) in the pmac6200 ROM.
import struct, sys
d = open('/Users/timdoug/mame0289s/mame/roms/pmac6200/63abfd3f.bin', 'rb').read()
targets = {int(t, 16) for t in sys.argv[1:]}
def w(i): return struct.unpack('>H', d[i:i+2])[0]
def sw(i): return struct.unpack('>h', d[i:i+2])[0]
def sl(i): return struct.unpack('>i', d[i:i+4])[0]
for i in range(0, 0x300000, 2):
    op = w(i)
    hits = []
    if op & 0xff00 in (0x6100, 0x6000) and op & 0xff == 0: hits.append(i + 2 + sw(i+2))
    if op & 0xff00 in (0x6100, 0x6000) and op & 0xff == 0xff: hits.append(i + 2 + sl(i+2))
    if op & 0xff00 in (0x6100, 0x6000) and op & 0xff not in (0, 0xff): hits.append(i + 2 + struct.unpack('b', bytes([op & 0xff]))[0])
    if op in (0x4eba, 0x4efa, 0x487a) or (op & 0xf1ff) == 0x41fa: hits.append(i + 2 + sw(i+2))
    for h in hits:
        if h in targets: print('%X -> %X  op %04X' % (i, h, op))
