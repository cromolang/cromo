# The monospaced font of std.gui (D492): writes third_party/jetbrains-mono, the
# regular and the semibold of JetBrains Mono NL, without ligatures, with all their
# characters but without what stb_truetype does not read: the hinting, the
# substitutions and the names of the glyphs. And its license.
#
# Usage: python tools/mono.py <folder>
#   <folder> is the release of JetBrains Mono, extracted from
#   https://github.com/JetBrains/JetBrainsMono/releases/download/v<version>/JetBrainsMono-<version>.zip:
#   the script reads fonts/ttf/JetBrainsMonoNL-Regular.ttf and -SemiBold.ttf, and OFL.txt.
#   It needs fontTools (pip install fonttools).
import os
import shutil
import sys

from fontTools import subset
from fontTools.ttLib import TTFont

FACES = ['Regular', 'SemiBold']


def main():
    if len(sys.argv) != 2:
        sys.exit('usage: python tools/mono.py <folder of the release of JetBrains Mono>')
    source = sys.argv[1]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    target = os.path.join(root, 'third_party', 'jetbrains-mono')
    os.makedirs(target, exist_ok=True)
    for face in FACES:
        name = 'JetBrainsMonoNL-%s.ttf' % face
        font = TTFont(os.path.join(source, 'fonts', 'ttf', name))
        options = subset.Options()
        options.hinting = False          # stb_truetype draws the outlines as they are
        options.glyph_names = False
        options.layout_features = []     # no substitutions: the font has no ligatures
        options.drop_tables += ['GSUB', 'GPOS', 'GDEF', 'DSIG', 'STAT', 'gasp']
        options.name_IDs = ['*']
        options.notdef_outline = True
        cut = subset.Subsetter(options)
        cut.populate(unicodes=font.getBestCmap().keys())
        cut.subset(font)
        font.save(os.path.join(target, name))
        print('%s: %d bytes' % (name, os.path.getsize(os.path.join(target, name))))
    shutil.copyfile(os.path.join(source, 'OFL.txt'), os.path.join(target, 'OFL.txt'))


main()
