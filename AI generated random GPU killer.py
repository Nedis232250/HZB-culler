import random
import sys
import math

def triangle(cx, cy, cz, sx, sy, r, g, b, a):
    angle = random.uniform(0, math.pi)
    p1 = (cx + sx * math.cos(angle),         cy + sy * math.sin(angle))
    p2 = (cx + sx * math.cos(angle + 2.094), cy + sy * math.sin(angle + 2.094))
    p3 = (cx + sx * math.cos(angle + 4.189), cy + sy * math.sin(angle + 4.189))
    lines = []
    for p in [p1, p2, p3]:
        lines.append(f"{p[0]}, {p[1]}, {cz}, {r}, {g}, {b}, {a},")
    return "\n".join(lines) + "\n"

SMALL_SX = 0.003
SMALL_SY = 0.005

num_triangles = 5000000
num_occluders = 50
lines = []

occluders = []
for _ in range(num_occluders):
    occluders.append({
        'cx': random.uniform(-0.5, 0.5),
        'cy': random.uniform(-0.5, 0.5),
        'sx': random.uniform(0.3, 0.6),
        'sy': random.uniform(0.2, 0.5),
    })

for occ in occluders:
    steps = 30
    for xi in range(steps):
        for yi in range(steps):
            cx = occ['cx'] + (xi / steps - 0.5) * occ['sx'] * 2
            cy = occ['cy'] + (yi / steps - 0.5) * occ['sy'] * 2
            z = random.uniform(0.0, 1.0)
            lines.append(triangle(cx, cy, z, SMALL_SX, SMALL_SY, 0.7, 0.7, 0.7, 1.0) + "\n")

remaining = num_triangles - len(lines)
for i in range(remaining):
    if i % 10000 == 0:
        print(f"\r{100*i/remaining:.1f}%", end="")
        sys.stdout.flush()
    cx = random.uniform(-0.95, 0.95)
    cy = random.uniform(-0.95, 0.95)
    z = random.uniform(0.0, 1.0)
    r, g, b = random.uniform(0.3,1), random.uniform(0.3,1), random.uniform(0.3,1)
    lines.append(triangle(cx, cy, z, SMALL_SX, SMALL_SY, r, g, b, 1.0) + "\n")

random.shuffle(lines)

with open("hello.world", "w") as f:
    f.writelines(lines)

print("\ndone")
input()
