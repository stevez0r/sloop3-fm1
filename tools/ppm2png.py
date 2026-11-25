#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# The twin's screenshots (240 x 240 PPM) as PNG, each pixel doubled (sharp when the page scales it, up or down), for
# the user guide (tools/guide_shots.sh). No dependencies.
#   ppm2png.py IN.ppm... OUTDIR
import os, struct, sys, zlib
def png(path, w, h, rgb):
    raw = b''.join(b'\0' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    ch = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw, 9)) + ch(b'IEND', b''))
*ins, out = sys.argv[1:]
os.makedirs(out, exist_ok=True)
for f in ins:
    head, px = open(f, 'rb').read().split(b'\n', 1)
    _, w, h, _ = head.split(); w, h = int(w), int(h)
    rows = [b''.join(px[(y * w + x) * 3:(y * w + x) * 3 + 3] * 2 for x in range(w)) for y in range(h)]
    png(os.path.join(out, os.path.basename(f)[:-4] + '.png'), 2 * w, 2 * h, b''.join(r + r for r in rows))
