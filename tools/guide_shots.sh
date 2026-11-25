#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The user guide's pictures, again, from the firmware itself, then the guide (docs/guide.html):
#   - the FM-1's screens: the PC twin's `guide` scenario (host/twin.c) -> docs/guide/screens/*.png
#   - the web editor's tabs (optional, needs playwright on NODE_PATH): tools/guide_editor_shots.js -> docs/guide/editor/*.jpg
#   tools/guide_shots.sh            (macOS: build/twin-mac; Linux: build/twin)
set -eu
cd "$(dirname "$0")/.."
if [ "$(uname)" = Darwin ]; then T=build/twin-mac; else T=build/twin; fi
if [ ! -f build/fw/fw.area ]; then                 # (no ARM toolchain: the area the installer page carries)
  mkdir -p build/fw
  python3 -c "import re,base64;t=open('index.html').read();open('build/fw/fw.area','wb').write(base64.b64decode(re.search(r'const AREA = Uint8Array.from\(atob\(\"([^\"]+)\"',t).group(1)))"
fi
make "$T"
rm -rf build/guide/tw && mkdir -p build/guide/tw
"$T" guide build/guide/tw - 1 > build/guide/tw/log.txt 2>&1 || { tail -20 build/guide/tw/log.txt; exit 1; }
rm -f docs/guide/screens/*.png
python3 tools/ppm2png.py build/guide/tw/*.ppm docs/guide/screens
if node -e "require('playwright')" 2>/dev/null; then
  node tools/guide_editor_shots.js "$T" build/guide/editor
  mkdir -p docs/guide/editor
  for f in build/guide/editor/*.png; do ffmpeg -loglevel error -y -i "$f" -vf scale=1440:-1 -q:v 4 "docs/guide/editor/$(basename "${f%.png}").jpg"; done
else
  echo "(playwright not found: the editor's pictures kept as they are)"
fi
python3 tools/mkguide.py
