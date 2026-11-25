#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# The user guide as one file: docs/guide/guide.src.html with its pictures inlined (data: URIs), so it opens from
# the disk, prints, and serves from anywhere. Screens: tools/guide_shots.sh.
#   mkguide.py [SRC] [OUT]
import base64, os, re, sys
src = sys.argv[1] if len(sys.argv) > 1 else 'docs/guide/guide.src.html'
out = sys.argv[2] if len(sys.argv) > 2 else 'docs/guide.html'
base = os.path.dirname(src)
TYPES = {'.png': 'image/png', '.jpg': 'image/jpeg', '.webp': 'image/webp'}
seen = {}
def inline(m):
    attr, path = m.group(1), m.group(2)
    ext = os.path.splitext(path)[1].lower()
    if ext not in TYPES or re.match(r'[a-z]+:', path): return m.group(0)
    f = os.path.normpath(os.path.join(base, path))
    if f not in seen: seen[f] = 'data:%s;base64,%s' % (TYPES[ext], base64.b64encode(open(f, 'rb').read()).decode())
    return '%s="%s"' % (attr, seen[f])
t = re.sub(r'\b(src|href)="([^"]+)"', inline, open(src).read())
t = t.replace('\n', '\n<!-- built from docs/guide/guide.src.html by tools/mkguide.py: edit the source, not this file -->\n', 1)
open(out, 'w').write(t)
print(out, len(t), 'bytes,', len(seen), 'pictures')
