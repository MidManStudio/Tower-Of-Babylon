# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/weapons.md, section "tools/viewmodel_raster.py"
# ============================================================================
# Draws the triangles of a first person pose offline, from the player's eye (or from the side or above), with a depth
# buffer and near-plane clipping. Needs Python 3 with numpy and Pillow.
#   1. Build tests/weapons_headless.c (build line in its header) and run it with VM_DUMP=/tmp/vm: it writes /tmp/vm_<pose>.txt.
#   2. python3 tools/viewmodel_raster.py /tmp/vm shotgun_idle,rifle_fire /tmp/sheet.png cam,side,top
# import sys, math
import numpy as np
from PIL import Image, ImageDraw

W, H, FOV, NEAR = 800, 450, 60.0, 0.04

def load(path):
    tris = []
    for line in open(path):
        p = line.split()
        if not p or p[0] != 'T': continue
        v = list(map(float, p[1:10])); c = list(map(float, p[10:19]))
        tris.append(([np.array(v[0:3]), np.array(v[3:6]), np.array(v[6:9])], [np.array(c[0:3]), np.array(c[3:6]), np.array(c[6:9])]))
    return tris

def lookat(eye, target, up):
    f = np.array(target, float) - np.array(eye, float); f /= np.linalg.norm(f)
    r = np.cross(f, up); r /= np.linalg.norm(r)
    u = np.cross(r, f)
    return np.array(eye, float), r, u, f

def clip_near(vs, cs):
    # Sutherland-Hodgman against the near plane z_cam >= NEAR (z_cam is distance along the view direction)
    out_v, out_c = [], []
    n = len(vs)
    for i in range(n):
        a, b = vs[i], vs[(i + 1) % n]; ca, cb = cs[i], cs[(i + 1) % n]
        ina, inb = a[2] >= NEAR, b[2] >= NEAR
        if ina: out_v.append(a); out_c.append(ca)
        if ina != inb:
            t = (NEAR - a[2]) / (b[2] - a[2])
            out_v.append(a + (b - a) * t); out_c.append(ca + (cb - ca) * t)
    return out_v, out_c

def render(path, cam=None, bg=(28, 30, 36)):
    if cam is None: cam = lookat((0, 0, 0), (0, 0, -1), (0, 1, 0))
    eye, r, u, f = cam
    img = np.zeros((H, W, 3), np.float32); img[:] = bg
    zb = np.full((H, W), 1e9, np.float32)
    k = 1.0 / math.tan(math.radians(FOV) / 2.0); aspect = W / H
    for vs, cs in load(path):
        cam_v = [np.array([np.dot(v - eye, r), np.dot(v - eye, u), np.dot(v - eye, f)]) for v in vs]
        pv, pc = clip_near(cam_v, cs)
        if len(pv) < 3: continue
        for i in range(1, len(pv) - 1):
            tri = [pv[0], pv[i], pv[i + 1]]; col = [pc[0], pc[i], pc[i + 1]]
            pts = [((v[0] / v[2] * k / aspect * 0.5 + 0.5) * W, (0.5 - v[1] / v[2] * k * 0.5) * H, v[2]) for v in tri]
            (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = pts
            minx = max(int(math.floor(min(x0, x1, x2))), 0); maxx = min(int(math.ceil(max(x0, x1, x2))), W - 1)
            miny = max(int(math.floor(min(y0, y1, y2))), 0); maxy = min(int(math.ceil(max(y0, y1, y2))), H - 1)
            if minx > maxx or miny > maxy: continue
            den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
            if abs(den) < 1e-9: continue
            xs, ys = np.meshgrid(np.arange(minx, maxx + 1) + 0.5, np.arange(miny, maxy + 1) + 0.5)
            l0 = ((y1 - y2) * (xs - x2) + (x2 - x1) * (ys - y2)) / den
            l1 = ((y2 - y0) * (xs - x2) + (x0 - x2) * (ys - y2)) / den
            l2 = 1 - l0 - l1
            m = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not m.any(): continue
            z = l0 * z0 + l1 * z1 + l2 * z2
            sub = zb[miny:maxy + 1, minx:maxx + 1]
            m &= z < sub
            colr = l0[..., None] * col[0] + l1[..., None] * col[1] + l2[..., None] * col[2]
            sub[m] = z[m]
            img[miny:maxy + 1, minx:maxx + 1][m] = colr[m]
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))

VIEWS = {
    "cam":  lambda: lookat((0, 0, 0), (0, 0, -1), (0, 1, 0)),
    "side": lambda: lookat((1.7, -0.02, -0.55), (0.13, -0.02, -0.55), (0, 1, 0)),
    "top":  lambda: lookat((0.13, 1.6, -0.55), (0.13, 0.0, -0.55), (0, 0, -1)),
}

if __name__ == "__main__":
    prefix, names, out = sys.argv[1], sys.argv[2].split(','), sys.argv[3]
    views = sys.argv[4].split(',') if len(sys.argv) > 4 else ["cam"]
    tw, th = 600, 338
    sheet = Image.new('RGB', (len(views) * tw, len(names) * th), (0, 0, 0))
    for j, n in enumerate(names):
        for i, vname in enumerate(views):
            im = render(f"{prefix}_{n}.txt", VIEWS[vname]()).resize((tw, th), Image.LANCZOS)
            d = ImageDraw.Draw(im); d.text((8, 6), f"{n} [{vname}]", fill=(255, 255, 0))
            if vname == "cam":
                cx, cy = tw // 2, th // 2; d.line((cx - 8, cy, cx + 8, cy), fill=(255, 255, 255)); d.line((cx, cy - 8, cx, cy + 8), fill=(255, 255, 255))
            sheet.paste(im, (i * tw, j * th))
    sheet.save(out)
