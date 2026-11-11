"""Draws library/tools/design.ico, the icon of cromo design (D425): a window with a
component selected, in the colors of the logo of Cromo. Needs Pillow.

Usage: python tools/design-icon.py
"""
import os

from PIL import Image, ImageDraw

SIZE = 1024
SLATE = (43, 52, 66, 255)
SLATE_LIGHT = (70, 81, 100, 255)
PAPER = (237, 241, 244, 255)
WHITE = (255, 255, 255, 255)
RULE = (154, 167, 177, 255)
TEAL = (17, 166, 200, 255)


def draw():
    image = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    # The tile, with a lighter band at the top as the light on the logo.
    d.rounded_rectangle((40, 40, SIZE - 40, SIZE - 40), radius=210, fill=SLATE)
    # The window: its title bar and its content.
    left, top, right, bottom = 190, 250, 834, 774
    d.rounded_rectangle((left, top, right, bottom), radius=44, fill=PAPER)
    d.rounded_rectangle((left, top, right, top + 120), radius=44, fill=WHITE)
    d.rectangle((left, top + 76, right, top + 120), fill=WHITE)
    for k in range(3):  # the buttons of the title bar
        d.ellipse((right - 96 - 70 * k, top + 42, right - 60 - 70 * k, top + 78), fill=RULE)
    # A component, selected: its frame and its handles.
    a, b, c, e = 270, 470, 660, 640
    d.rounded_rectangle((a, b, c, e), radius=22, fill=WHITE, outline=RULE, width=8)
    d.rectangle((a - 26, b - 26, c + 26, e + 26), outline=SLATE_LIGHT, width=12)
    for x, y in ((a - 26, b - 26), (c + 26, b - 26), (a - 26, e + 26)):
        d.rectangle((x - 30, y - 30, x + 30, y + 30), fill=WHITE, outline=SLATE_LIGHT, width=12)
    d.ellipse((c + 26 - 58, e + 26 - 58, c + 26 + 58, e + 26 + 58), fill=TEAL)  # the one being dragged
    return image


if __name__ == '__main__':
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    target = os.path.join(root, 'library', 'tools', 'design.ico')
    icon = draw()
    sizes = [16, 24, 32, 48, 64, 128, 256]
    icon.resize((256, 256), Image.LANCZOS).save(target, sizes=[(s, s) for s in sizes])
    print(target)
