"""Generate resources/app.ico (isometric cube with a dimension line) without external packages."""
import math, struct, zlib, sys, os

def png_bytes(w, h, rgba):
    raw = b''.join(b'\x00' + bytes(rgba[y*w*4:(y+1)*w*4]) for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')

def inside(poly, x, y):
    c = False
    for i in range(len(poly)):
        x1, y1 = poly[i]; x2, y2 = poly[(i+1) % len(poly)]
        if (y1 > y) != (y2 > y) and x < (x2-x1)*(y-y1)/(y2-y1) + x1: c = not c
    return c

def seg_dist(px, py, a, b):
    ax, ay = a; bx, by = b
    dx, dy = bx-ax, by-ay
    t = max(0, min(1, ((px-ax)*dx + (py-ay)*dy) / (dx*dx + dy*dy)))
    return math.hypot(px-(ax+t*dx), py-(ay+t*dy))

def render(size):
    ss = 4; S = size*ss
    s = S / 256.0
    c = (128*s, 118*s); r = 92*s
    hx = r*math.cos(math.radians(30)); hy = r*0.5
    top = [(c[0], c[1]-r), (c[0]+hx, c[1]-hy), (c[0], c[1]), (c[0]-hx, c[1]-hy)]
    left = [(c[0]-hx, c[1]-hy), (c[0], c[1]), (c[0], c[1]+r), (c[0]-hx, c[1]+hy)]
    right = [(c[0], c[1]), (c[0]+hx, c[1]-hy), (c[0]+hx, c[1]+hy), (c[0], c[1]+r)]
    faces = [(top, (126, 200, 255)), (left, (40, 120, 205)), (right, (22, 78, 150))]
    edges = [(top[i], top[(i+1)%4]) for i in range(4)] + [(left[3], left[2]), (left[2], right[2]), (right[1], right[2]), (c, left[2])]
    lw = max(1.0, 5*s) if size >= 32 else 1.2*ss
    # dimension line under the cube
    dy = c[1] + r + 22*s
    dim = [((c[0]-hx, dy), (c[0]+hx, dy)), ((c[0]-hx, dy-12*s), (c[0]-hx, dy+12*s)), ((c[0]+hx, dy-12*s), (c[0]+hx, dy+12*s))]
    dlw = max(1.0, 7*s) if size >= 24 else 1.5*ss
    buf = [0.0]*(S*S*4)
    for y in range(S):
        for x in range(S):
            px, py = x+0.5, y+0.5
            col = None
            for poly, fc in faces:
                if inside(poly, px, py): col = fc
            if size >= 24 and any(seg_dist(px, py, a, b) < dlw/2 for a, b in dim): col = (230, 40, 40)
            if col is not None and any(seg_dist(px, py, a, b) < lw/2 for a, b in edges): col = (10, 30, 60)
            if col is not None:
                i = (y*S+x)*4
                buf[i:i+4] = [col[0], col[1], col[2], 255]
    out = []
    for y in range(size):
        for x in range(size):
            acc = [0.0]*4
            for sy in range(ss):
                for sx in range(ss):
                    i = ((y*ss+sy)*S + x*ss+sx)*4
                    a = buf[i+3]
                    acc[0] += buf[i]*a; acc[1] += buf[i+1]*a; acc[2] += buf[i+2]*a; acc[3] += a
            a = acc[3]
            out += [int(acc[0]/a) if a else 0, int(acc[1]/a) if a else 0, int(acc[2]/a) if a else 0, int(a/(ss*ss))]
    return out

sizes = [16, 24, 32, 48, 64, 128, 256]
images = [png_bytes(sz, sz, render(sz)) for sz in sizes]
hdr = struct.pack('<HHH', 0, 1, len(sizes))
off = 6 + 16*len(sizes); entries = b''
for sz, img in zip(sizes, images):
    entries += struct.pack('<BBBBHHII', sz % 256, sz % 256, 0, 0, 1, 32, len(img), off); off += len(img)
out = sys.argv[1]
open(out, 'wb').write(hdr + entries + b''.join(images))
print('wrote', out, os.path.getsize(out), 'bytes')
