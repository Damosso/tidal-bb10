"""Draws the app icon and action-bar glyphs as PNGs (no dependencies)."""
import os, struct, zlib

def png(path, size, pixel):
    ss = 3  # supersampling for smooth edges
    rows = []
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(ss):
                for sx in range(ss):
                    r, g, b, a = pixel((x + (sx + .5) / ss) / size, (y + (sy + .5) / ss) / size)
                    acc[0] += r * a; acc[1] += g * a; acc[2] += b * a; acc[3] += a
            n = ss * ss
            a = acc[3] / n
            row += bytes([int(acc[0] / acc[3]) if acc[3] else 0, int(acc[1] / acc[3]) if acc[3] else 0,
                          int(acc[2] / acc[3]) if acc[3] else 0, int(a * 255)])
        rows.append(bytes(row))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)

def tri_right(x, y, x0, x1, cy, h):   # triangle pointing right
    if not x0 <= x <= x1: return False
    return abs(y - cy) <= h * (x1 - x) / (x1 - x0)

def tri_left(x, y, x0, x1, cy, h):
    if not x0 <= x <= x1: return False
    return abs(y - cy) <= h * (x - x0) / (x1 - x0)

def box(x, y, x0, y0, x1, y1): return x0 <= x <= x1 and y0 <= y <= y1

SHAPES = {
    "play":  lambda x, y: tri_right(x, y, .34, .72, .5, .24),
    "pause": lambda x, y: box(x, y, .32, .28, .44, .72) or box(x, y, .56, .28, .68, .72),
    "next":  lambda x, y: tri_right(x, y, .28, .62, .5, .22) or box(x, y, .64, .28, .72, .72),
    "prev":  lambda x, y: tri_left(x, y, .38, .72, .5, .22) or box(x, y, .28, .28, .36, .72),
    "search": lambda x, y: (.17 ** 2 <= (x - .44) ** 2 + (y - .44) ** 2 <= .23 ** 2)
        or (abs((x - y)) < .045 and .60 <= x <= .78),
    "library": lambda x, y: box(x, y, .26, .30, .74, .36) or box(x, y, .26, .47, .74, .53)
        or box(x, y, .26, .64, .74, .70),
    "playing": lambda x, y: box(x, y, .28, .50, .38, .72) or box(x, y, .45, .28, .55, .72)
        or box(x, y, .62, .40, .72, .72),
}

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
for name, fn in SHAPES.items():
    png(os.path.join(root, "assets", "images", name + ".png"), 81,
        lambda x, y, fn=fn: (255, 255, 255, 1.0 if fn(x, y) else 0.0))

# Controls of the Playing screen: a filled round play/pause button and plain
# previous/next glyphs, each with a dimmer "pressed" version.
def control(name, size, glyph, fg, disc=None):
    def pixel(x, y):
        if glyph(x, y): return fg + (1.0,)
        if disc and (x - .5) ** 2 + (y - .5) ** 2 <= .49 ** 2: return disc + (1.0,)
        return (0, 0, 0, 0.0)
    png(os.path.join(root, "assets", "images", name + ".png"), size, pixel)

TEAL, TEAL_DIM, INK = (49, 208, 198), (33, 150, 143), (11, 13, 16)
big_play = lambda x, y: tri_right(x, y, .40, .70, .5, .19)
big_pause = lambda x, y: box(x, y, .35, .32, .45, .68) or box(x, y, .55, .32, .65, .68)
skip_next = lambda x, y: tri_right(x, y, .22, .60, .5, .26) or box(x, y, .64, .24, .74, .76)
skip_prev = lambda x, y: tri_left(x, y, .40, .78, .5, .26) or box(x, y, .26, .24, .36, .76)
control("ctl_play", 150, big_play, INK, TEAL)
control("ctl_play_down", 150, big_play, INK, TEAL_DIM)
control("ctl_pause", 150, big_pause, INK, TEAL)
control("ctl_pause_down", 150, big_pause, INK, TEAL_DIM)
control("ctl_next", 110, skip_next, (255, 255, 255))
control("ctl_next_down", 110, skip_next, (130, 140, 150))
control("ctl_prev", 110, skip_prev, (255, 255, 255))
control("ctl_prev_down", 110, skip_prev, (130, 140, 150))

def seg(x, y, x0, y0, x1, y1, w):   # thick line segment
    dx, dy = x1 - x0, y1 - y0
    t = max(0.0, min(1.0, ((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy)))
    return (x - x0 - t * dx) ** 2 + (y - y0 - t * dy) ** 2 <= w * w

shuffle = lambda x, y: (seg(x, y, .14, .30, .74, .70, .045) or seg(x, y, .14, .70, .74, .30, .045)
    or tri_right(x, y, .70, .90, .70, .13) or tri_right(x, y, .70, .90, .30, .13))
control("ctl_shuffle_on", 84, shuffle, TEAL)
control("ctl_shuffle_off", 84, shuffle, (130, 140, 150))

def home_glyph(x, y):
    roof = y <= .50 and abs(x - .5) <= (y - .20) * 1.05 and y >= .20
    body = box(x, y, .30, .48, .70, .78) and not box(x, y, .44, .60, .56, .78)
    return roof or body

def refresh_glyph(x, y):
    r2 = (x - .5) ** 2 + (y - .52) ** 2
    ring = .19 ** 2 <= r2 <= .26 ** 2 and not (x > .5 and y < .50)
    head = tri_right(x, y, .50, .70, .30, .12)
    return ring or head

for name, fn in (("home", home_glyph), ("refresh", refresh_glyph)):
    png(os.path.join(root, "assets", "images", name + ".png"), 81,
        lambda x, y, fn=fn: (255, 255, 255, 1.0 if fn(x, y) else 0.0))

def icon(x, y):
    d = max(abs(x - .5), abs(y - .5))
    if d > .46: return (0, 0, 0, 0.0)
    if tri_right(x, y, .36, .72, .5, .24): return (11, 13, 16, 1.0)
    return (49, 208, 198, 1.0)
png(os.path.join(root, "icon.png"), 114, icon)
print("icons written")
