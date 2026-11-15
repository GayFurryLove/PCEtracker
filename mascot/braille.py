"""Braille line-art converter: each character is a 2x4 dot grid, so outlines
keep 8x the resolution of plain ASCII. Lines are solid, black fills are a
checker dither, orange markings a sparse diagonal.
Usage: python mascot/braille.py [image] [cols ...] [crop=l,t,r,b] [name=prefix]"""
import os, sys
import numpy as np
from PIL import Image, ImageFilter

here = os.path.dirname(os.path.abspath(__file__))
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, 'ref.webp')
args = sys.argv[2:]
crop = next(([float(v) for v in s[5:].split(',')] for s in args if s.startswith('crop=')), [0, 0, 1, 1])
name = next((s[5:] for s in args if s.startswith('name=')), 'braille')
sizes = [int(v) for v in args if v.isdigit()] or [60, 80]

a = np.asarray(Image.open(src).convert('RGB'), dtype=np.float32)
r, g, b = a[..., 0], a[..., 1], a[..., 2]
lum = a @ np.array([.299, .587, .114], dtype=np.float32)
orange = (r > 190) & (g > 90) & (g < 190) & (b < 120)
dark = (lum < 110) & ~orange
dm = Image.fromarray((dark * 255).astype('uint8'))
interior = np.asarray(dm.filter(ImageFilter.MinFilter(15)).filter(ImageFilter.MaxFilter(9))) > 127
line = dark & ~interior

ys, xs = np.where(dark | orange)
pad = 12
x0, x1 = max(0, xs.min() - pad), min(a.shape[1], xs.max() + pad)
y0, y1 = max(0, ys.min() - pad), min(a.shape[0], ys.max() + pad)
bw, bh = x1 - x0, y1 - y0
x0, x1 = int(x0 + crop[0] * bw), int(x0 + crop[2] * bw)
y0, y1 = int(y0 + crop[1] * bh), int(y0 + crop[3] * bh)

BITS = [[0x01, 0x08], [0x02, 0x10], [0x04, 0x20], [0x40, 0x80]]


def cover(mask, w, h):
    im = Image.fromarray((mask[y0:y1, x0:x1] * 255).astype('uint8'))
    return np.asarray(im.resize((w, h), Image.BOX), dtype=np.float32) / 255


def convert(cols):
    w = cols * 2
    rows = round(w * (y1 - y0) / (x1 - x0) / 4)
    h = rows * 4
    yy, xx = np.mgrid[0:h, 0:w]
    dots = cover(line, w, h) > 0.15
    dots |= (cover(interior, w, h) > 0.5) & ((xx + yy) % 2 == 0)
    dots |= (cover(orange, w, h) > 0.4) & ((xx + yy) % 3 == 0)
    out = []
    for ry in range(rows):
        s = ''
        for cx in range(cols):
            v = 0
            for dy in range(4):
                for dx in range(2):
                    if dots[ry * 4 + dy, cx * 2 + dx]:
                        v |= BITS[dy][dx]
            s += chr(0x2800 + v)
        out.append(s.rstrip('⠀'))
    return out, dots


for cols in sizes:
    lines, dots = convert(cols)
    with open(os.path.join(here, f'{name}_{cols}.txt'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    # preview: simulate a dark terminal, 8x16 px cell, one dot = 3 px on a 4 px pitch
    h, w = dots.shape
    im = np.full((h * 4 + 16, w * 4 + 16, 3), (18, 18, 24), dtype=np.uint8)
    for y, x in zip(*np.where(dots)):
        im[8 + y * 4:8 + y * 4 + 3, 8 + x * 4:8 + x * 4 + 3] = (225, 225, 230)
    Image.fromarray(im).save(os.path.join(here, f'{name}_{cols}_preview.png'))
    print(cols, 'x', len(lines))
