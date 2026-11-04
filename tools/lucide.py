# The icons of std.gui (D448, D449): writes third_party/lucide/lucide-icons.ttf, a
# part of the font of Lucide with the icons of ICONS, its license, and the enum
# gui.Icon in library/std/gui.cro, between its two marks.
#
# Usage: python tools/lucide.py <folder>
#   <folder> is the package lucide-static, extracted from
#   https://registry.npmjs.org/lucide-static/-/lucide-static-<version>.tgz: the
#   script reads font/lucide.ttf, font/codepoints.json, LICENSE and package.json.
#   It needs fontTools (pip install fonttools).
import json
import os
import sys

from fontTools import subset
from fontTools.ttLib import TTFont

ICONS = '''
user users user-plus user-check contact
mail phone message-circle message-square send bell bell-off at-sign inbox
house menu search arrow-left arrow-right arrow-up arrow-down
chevron-left chevron-right chevron-up chevron-down
ellipsis ellipsis-vertical external-link log-in log-out x check plus minus
settings pencil trash-2 save download upload share-2 copy clipboard refresh-cw rotate-ccw
funnel sliders-horizontal printer link paperclip
heart star bookmark tag calendar clock alarm-clock map-pin map globe camera image video
music mic file file-text folder folder-open archive
package shopping-cart shopping-bag credit-card wallet gift truck briefcase building-2
key lock lock-open shield eye eye-off
info circle-alert triangle-alert circle-question-mark circle-check circle-x ban loader
wifi wifi-off battery bluetooth
play pause square skip-forward skip-back volume-2 volume-x
sun moon cloud zap flame droplet thermometer umbrella
chart-column chart-line chart-pie list layout-grid table database server cpu monitor
smartphone laptop code terminal layers
thumbs-up thumbs-down smile flag award trophy lightbulb book book-open graduation-cap
coffee utensils car plane bike activity history power qr-code scan
'''.split()

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BEGIN = '// ---- The icons (D448, D449), written by tools/lucide.py ----'
END = '// ---- The end of the icons ----'


def caseName(name):
    # message-circle -> MessageCircle, trash-2 -> Trash2
    return ''.join(part[:1].upper() + part[1:] for part in name.split('-'))


def main():
    package = sys.argv[1]
    codepoints = json.load(open(os.path.join(package, 'font', 'codepoints.json'), encoding='utf-8'))
    version = json.load(open(os.path.join(package, 'package.json'), encoding='utf-8'))['version']
    missing = [name for name in ICONS if name not in codepoints]
    if missing:
        sys.exit('not in Lucide {}: {}'.format(version, ', '.join(missing)))
    if len(set(ICONS)) != len(ICONS):
        sys.exit('an icon is twice in ICONS')

    folder = os.path.join(ROOT, 'third_party', 'lucide')
    os.makedirs(folder, exist_ok=True)
    font = TTFont(os.path.join(package, 'font', 'lucide.ttf'))
    options = subset.Options()
    options.hinting = False
    options.layout_features = []
    options.name_IDs = ['*']
    options.notdef_outline = True
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=[codepoints[name] for name in ICONS])
    subsetter.subset(font)
    font.save(os.path.join(folder, 'lucide-icons.ttf'))
    with open(os.path.join(package, 'LICENSE'), encoding='utf-8') as source:
        license = source.read()
    with open(os.path.join(folder, 'LICENSE'), 'w', encoding='utf-8', newline='\n') as target:
        target.write(license)

    lines = [BEGIN,
             '',
             '// An icon of Lucide {} (D448): its value is its character in the font of the'.format(version),
             '// icons. None, the default, is no icon.',
             'public enum Icon {',
             '    None = 0']
    for name in sorted(ICONS, key=caseName):
        lines.append('    {} = 0x{:X}'.format(caseName(name), codepoints[name]))
    lines += ['}', '', END]
    path = os.path.join(ROOT, 'library', 'std', 'gui.cro')
    with open(path, encoding='utf-8') as source:
        text = source.read()
    if BEGIN not in text or END not in text:
        sys.exit('gui.cro has not the marks of the icons')
    start = text.index(BEGIN)
    end = text.index(END) + len(END)
    text = text[:start] + '\n'.join(lines) + text[end:]
    with open(path, 'w', encoding='utf-8', newline='\n') as target:
        target.write(text)
    print('{} icons of Lucide {}, {} bytes'.format(len(ICONS), version,
                                                os.path.getsize(os.path.join(folder, 'lucide-icons.ttf'))))


main()
