"""The time zones that std.time carries: library/std/zoneinfo.dat (D533).

    python tools/zoneinfo.py <folder of zoneinfo> <version>

The folder holds the files of the IANA Time Zone Database as zic compiles them
(TZif, RFC 8536), one for each zone: /usr/share/zoneinfo of a Linux, or the folder
zoneinfo of the package tzdata of Python. The version is the one of the database,
as 2025b.

The file written:

    "CROZONE1"                  8 bytes
    the version                 8 bytes, with zeros at its end
    the number of the zones     4 bytes, little-endian as the numbers below
    for each zone, in the order of the names:
        the length of its name  1 byte
        its name
        where its TZif starts   4 bytes, from the start of the file
        the bytes of its TZif   4 bytes
    the TZif files, each one once: the zones that are a name for another share it
"""
import os
import struct
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    folder, version = sys.argv[1], sys.argv[2].encode()
    if len(version) > 8:
        sys.exit('the version is longer than 8 bytes')
    zones = {}
    for base, _, files in os.walk(folder):
        for name in files:
            path = os.path.join(base, name)
            with open(path, 'rb') as f:
                data = f.read()
            if data[:4] != b'TZif':
                continue
            zone = os.path.relpath(path, folder).replace(os.sep, '/')
            if zone.startswith(('posix/', 'right/')) or zone in ('localtime', 'posixrules', 'Factory'):
                continue
            zones[zone] = data
    names = sorted(zones)
    index = sum(1 + len(name.encode()) + 8 for name in names)
    at = 20 + index
    places = {}
    blobs = []
    for name in names:
        data = zones[name]
        if data not in places:
            places[data] = at
            blobs.append(data)
            at += len(data)
    out = bytearray(b'CROZONE1' + version.ljust(8, b'\0') + struct.pack('<I', len(names)))
    for name in names:
        data = zones[name]
        out += bytes([len(name.encode())]) + name.encode() + struct.pack('<II', places[data], len(data))
    for data in blobs:
        out += data
    target = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'library', 'std', 'zoneinfo.dat')
    with open(target, 'wb') as f:
        f.write(out)
    print(f'{len(names)} zones, {len(blobs)} files, {len(out)} bytes')


if __name__ == '__main__':
    main()
