#!/usr/bin/env python3
# selmap.py END...  -- parse a Sound Input driver selector table ending (00000000 terminator) at END,
# and list which sound-hardware vectors (ExpandMem+$1AA table, disp/4) each handler reaches.
import struct, sys
d = open('/Users/timdoug/mame0289s/mame/roms/pmac6200/63abfd3f.bin', 'rb').read()
W = lambda a: struct.unpack('>H', d[a:a+2])[0]
SW = lambda a: struct.unpack('>h', d[a:a+2])[0]
SL = lambda a: struct.unpack('>i', d[a:a+4])[0]
IDIOM = bytes.fromhex('81e202b601aa')

def vectors_in(start, length):
    found = set()
    seg = d[start:start+length]
    i = seg.find(IDIOM)
    while i != -1:
        a = start + i + 6
        for k in range(0, 32, 2):
            w = W(a+k)
            if (w & 0xf1f8) == 0x2068:
                found.add(SW(a+k+2) // 4); break
        i = seg.find(IDIOM, i+1)
    return found

def calls_in(start, length):
    out = set()
    for a in range(start, start+length, 2):
        op = W(a)
        if op in (0x4eba,) or op == 0x6100: out.add(a+2+SW(a+2))
        elif (op & 0xff00) == 0x6100 and op & 0xff not in (0, 0xff): out.add(a+2+struct.unpack('b', bytes([op & 0xff]))[0])
    return out

def ends(start):  # crude: handler extent up to first rts/jmp-to-exit within 0x200
    for a in range(start, start+0x200, 2):
        if W(a) in (0x4e75,) or (W(a) == 0x6000 and a+2+SW(a+2) > a+0x40): return a+4-start
    return 0x200

for endarg in sys.argv[1:]:
    end = int(endarg, 16)
    a = end
    ents = []
    while all(0x20 <= c < 0x7f for c in d[a-6:a-2]):
        a -= 6
        ents.append((d[a:a+4].decode(), a+4+W(a+4) if W(a+4) < 0x8000 else a+4+SW(a+4)))
    print('table %X-%X' % (a, end))
    for sel, h in reversed(ents):
        n = ends(h)
        v = vectors_in(h, n)
        for c in calls_in(h, n):
            v |= vectors_in(c, ends(c))
        print('  %s -> %X  vectors %s' % (sel, h, sorted(v) if v else '-'))
