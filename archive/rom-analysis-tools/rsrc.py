#!/usr/bin/env python3
# rsrc.py FILE.bin [TYPE [ID]] -- list or dump resources from a MacBinary (hcopy -m) file.
#
# With no TYPE, lists every resource (type, id, size, name).  With TYPE and ID, writes that
# resource's bytes to stdout.  Types are the four-character codes, e.g. INIT, sdev, thng.
import struct, sys

def macbinary_forks(data):
    """Return (data_fork, resource_fork) from a MacBinary I/II file."""
    if len(data) < 128 or data[0] != 0 or data[74] != 0:
        raise SystemExit('not MacBinary')
    dlen = struct.unpack('>I', data[83:87])[0]
    rlen = struct.unpack('>I', data[87:91])[0]
    doff = 128
    roff = doff + ((dlen + 127) & ~127)
    return data[doff:doff + dlen], data[roff:roff + rlen]

def resources(rf):
    """Yield (type, id, name, bytes) for every resource in a resource fork."""
    data_off, map_off = struct.unpack('>II', rf[0:8])
    m = rf[map_off:]
    type_list_off, name_list_off = struct.unpack('>HH', m[24:28])
    ntypes = struct.unpack('>H', m[type_list_off:type_list_off + 2])[0] + 1
    for t in range(ntypes):
        e = type_list_off + 2 + t * 8
        rtype = m[e:e + 4].decode('mac_roman')
        count = struct.unpack('>H', m[e + 4:e + 6])[0] + 1
        ref_off = struct.unpack('>H', m[e + 6:e + 8])[0]
        for r in range(count):
            f = type_list_off + ref_off + r * 12
            rid = struct.unpack('>h', m[f:f + 2])[0]
            name_off = struct.unpack('>h', m[f + 2:f + 4])[0]
            doff = struct.unpack('>I', m[f + 4:f + 8])[0] & 0xffffff
            name = ''
            if name_off != -1:
                n = name_list_off + name_off
                name = m[n + 1:n + 1 + m[n]].decode('mac_roman', 'replace')
            dlen = struct.unpack('>I', rf[data_off + doff:data_off + doff + 4])[0]
            yield rtype, rid, name, rf[data_off + doff + 4:data_off + doff + 4 + dlen]

if __name__ == '__main__':
    data = open(sys.argv[1], 'rb').read()
    _, rf = macbinary_forks(data)
    if len(sys.argv) < 4:
        for rtype, rid, name, body in resources(rf):
            print(f'{rtype!r:8} {rid:6d} {len(body):7d}  {name}')
    else:
        for rtype, rid, name, body in resources(rf):
            if rtype == sys.argv[2] and rid == int(sys.argv[3]):
                sys.stdout.buffer.write(body)
