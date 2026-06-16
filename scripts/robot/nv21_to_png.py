#!/usr/bin/env python3
"""NV21 (YUV420 semi-planar, V then U) -> PNG. Runs on the workstation, not the robot.

Usage:  nv21_to_png.py <frame.nv21> <width> <height> <out.png>
Example (D10s Pro camera via camsiphon): nv21_to_png.py frame.nv21 672 504 frame.png

The raw buffer may be larger than W*H*3/2 (alloc padding) — only the leading planes are used.
"""
import sys
from PIL import Image


def clamp(v):
    return 0 if v < 0 else 255 if v > 255 else v


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        sys.exit(1)
    raw = open(sys.argv[1], "rb").read()
    W, H, out = int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    Y = raw
    C = raw[W * H:]                       # interleaved V,U plane (NV21)
    img = Image.new("RGB", (W, H))
    px = img.load()
    for y in range(H):
        for x in range(W):
            yy = Y[y * W + x]
            ci = (y // 2) * W + (x & ~1)  # 2x2 chroma subsampling
            v = C[ci]; u = C[ci + 1]      # NV21: V first, then U
            d, e = u - 128, v - 128
            px[x, y] = (
                clamp(yy + ((91881 * e) >> 16)),
                clamp(yy - ((22554 * d + 46802 * e) >> 16)),
                clamp(yy + ((116130 * d) >> 16)),
            )
    img.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main()
