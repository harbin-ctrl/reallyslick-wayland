#!/usr/bin/env python3
"""
Raytrace an icon for lattice-wayland.
Renders interlocking brass tori viewed from inside the lattice,
matching the brassmesh preset's GL_SPHERE_MAP shading style.
"""
import numpy as np
from PIL import Image, ImageFilter

SIZE = 256

# ── Camera ────────────────────────────────────────────────────────────────────
eye    = np.array([0.18, 0.28, 1.8],  dtype=np.float64)
target = np.array([0.0,  0.05, -3.0], dtype=np.float64)
fov_h  = 0.68   # half-angle radians (~39°)

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

# ── Scene ─────────────────────────────────────────────────────────────────────
# Three pairs: (horizontal, vertical) at increasing depth.
# Vertical rings are rotated 90° around X; slight positional offsets add realism.
R  = 0.70   # major radius
r  = 0.155  # tube radius

tori = [
    # near pair  ─ large, fills the frame
    ([ 0.0,  0.0,  0.0 ], np.eye(3),      R, r),   # horizontal
    ([ 0.0,  0.0, -0.05], rotx(np.pi/2),  R, r),   # vertical

    # mid pair
    ([ 0.05, 0.0, -2.0 ], np.eye(3),      R, r),
    ([ 0.0,  0.05,-2.05], rotx(np.pi/2),  R, r),

    # far pair  ─ nearly swallowed by fog
    ([-0.05, 0.0, -4.0 ], np.eye(3),      R, r),
    ([ 0.0, -0.05,-4.05], rotx(np.pi/2),  R, r),
]

def scene_sdf(p):
    d = np.full(p.shape[:2], 1e9)
    for cen, rot, Rv, rv in tori:
        pl = (p - np.array(cen)) @ rot
        d  = np.minimum(d, sdf_torus(pl, Rv, rv))
    return d

# ── Ray march ─────────────────────────────────────────────────────────────────
pos0 = np.broadcast_to(eye, (SIZE, SIZE, 3)).copy()
t    = np.zeros((SIZE, SIZE))
hit  = np.zeros((SIZE, SIZE), bool)

for _ in range(96):
    p = pos0 + t[:,:,None] * rays
    d = scene_sdf(p)
    active = ~hit & (t < 10.0) & (d > 4e-4)
    t   = np.where(active, t + np.abs(d), t)
    hit |= (d < 4e-4) & (t < 10.0)

# ── Normals (central differences) ─────────────────────────────────────────────
hp = pos0 + t[:,:,None] * rays
e  = 4e-4
for ax in range(3):
    pass  # just silence lint; computed below
ep = [np.zeros_like(hp) for _ in range(3)]
for ax in range(3): ep[ax][..., ax] = e
nx = scene_sdf(hp + ep[0]) - scene_sdf(hp - ep[0])
ny = scene_sdf(hp + ep[1]) - scene_sdf(hp - ep[1])
nz = scene_sdf(hp + ep[2]) - scene_sdf(hp - ep[2])
nrm = np.stack([nx, ny, nz], axis=2)
nrm /= np.linalg.norm(nrm, axis=2, keepdims=True).clip(1e-8)

# ── Brass shading via sphere-map simulation ───────────────────────────────────
# Project normal into eye space → sphere-map (u,v)
ne_x = np.einsum('ijk,k->ij', nrm, rgt)
ne_y = np.einsum('ijk,k->ij', nrm, cup)

sv = (ne_y + 1.0) * 0.5   # 0..1 vertical sphere-map coord
su = (ne_x + 1.0) * 0.5   # 0..1 horizontal

# Brass colour palette (dark amber → gold → bright highlight)
dark_  = np.array([0.20, 0.09, 0.01])
amber_ = np.array([0.62, 0.38, 0.04])
gold_  = np.array([1.00, 0.77, 0.10])
shine_ = np.array([1.00, 0.97, 0.84])

v3 = np.clip(sv, 0, 1)[..., None]
col = np.where(v3 < 0.25,
        dark_  + (amber_ - dark_)  * (v3 / 0.25),
      np.where(v3 < 0.60,
        amber_ + (gold_  - amber_) * ((v3 - 0.25) / 0.35),
      np.where(v3 < 0.85,
        gold_  + (shine_ - gold_)  * ((v3 - 0.60) / 0.25),
        shine_)))

# Horizontal modulation from su (side highlights + shadows)
col *= 0.78 + 0.22 * np.cos((np.clip(su, 0, 1)[..., None] - 0.5) * np.pi)

# Slight specular pop at the very top of the sphere map
spec = np.clip((sv - 0.80) / 0.18, 0, 1)[..., None]
col  = col + spec * np.array([0.55, 0.50, 0.38])

# Fog: blend to black over depth
bg_col  = np.array([0.016, 0.007, 0.001])
fog_fac = np.clip((t - 1.2) / 6.5, 0, 1)[..., None]
col     = col * (1.0 - fog_fac) + bg_col * fog_fac

# ── Background ────────────────────────────────────────────────────────────────
ix = np.linspace(-1, 1, SIZE);  iy = np.linspace(-1, 1, SIZE)
xg, yg = np.meshgrid(ix, iy)
# Warm amber centre glow (suggests the lit lattice interior in the distance)
glow = np.exp(-(xg**2 + yg**2) * 3.5) * 0.055
bgc  = bg_col + np.stack([glow * 1.0, glow * 0.45, glow * 0.06], axis=2)

# ── Composite ─────────────────────────────────────────────────────────────────
out = np.where(hit[:,:,None], np.clip(col, 0, 1), bgc)

# Gamma encode (linear → sRGB)
out = np.power(np.clip(out, 1e-10, 1), 1.0 / 2.2)

arr = (out * 255).astype(np.uint8)
img = Image.fromarray(arr, 'RGB')

# Subtle bloom: extract bright pixels and add a soft halo
bright = np.clip((out - 0.55) * 2.5, 0, 1)
b_img  = Image.fromarray((bright * 255).astype(np.uint8), 'RGB')
b_blur = b_img.filter(ImageFilter.GaussianBlur(radius=4))
b_arr  = np.array(b_blur).astype(np.float32) / 255.0
final  = np.clip(arr.astype(np.float32)/255.0 + b_arr * 0.28, 0, 1)
img    = Image.fromarray((final * 255).astype(np.uint8), 'RGB')

img.save('$HOME/lattice/lattice-icon-256.png')
print("Saved lattice-icon-256.png")

# Downsample to common icon sizes
for sz in (128, 64, 48, 32, 16):
    img.resize((sz, sz), Image.LANCZOS).save(f'$HOME/lattice/lattice-icon-{sz}.png')
    print(f"Saved lattice-icon-{sz}.png")
