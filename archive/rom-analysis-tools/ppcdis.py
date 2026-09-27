#!/usr/bin/env python3
# ppcdis.py VA COUNT  -- disassemble COUNT PowerPC words of the pmac6200 ROM at VA.
# Emits one line per word; words llvm-mc cannot decode are shown as .long so addresses stay in sync.
import subprocess, sys, struct
ROM = '/Users/timdoug/mame0289s/mame/roms/pmac6200/63abfd3f.bin'
MC = '/opt/homebrew/opt/llvm/bin/llvm-mc'
va = int(sys.argv[1], 16); n = int(sys.argv[2])
base = 0x40000000 if va < 0x40800000 else 0x40800000
data = open(ROM, 'rb').read()
off = va - base
for i in range(n):
    w, = struct.unpack('>I', data[off+4*i:off+4*i+4])
    b = ','.join('0x%02x' % x for x in w.to_bytes(4, 'big'))
    r = subprocess.run([MC, '--disassemble', '--triple=powerpc-unknown-unknown'], input=b, capture_output=True, text=True)
    lines = [l.strip() for l in r.stdout.splitlines() if l.strip() and not l.strip().startswith('.text')]
    print('%08X  %08X  %s' % (va+4*i, w, lines[0] if lines else '.long 0x%08x' % w))
