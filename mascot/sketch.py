"""Mascot sketch: shapes are sculpted from round volumes, lit, then quantised
to 4-tone ramps. Run: python mascot/sketch.py"""
import math, os, struct, zlib

W, H, SCALE = 96, 128, 5
RAMPS = {
    'fur':   [(255, 255, 255), (228, 232, 246), (180, 187, 224), (134, 138, 196)],
    'acc':   [(255, 200, 112), (255, 138, 76), (214, 80, 84), (140, 46, 84)],
    'cloth': [(98, 214, 200), (44, 164, 170), (30, 108, 136), (24, 62, 96)],
    'dark':  [(120, 124, 150), (74, 78, 106), (48, 48, 74), (30, 26, 48)],
    'pink':  [(255, 190, 200), (240, 150, 170), (200, 110, 140), (150, 80, 120)],
}
OUT = (40, 28, 58)
BG = (58, 52, 84)
LIGHT = (0.45, -0.6, 0.66)
_l = math.sqrt(sum(c * c for c in LIGHT))
LIGHT = tuple(c / _l for c in LIGHT)

prims = []


def cap(part, mat, x0, y0, r0, x1, y1, r1, z):
    prims.append(('c', part, mat, (x0, y0, r0, x1, y1, r1, z)))


def ell(part, mat, cx, cy, rx, ry, z, rot=0.0):
    prims.append(('e', part, mat, (cx, cy, rx, ry, z, math.cos(rot), math.sin(rot))))


def chain(part, mat, pts, z):
    for a, b in zip(pts, pts[1:]):
        cap(part, mat, a[0], a[1], a[2], b[0], b[1], b[2], z)


def tail_mat(x, y):
    if y < 62 or (y < 69 and (int(x) + int(y)) % 2 == 0):
        return 'acc'
    return 'fur'


def torso_mat(x, y):
    return 'cloth' if 50 <= y < 63 else 'fur'


def leg_mat(x, y):
    return 'cloth' if y < 93 - abs(x - 46) * 0.25 else 'fur'


# --- tail (far back)
chain('tail', tail_mat, [(40, 84, 5), (28, 95, 10), (15, 85, 12), (12, 67, 10),
                         (17, 53, 5), (22, 45, 1.5)], -32)
cap('tail', tail_mat, 12, 60, 4, 5, 52, 1, -32)
# --- back limbs
chain('armB', 'fur', [(39, 53, 4.5), (31, 64, 4), (34, 75, 3.5)], -14)
chain('legB', leg_mat, [(40, 87, 8), (37, 106, 5.5), (39, 121, 3.5)], -12)
ell('legB', 'fur', 42, 124, 6, 3, -12)
# --- ears behind the head
cap('earL', 'fur', 44, 20, 5.5, 37, 5, 1, 0)
cap('earLi', 'pink', 43.5, 19, 3, 39, 9, 0.8, 3)
cap('earR', 'fur', 59, 18, 5.5, 64, 4, 1, 0)
cap('earRi', 'pink', 59.5, 17, 3, 63, 8, 0.8, 3)
# --- body
cap('neck', 'fur', 50, 41, 5, 49, 47, 5, -2)
cap('torso', torso_mat, 49, 52, 10, 47, 72, 7.5, 0)
ell('hips', 'cloth', 46, 82, 12, 9.5, -1)
ell('fluff', 'fur', 51, 47, 9, 5, 7)
cap('fluff', 'fur', 46, 49, 3, 43, 54, 1, 7)
cap('fluff', 'fur', 52, 50, 3, 52, 56, 1, 8)
cap('fluff', 'fur', 57, 49, 3, 60, 53, 1, 7)
# --- front leg
chain('legF', leg_mat, [(51, 86, 8), (55, 106, 5.5), (53, 121, 3.5)], 2)
ell('legF', 'fur', 56, 124, 6, 3, 2)
# --- head
ell('head', 'fur', 52, 30, 15, 13, 4)
cap('cheek', 'fur', 41, 35, 4, 34, 40, 1, 4)
cap('cheek', 'fur', 42, 39, 3.5, 37, 46, 1, 5)
ell('muzzle', 'fur', 64, 35.5, 7.5, 5, 9)
ell('nose', 'dark', 70.5, 33.5, 2.2, 1.7, 16)
# --- hair tuft
cap('hair', 'acc', 48, 18, 6, 60, 15, 3.5, 11)
cap('hair', 'acc', 58, 16, 4, 66, 21, 1.2, 12)
cap('hair', 'acc', 50, 20, 4, 57, 24, 1.2, 13)
cap('hair', 'acc', 46, 16, 4, 41, 10, 1.2, 10)
# --- headphones
chain('band', 'dark', [(41, 27, 2), (44, 16, 2), (53, 11, 2), (62, 12, 2)], 14)
ell('cup', 'dark', 42, 31, 4.5, 6.5, 14)
ell('cupc', 'cloth', 42, 31, 2.2, 3.6, 19)
# --- front arm, hand on hip
chain('armF', 'fur', [(59, 53, 4.5), (66, 65, 4), (60, 75, 3.5)], 9)
ell('armF', 'fur', 58.5, 76.5, 4, 3.5, 10)


def sample(px, py):
    best = None
    for kind, part, mat, p in prims:
        if kind == 'c':
            x0, y0, r0, x1, y1, r1, z = p
            dx, dy = x1 - x0, y1 - y0
            l2 = dx * dx + dy * dy
            t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((px - x0) * dx + (py - y0) * dy) / l2))
            cx, cy, r = x0 + t * dx, y0 + t * dy, r0 + t * (r1 - r0)
            d2 = (px - cx) ** 2 + (py - cy) ** 2
            if d2 >= r * r:
                continue
            h = math.sqrt(r * r - d2)
            zz, n = z + h, ((px - cx) / r, (py - cy) / r, h / r)
        else:
            cx, cy, rx, ry, z, c, s = p
            ux = ((px - cx) * c + (py - cy) * s) / rx
            uy = (-(px - cx) * s + (py - cy) * c) / ry
            d2 = ux * ux + uy * uy
            if d2 >= 1:
                continue
            h = math.sqrt(1 - d2)
            zz = z + h * min(rx, ry)
            n = (ux * c - uy * s, ux * s + uy * c, h)
        if best is None or zz > best[0]:
            m = mat(px, py) if callable(mat) else mat
            best = (zz, part, m, n)
    return best


grid = [[sample(x + 0.5, y + 0.5) for x in range(W)] for y in range(H)]
img = [[None] * W for _ in range(H)]


def at(x, y):
    return grid[y][x] if 0 <= x < W and 0 <= y < H else None


for y in range(H):
    for x in range(W):
        g = grid[y][x]
        nb = [at(x + 1, y), at(x - 1, y), at(x, y + 1), at(x, y - 1)]
        if g is None:
            if any(nb):
                img[y][x] = OUT
            continue
        zz, part, m, n = g
        d = sum(a * b for a, b in zip(n, LIGHT))
        tone = 0 if d > 0.72 else 1 if d > 0.45 else 2 if d > 0.15 else 3
        for q in nb:
            if q and q[1] != part and q[0] - zz > 3:
                tone = 3          # contact shadow under an overlapping part
        below = at(x, y + 1)
        if below and below[1] == part and below[2] != m and m == 'cloth':
            tone = min(3, tone + 1)   # hem line
        img[y][x] = RAMPS[m][tone]


def put(pts, col):
    for x, y in pts:
        img[y][x] = col


TEAL, TEAL_L, WHITE, LINE = (44, 164, 170), (98, 214, 200), (255, 255, 255), (96, 74, 128)
put([(x, 26) for x in range(54, 60)] + [(53, 27)], OUT)                 # near eye
put([(x, y) for x in range(55, 59) for y in range(27, 31)], TEAL)
put([(x, 31) for x in range(55, 59)], TEAL_L)
put([(x, y) for x in (56, 57) for y in (28, 29, 30)], OUT)
put([(55, 27), (56, 27)], WHITE)
put([(63, 26), (64, 26), (65, 26)], OUT)                                # far eye
put([(64, 27), (64, 28), (64, 29)], TEAL)
put([(65, 27), (65, 28), (65, 29)], OUT)
put([(61, 37), (62, 38), (63, 38), (64, 38), (65, 38), (66, 37)], LINE)  # smile
put([(54, 33), (55, 33), (56, 33), (57, 33)], RAMPS['pink'][0])         # blush


def png(path, w, h, rows):
    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


here = os.path.dirname(os.path.abspath(__file__))
png(os.path.join(here, 'sketch_1x.png'), W, H,
    [[v for c in row for v in ((*c, 255) if c else (0, 0, 0, 0))] for row in img])
big = []
for row in img:
    line = [v for c in row for _ in range(SCALE) for v in (*(c or BG), 255)]
    big.extend([line] * SCALE)
png(os.path.join(here, 'sketch_preview.png'), W * SCALE, H * SCALE, big)
print('ok')
