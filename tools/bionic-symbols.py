# The names of the libraries of Bionic of API 26 (D454), for the stubs of the
# runtime on Android (P43): writes runtime/bionic/symbols_arm64.txt and
# symbols_x86_64.txt, in the format of runtime/glibc/symbols.txt, from the stubs of
# the sysroot of the NDK. Only the functions, and the variables of VARIABLES with
# their sizes: those of the stubs are not the real ones. An app reads environ
# (runtime/bionic.c), through the table of the addresses of libmain.so.
#
# Usage: python tools/bionic-symbols.py <ndk> [<llvm-readelf>]
#   <ndk> is a folder of the NDK, as android-ndk-r27; llvm-readelf is that of the
#   LLVM of the build when it is not given.
import os
import re
import subprocess
import sys

API = 26
LIBRARIES = ['libc.so', 'libm.so', 'libdl.so', 'liblog.so']
ARCHITECTURES = {'arm64': 'aarch64-linux-android', 'x86_64': 'x86_64-linux-android'}
VARIABLES = {'environ': 8}
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ndk = sys.argv[1]
    readelf = sys.argv[2] if len(sys.argv) > 2 else r'C:\software\llvm-23.1.4\bin\llvm-readelf.exe'
    prebuilt = os.path.join(ndk, 'toolchains', 'llvm', 'prebuilt')
    host = os.listdir(prebuilt)[0]
    folder = os.path.join(ROOT, 'runtime', 'bionic')
    os.makedirs(folder, exist_ok=True)
    line = re.compile(r'^\s*\d+:\s+[0-9a-f]+\s+\d+\s+(\w+)\s+(\w+)\s+\w+\s+(\w+)\s+(\S+)$')
    for arch, triple in ARCHITECTURES.items():
        rows = []
        for library in LIBRARIES:
            path = os.path.join(prebuilt, host, 'sysroot', 'usr', 'lib', triple, str(API), library)
            out = subprocess.run([readelf, '--dyn-syms', '--wide', path], capture_output=True, text=True, check=True).stdout
            for text in out.splitlines():
                match = line.match(text)
                if not match:
                    continue
                kind, bind, section, name = match.groups()
                if section == 'UND' or bind not in ('GLOBAL', 'WEAK'):
                    continue
                version = '-'
                if '@' in name:
                    name, version = name.split('@', 1)
                    version = version.lstrip('@')
                weak = 'weak-' if bind == 'WEAK' else ''
                if kind == 'FUNC':
                    rows.append('{} {} {}func 0 {}'.format(library, version, weak, name))
                elif kind == 'OBJECT' and name in VARIABLES:
                    rows.append('{} {} {}object {} {}'.format(library, version, weak, VARIABLES[name], name))
        rows.sort()
        with open(os.path.join(folder, 'symbols_{}.txt'.format(arch)), 'w', encoding='utf-8', newline='\n') as target:
            target.write('# The functions of Bionic of API {} for {}, from the stubs of the NDK, written by\n'.format(API, arch))
            target.write('# tools/bionic-symbols.py (P43). library version kind size name: version - for none.\n')
            for row in rows:
                target.write(row + '\n')
        print('{}: {} functions'.format(arch, len(rows)))


main()
