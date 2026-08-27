"""Fit-gauge ring for the CrowPanel 2.1" rotary display — FreeCAD headless.

Run:  /Applications/FreeCAD.app/Contents/Resources/bin/freecadcmd case/gauge_ring.py
Out:  case/export/gauge_ring.stl + case/export/spotify_knob_gauge.FCStd

Purpose: measure the real print-to-board fit BEFORE designing the cradle.
The claude-pet case (case/shell_v2.py in that repo) is the cautionary tale —
its v1 was modelled on unverified dimensions and printed >2mm off on both
axes. This gauge is minutes of printing; the shell is hours.

Geometry source: Elecrow's own STEP file (3D file/00-irad-21.stp), sliced at
0.5mm along the depth axis:

    body cylinder   79.00 mm dia, from y=-1.5 to y=+11  (the band a cradle grips)
    knob above it   77.90 mm dia
    rear boss       48.00 -> 23.60 -> 21.40 -> 16.90 mm steps

The 79.00 figure independently matches an Amazon reviewer's caliper reading
and disproves the listing's "5.3 cm".

Design: one ring, three stacked bore steps. Push the board in from the wide
end and note the step it seats at:

    step 1 (widest)  79.60  loose  -> cradle bore should be tighter
    step 2           79.35  target -> use this bore in the shell
    step 3 (tightest)79.15  snug   -> if it seats here, FDM shrinkage is low

Each step is 4mm tall and labelled by a notch count on the rim (1/2/3
notches, widest to tightest) so the steps stay identifiable off the printer.
Print flat, no supports.
"""

import os

import FreeCAD as App
import Part
import Mesh
import MeshPart

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "export")
os.makedirs(OUT, exist_ok=True)

# ---- measured (STEP file, verified against a caliper report) ----
BOARD_DIA = 79.00

# ---- gauge parameters ----
STEP_CLEARANCES = [0.60, 0.35, 0.15]   # widest first: bore = BOARD_DIA + this
STEP_H = 4.0                           # height of each bore step
WALL = 4.0                             # ring wall around the widest bore
NOTCH_W = 3.0                          # rim notch: width
NOTCH_D = 1.5                          # rim notch: radial depth
NOTCH_H = 2.0                          # rim notch: height (top face)
CHAMFER = 0.5                          # lead-in chamfer on the wide mouth

OD = BOARD_DIA + STEP_CLEARANCES[0] + 2 * WALL
TOTAL_H = STEP_H * len(STEP_CLEARANCES)

doc = App.newDocument("gauge")

# Outer body
body = Part.makeCylinder(OD / 2, TOTAL_H)

# Stepped bore, widest step at the bottom (z=0) so the print needs no support:
# each step going up is SMALLER, so every overhang is an internal ledge facing
# down onto the previous step — printable flat as-is when flipped? No: smaller
# bores above larger ones ARE bridgeable ledges. 0.125mm ledges bridge fine.
z = 0.0
for clearance in STEP_CLEARANCES:
    bore = Part.makeCylinder((BOARD_DIA + clearance) / 2, STEP_H + 0.01,
                             App.Vector(0, 0, z))
    body = body.cut(bore)
    z += STEP_H

# Lead-in chamfer at the wide mouth (z=0 face) so the board starts easily.
mouth_r = (BOARD_DIA + STEP_CLEARANCES[0]) / 2
cone = Part.makeCone(mouth_r + CHAMFER, mouth_r, CHAMFER, App.Vector(0, 0, 0))
body = body.cut(cone)

# Identification notches on the outer rim: N notches at the height band of
# step N (1 = widest/bottom). Cut as small radial boxes.
import math
for step_index in range(len(STEP_CLEARANCES)):
    n_notches = step_index + 1
    z_mid = STEP_H * step_index + (STEP_H - NOTCH_H) / 2
    for k in range(n_notches):
        ang = math.radians(k * (360.0 / max(n_notches, 1)))
        x = math.cos(ang) * (OD / 2)
        y = math.sin(ang) * (OD / 2)
        notch = Part.makeBox(NOTCH_D * 2, NOTCH_W, NOTCH_H)
        notch.translate(App.Vector(-NOTCH_D, -NOTCH_W / 2, 0))
        notch.rotate(App.Vector(0, 0, 0), App.Vector(0, 0, 1), math.degrees(ang))
        notch.translate(App.Vector(x, y, z_mid))
        body = body.cut(notch)

obj = doc.addObject("Part::Feature", "gauge_ring")
obj.Shape = body
doc.recompute()

doc.saveAs(os.path.join(OUT, "spotify_knob_gauge.FCStd"))

mesh = doc.addObject("Mesh::Feature", "gauge_mesh")
mesh.Mesh = MeshPart.meshFromShape(Shape=body, LinearDeflection=0.05,
                                   AngularDeflection=0.3, Relative=False)
Mesh.export([mesh], os.path.join(OUT, "gauge_ring.stl"))

bb = body.BoundBox
print(f"gauge ring: OD {bb.XLength:.2f} mm, H {bb.ZLength:.2f} mm")
print(f"bores (bottom->top): "
      + ", ".join(f"{BOARD_DIA + c:.2f}" for c in STEP_CLEARANCES))
print("exported:", os.path.join(OUT, "gauge_ring.stl"))
