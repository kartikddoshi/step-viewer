"""Render STEP thumbnails through StepThumb.dll (same code path Explorer uses) and save PNG previews.
usage: python scripts/test_thumb.py <build/out> <out_dir> <size> file1.step [file2.step ...]"""
import ctypes, os, sys, struct, zlib, time

out_dir_bin, out_dir, size = sys.argv[1], sys.argv[2], int(sys.argv[3])
os.add_dll_directory(os.path.abspath(out_dir_bin))
dll = ctypes.WinDLL(os.path.join(os.path.abspath(out_dir_bin), 'StepThumb.dll'))
fn = dll.RenderStepToBmp
fn.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_int]

def bmp_to_png(bmp_path, png_path):
    d = open(bmp_path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    h = abs(h)
    px = d[off:off + w * h * 4]
    rows = []
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            b, g, r, a = px[(y * w + x) * 4:(y * w + x) * 4 + 4]
            # composite premultiplied pixel over a light checkerboard so transparency is visible
            bg = 255 if ((x // 8 + y // 8) % 2) == 0 else 228
            row += bytes([r + bg * (255 - a) // 255, g + bg * (255 - a) // 255, b + bg * (255 - a) // 255])
        rows.append(bytes(row))
    def chunk(t, dd): return struct.pack('>I', len(dd)) + t + dd + struct.pack('>I', zlib.crc32(t + dd) & 0xffffffff)
    open(png_path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                               + chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b''))

os.makedirs(out_dir, exist_ok=True)
for f in sys.argv[4:]:
    base = os.path.splitext(os.path.basename(f))[0]
    bmp = os.path.join(out_dir, base + '.bmp')
    t = time.time()
    rc = fn(os.path.abspath(f), bmp, size)
    dt = time.time() - t
    if rc == 0:
        bmp_to_png(bmp, os.path.join(out_dir, base + '.png'))
        os.remove(bmp)
    print(f'{base}: rc={rc} {dt:.2f}s')
