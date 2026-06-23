#!/usr/bin/env python3
"""
Raytrace lattice-wayland icon.
Two interlocking brass tori, 3/4 view, RGBA with transparent background.
"""
import numpy as np
from PIL import Image, ImageFilter

SIZE = 512   # render high-res, downsample for quality

# ── Camera: 3/4 view so both ring orientations clearly read as rings ──────────
eye    = np.array([1.5, 1.1, 2.0], dtype=np.float64)
target = np.array([0.0, 0.0, 0.0], dtype=np.float64)
fov_h  = 0.52   # ~30° half-angle keeps rings large in frame

fwd = target - eye;  fwd /= np.linalg.norm(fwd)
rgt = np.cross(fwd, [0, 1, 0]); rgt /= np.linalg.norm(rgt)
cup = np.cross(rgt, fwd)

u = np.linspace(-1, 1, SIZE) * np.tan(fov_h)
v = np.linspace( 1,-1, SIZE) * np.tan(fov_h)
ug, vg = np.meshgrid(u, v)
rays = fwd + ug[:,:,None]*rgt + vg[:,:,None]*cup
rays /= np.linalg.norm(rays, axis=2, keepdims=True)

# ── SDF helpers ───────────────────────────────────────────────────────────────
def rotx(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[1,0,0],[0,c,-s],[0,s,c]])

def roty(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[c,0,s],[0,1,0],[-s,0,c]])

def sdf_torus(p, R, r):
    """Torus in XY plane centred at origin. p: [..., 3]"""
    d_xy = np.sqrt(p[...,0]**2 + p[...,1]**2) - R
    return np.sqrt(d_xy**2 + p[...,2]**2) - r

# ── Scene: two interlocking rings (same centre, perpendicular planes) ─────────
R = 0.72   # major radius
r = 0.18   # tube radius

tori = [
    ([0.0, 0.0, 0.0], np.eye(3),      R, r),   # horizontal (XY plane)
    ([0.0, 0.0, 0.0], rotx(np.pi/2),  R, r),   # vertical   (XZ plane)
]

def scene_sdf(p):
    d = np.full(p.shape[:2], 1e9)
    for cen, rot, Rv, rv in tori:
        pl = (p - np.array(cen)) @ rot
        d  = np.minimum(d, sdf_torus(pl, Rv, rv))
    return d

# ── Ray march ─────────────────────────────────────────────────────────────────
pos0  = np.broadcast_to(eye, (SIZE, SIZE, 3)).copy()
t     = np.zeros((SIZE, SIZE))
hit   = np.zeros((SIZE, SIZE), bool)
min_d = np.full((SIZE, SIZE), 1e9)   # track nearest approach for AA

for _ in range(120):
    p = pos0 + t[:,:,None] * rays
    d = scene_sdf(p)
    min_d = np.minimum(min_d, d)
    active = ~hit & (t < 8.0) & (d > 3e-4)
    t   = np.where(active, t + np.abs(d), t)
    hit |= (d < 3e-4) & (t < 8.0)

# ── Normals ───────────────────────────────────────────────────────────────────
hp = pos0 + t[:,:,None] * rays
e  = 4e-4
ep = [np.zeros_like(hp) for _ in range(3)]
for ax in range(3): ep[ax][..., ax] = e
nx = scene_sdf(hp + ep[0]) - scene_sdf(hp - ep[0])
ny = scene_sdf(hp + ep[1]) - scene_sdf(hp - ep[1])
nz = scene_sdf(hp + ep[2]) - scene_sdf(hp - ep[2])
nrm = np.stack([nx, ny, nz], axis=2)
nrm /= np.linalg.norm(nrm, axis=2, keepdims=True).clip(1e-8)

# ── Brass shading (sphere-map simulation) ────────────────────────────────────
ne_x = np.einsum('ijk,k->ij', nrm, rgt)
ne_y = np.einsum('ijk,k->ij', nrm, cup)

sv = (ne_y + 1.0) * 0.5
su = (ne_x + 1.0) * 0.5

dark_  = np.array([0.30, 0.14, 0.02])
amber_ = np.array([0.72, 0.46, 0.05])
gold_  = np.array([1.00, 0.80, 0.12])
shine_ = np.array([1.00, 0.97, 0.85])

v3 = np.clip(sv, 0, 1)[..., None]
col = np.where(v3 < 0.25,
        dark_  + (amber_ - dark_)  * (v3 / 0.25),
      np.where(v3 < 0.60,
        amber_ + (gold_  - amber_) * ((v3 - 0.25) / 0.35),
      np.where(v3 < 0.85,
        gold_  + (shine_ - gold_)  * ((v3 - 0.60) / 0.25),
        shine_)))

# Horizontal sphere-map modulation
col *= 0.80 + 0.20 * np.cos((np.clip(su,0,1)[...,None] - 0.5) * np.pi)

# Tight specular highlight
spec = np.clip((sv - 0.82) / 0.16, 0, 1)[..., None]
col += spec * np.array([0.45, 0.42, 0.32])

col = np.clip(col, 0, 1)

# ── Alpha: opaque on hit, smooth anti-aliased edge, transparent elsewhere ─────
# SDF edge distance → smooth alpha at silhouette
edge_width = 0.025
alpha = np.where(hit, 1.0, np.clip(1.0 - min_d / edge_width, 0.0, 1.0))

# ── Composite RGBA ────────────────────────────────────────────────────────────
# Gamma encode
col_g = np.power(np.clip(col, 1e-10, 1), 1.0 / 2.2)

# For edge pixels: blend colour from nearest-ring colour (already computed)
rgb = (col_g * 255).astype(np.uint8)
a   = (alpha * 255).astype(np.uint8)

img = Image.fromarray(
    np.dstack([rgb, a]), 'RGBA'
)

# Very subtle inner glow (bloom only on the rings themselves, not into background)
glow_arr = np.array(img).astype(np.float32) / 255.0
bright   = np.clip((glow_arr[..., :3] - 0.60) * 2.5, 0, 1)
b_img    = Image.fromarray((bright * 255).astype(np.uint8), 'RGB')
b_blur   = np.array(b_img.filter(ImageFilter.GaussianBlur(radius=6))).astype(np.float32)/255
# Apply bloom only where ring is opaque
a_mask   = glow_arr[..., 3:4]
bloomed  = np.clip(glow_arr[..., :3] + b_blur * 0.20 * a_mask, 0, 1)

final_rgb = (np.power(bloomed, 1.0) * 255).astype(np.uint8)
final_a   = (glow_arr[..., 3] * 255).astype(np.uint8)
img = Image.fromarray(np.dstack([final_rgb, final_a]), 'RGBA')

# ── Save at multiple sizes ────────────────────────────────────────────────────
img.save('$HOME/lattice/lattice-icon-512.png')
print("Saved lattice-icon-512.png")

for sz in (256, 128, 64, 48, 32, 16):
    img.resize((sz, sz), Image.LANCZOS).save(f'$HOME/lattice/lattice-icon-{sz}.png')
    print(f"Saved lattice-icon-{sz}.png")
