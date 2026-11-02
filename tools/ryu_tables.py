# Writes the tables of Ryu at the end of library/std/runtime.cro: the powers of 5 for
# the shortest digits of an f64 (Ulf Adams, "Ryu: fast float-to-string conversion",
# PLDI 2018), each in 125 bits as two words, the low one first, as constants of the
# module (D414, P25). Run it from the folder of the project:
#   python tools/ryu_tables.py
import os

BITS = 125
POW5 = 326      # 5^i shifted to 125 bits, for the negative exponents of 2
POW5_INV = 342  # 2^(len(5^i) - 1 + 125) / 5^i + 1, for the positive ones
MARK = '// ---- The tables of Ryu, written by tools/ryu_tables.py ----'


def table(name, values):
    lines = ['const u64[%d] %s = [' % (2 * len(values), name)]
    for value in values:
        lines.append('    0x%016X, 0x%016X,' % (value & (2**64 - 1), value >> 64))
    lines.append(']')
    return '\n'.join(lines)


split = []
for i in range(POW5):
    power = 5 ** i
    shift = power.bit_length() - BITS
    split.append(power >> shift if shift >= 0 else power << -shift)

inverse = []
for i in range(POW5_INV):
    power = 5 ** i
    inverse.append((1 << (power.bit_length() - 1 + BITS)) // power + 1)

path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'library', 'std', 'runtime.cro')
with open(path, encoding='utf-8', newline='') as source:
    text = source.read()
if MARK in text:
    text = text[:text.index(MARK)].rstrip('\n') + '\n'
text += ('\n' + MARK + '\n//\n// ryuPow5[2i] and ryuPow5[2i + 1] hold 5^i, and ryuPow5Inverse the inverse of 5^i, as\n'
         '// multipliers of 128 bits.\n\n' +
         table('ryuPow5', split) + '\n\n' + table('ryuPow5Inverse', inverse) + '\n')
with open(path, 'w', encoding='utf-8', newline='\n') as out:
    out.write(text)
print('wrote', os.path.normpath(path))
