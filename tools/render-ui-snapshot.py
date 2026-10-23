"""Render exported ImGui triangles with the actual font/artwork atlas for visual QA."""
import argparse
import struct
from pathlib import Path
import numpy as np
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
with args.source.open("rb") as f:
    width, height, aw, ah, count = struct.unpack("<5I", f.read(20))
    atlas = np.frombuffer(f.read(aw * ah * 4), np.uint8).reshape(ah, aw, 4).astype(np.float32) / 255
    canvas = np.zeros((height, width, 3), np.float32)
    vertex_type = np.dtype([("pos", "<f4", 2), ("uv", "<f4", 2), ("color", "u1", 4)])
    for _ in range(count):
        texture, x0, y0, x1, y1, vertices = struct.unpack("<I4fI", f.read(24))
        mesh = np.frombuffer(f.read(vertices * 20), vertex_type)
        for n in range(0, vertices, 3):
            tri = mesh[n:n+3]
            pos, uv = tri["pos"], tri["uv"]
            left = max(0, int(np.floor(max(x0, pos[:, 0].min()))))
            top = max(0, int(np.floor(max(y0, pos[:, 1].min()))))
            right = min(width, int(np.ceil(min(x1, pos[:, 0].max()))))
            bottom = min(height, int(np.ceil(min(y1, pos[:, 1].max()))))
            if left >= right or top >= bottom:
                continue
            a, b, c = pos
            den = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(den) < 1e-6:
                continue
            yy, xx = np.mgrid[top:bottom, left:right].astype(np.float32)
            xx += .5; yy += .5
            wa = ((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/den
            wb = ((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/den
            wc = 1-wa-wb
            mask = (wa >= 0) & (wb >= 0) & (wc > 0)
            if not mask.any():
                continue
            weights = np.stack((wa[mask], wb[mask], wc[mask]), axis=1)
            coord = weights @ uv
            if texture == 2:
                sampled = np.column_stack((.08+coord[:, 0]*.22, .18+coord[:, 1]*.32, .45+coord[:, 0]*.30, np.ones(len(coord))))
            else:
                tx = np.clip(coord[:, 0]*aw-.5, 0, aw-1)
                ty = np.clip(coord[:, 1]*ah-.5, 0, ah-1)
                lx, ly = tx.astype(int), ty.astype(int)
                hx, hy = np.minimum(lx+1, aw-1), np.minimum(ly+1, ah-1)
                fx, fy = (tx-lx)[:, None], (ty-ly)[:, None]
                sampled = (atlas[ly, lx]*(1-fx)+atlas[ly, hx]*fx)*(1-fy)+(atlas[hy, lx]*(1-fx)+atlas[hy, hx]*fx)*fy
            rgba = sampled * (weights @ (tri["color"].astype(np.float32)/255))
            target = canvas[top:bottom, left:right]
            alpha = rgba[:, 3:4]
            target[mask] = rgba[:, :3]*alpha+target[mask]*(1-alpha)
args.output.parent.mkdir(parents=True, exist_ok=True)
Image.fromarray(np.clip(canvas*255, 0, 255).astype(np.uint8)).save(args.output)
print(args.output)
