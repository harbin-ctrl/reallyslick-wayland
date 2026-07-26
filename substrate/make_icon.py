#!/usr/bin/env python3
import os
import math
import random
from PIL import Image, ImageDraw

SIZE = 512
img = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)

# Draw a beautiful dark rounded rectangle as the icon base
box = [16, 16, 496, 496]
draw.rounded_rectangle(box, radius=90, fill=(32, 31, 33, 255), outline=(60, 57, 61, 255), width=6)

# Substrate simulator inside the box
w, h = SIZE, SIZE
cgrid = [10001] * (w * h)

# Warm amber/brown/gold colors
colors = [
    (156, 84, 43, 255), (157, 84, 50, 255), (157, 91, 53, 255), (147, 107, 54, 255),
    (170, 115, 48, 255), (196, 90, 39, 255), (217, 82, 35, 255), (216, 90, 32, 255),
    (219, 90, 35, 255), (229, 112, 55, 255), (131, 108, 75, 255), (140, 107, 75, 255),
    (130, 115, 92, 255), (147, 115, 82, 255), (129, 123, 99, 255), (129, 123, 109, 255),
    (217, 137, 59, 255), (228, 152, 50, 255), (223, 161, 51, 255), (229, 160, 55, 255),
    (240, 171, 59, 255)
]

class Crack:
    def __init__(self, x, y, t):
        self.x = x
        self.y = y
        self.t = t
        self.sandcolor = random.choice(colors)
        self.sandg = random.uniform(0.0, 0.2)
        self.active = True

cracks = []
def start_crack(cr):
    found = False
    for _ in range(1000):
        px = random.randint(40, 470)
        py = random.randint(40, 470)
        if cgrid[py * w + px] < 10000:
            found = True
            break
    if not found:
        px, py = int(cr.x), int(cr.y)
        if px < 40: px = 40
        if px >= 470: px = 470
        if py < 40: py = 40
        if py >= 470: py = 470
        cgrid[py * w + px] = int(cr.t)
    
    a = cgrid[py * w + px]
    if random.random() < 0.5:
        a -= 90 + random.uniform(-2, 2)
    else:
        a += 90 + random.uniform(-2, 2)
        
    cr.x, cr.y = px, py
    cr.t = a

# Seed initial cracks
for _ in range(8):
    cx = random.randint(60, 450)
    cy = random.randint(60, 450)
    ct = random.randint(0, 359)
    cr = Crack(cx, cy, ct)
    start_crack(cr)
    cracks.append(cr)

# Run simulation
for cycle in range(2500):
    for cr in cracks:
        if not cr.active:
            continue
            
        STEP_VAL = 0.42
        cr.x += STEP_VAL * math.cos(math.radians(cr.t))
        cr.y += STEP_VAL * math.sin(math.radians(cr.t))
        
        cx, cy = int(cr.x), int(cr.y)
        dx = cx - 256
        dy = cy - 256
        if dx*dx + dy*dy > 200*200:
            start_crack(cr)
            continue
            
        if 0 <= cx < w and 0 <= cy < h:
            # Draw crack point with a high-contrast crack line color (off-white)
            draw.point((cx, cy), fill=(240, 240, 245, 255))
            
            # Draw sand grains perpendicularly
            rx, ry = cr.x, cr.y
            openspace = True
            while openspace:
                rx += 0.81 * math.sin(math.radians(cr.t))
                ry -= 0.81 * math.cos(math.radians(cr.t))
                ccx, ccy = int(rx), int(ry)
                cdx = ccx - 256
                cdy = ccy - 256
                if cdx*cdx + cdy*cdy > 200*200:
                    openspace = False
                    break
                if 0 <= ccx < w and 0 <= ccy < h:
                    if cgrid[ccy * w + ccx] > 10000:
                        pass
                    else:
                        openspace = False
                else:
                    openspace = False
            
            cr.sandg += random.uniform(-0.05, 0.05)
            cr.sandg = max(0.0, min(1.0, cr.sandg))
            grains = 32
            w_val = cr.sandg / (grains - 1)
            for i in range(grains):
                drawx = cr.x + (rx - cr.x) * math.sin(math.sin(i * w_val))
                drawy = cr.y + (ry - cr.y) * math.sin(math.sin(i * w_val))
                alpha = int((0.1 - i / (grains * 10.0)) * 255)
                if alpha > 0:
                    sc = cr.sandcolor
                    draw.point((int(drawx), int(drawy)), fill=(sc[0], sc[1], sc[2], alpha))
            
            if cgrid[cy * w + cx] > 10000 or abs(cgrid[cy * w + cx] - cr.t) < 5:
                cgrid[cy * w + cx] = int(cr.t)
            elif abs(cgrid[cy * w + cx] - cr.t) > 2:
                start_crack(cr)
                if len(cracks) < 30:
                    new_cr = Crack(cx, cy, random.randint(0, 359))
                    start_crack(new_cr)
                    cracks.append(new_cr)
        else:
            start_crack(cr)

# Write beside this script unless told otherwise
OUT = os.environ.get('ICON_OUT', os.path.dirname(os.path.abspath(__file__)))

img.save(os.path.join(OUT, 'substrate-icon-512.png'))
print("Saved substrate-icon-512.png")

for sz in (256, 128, 64, 48, 32, 16):
    img.resize((sz, sz), Image.Resampling.LANCZOS).save(os.path.join(OUT, f'substrate-icon-{sz}.png'))
    print(f"Saved substrate-icon-{sz}.png")
