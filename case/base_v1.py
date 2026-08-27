"""Tilted desk base for the CrowPanel 2.1" rotary display — FreeCAD headless.

Run:  /Applications/FreeCAD.app/Contents/Resources/bin/freecadcmd case/base_v1.py
Out:  case/export/base_v1.stl + case/export/spotify_knob_base.FCStd

Nest-thermostat posture, owner's pick: the face leans back TILT_DEG from
vertical. The knob's fixed lower band drops into a pocket sunk into the sloped
face; the rotating knob body must NEVER touch the base, which is why the
pocket is shallower than the fixed band.

Construction: one wedge (box minus a half-space whose plane IS the face),
then every cavity is cut along the single tilted axis vector N. No solids are
rotated after building — each cut is placed analytically. That keeps the
geometry auditable: the section render shows exactly the planned stack-up.

Geometry sources: STEP = Elecrow's 3D file; CAL-PENDING = awaiting the
owner's calipers — update and rerun, everything is parametric.

Print note: flat bottom on the bed. The sloped face is 55 degrees from
horizontal (printable). The pocket floor is a 35-degree-from-horizontal
annulus and wants slicer supports inside the pocket only — small, breaks out
clean. v2 can chamfer it away if it bothers.
"""

import math
import os

import FreeCAD as App
import Part
import Mesh
import MeshPart

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "export")
os.makedirs(OUT, exist_ok=True)

# ---- device, measured ------------------------------------------------------
# Caliper session 2026-08-26. The owner's #1 reading (47.87) matches the rear
# boss, not the front disc — the disc cannot be 47.87 (the 2.1" glass alone is
# ~53mm) and three independent sources put it at 79.0 (Elecrow STEP, mat-photo
# measurement, an Amazon reviewer's calipers). Bore stays 79.0-based; the
# first print's fit is the final arbiter.
BODY_DIA   = 79.00   # front disc — STEP + photo + reviewer agree; not re-measured
BAND_H     = 12.5    # CALIPER 2026-08-26 — matches STEP prediction exactly
BOSS_DIA   = 47.87   # CALIPER 2026-08-26 (STEP said 48.00)
BOSS_DEPTH = 19.5    # kept at STEP worst case; caliper read 11.93, but the
                     # cavity is a through-tunnel so extra depth is free
                     # clearance and under-depth is the only failure mode
CABLE_W    = 12.0    # CALIPER: plug is 9.03 wide; +3 routing slack

# ---- fit -------------------------------------------------------------------
BORE_CLR   = 0.35    # radial clearance (printed gauge ring refines this)
POCKET_D   = 9.0     # < BAND_H, so the rim stays clear of the rotating seam

# ---- base shape ------------------------------------------------------------
TILT_DEG   = 35.0    # face leans back this much from vertical
FOOT_W     = 96.0    # footprint width (x)
CENTER_H   = 52.0    # device center height above the desk
FACE_Y     = 72.0    # where the face plane meets the desk (front edge)
BACK_TOP_H = 64.0    # flat top height at the back of the wedge

BORE_R = BODY_DIA / 2 + BORE_CLR

a  = math.radians(TILT_DEG)
CA = math.cos(a)
SA = math.sin(a)

# Face plane: y*cos(a) + z*sin(a) = C, with outward normal N (toward user+up).
N = App.Vector(0, CA, SA)
C = FACE_Y * CA
# Device center sits ON the face plane at height CENTER_H:
P0 = App.Vector(0, (C - CENTER_H * SA) / CA, CENTER_H)

doc = App.newDocument("base")

# --- wedge: box minus the half-space in front of the face plane -------------
stock = Part.makeBox(FOOT_W, FACE_Y + 14, BACK_TOP_H,
                     App.Vector(-FOOT_W / 2, 0, 0))

# Cutter: a big box whose bottom face lies exactly on the face plane.
# Rotation self-check: the box's +Z (bottom-face normal) must land on N.
# rotate(X, angle) maps (0,0,1) -> (0, -sin(angle), cos(angle)); to reach
# N = (0, cos(a), sin(a)) the angle is -(90 - TILT_DEG). Getting this wrong
# by the 90-degree complement is exactly what mangled the first attempt, so
# the achieved normal is verified numerically below, not by eye.
cutter = Part.makeBox(FOOT_W + 20, 220, 160,
                      App.Vector(-(FOOT_W + 20) / 2, -110, 0))
cutter.rotate(App.Vector(0, 0, 0), App.Vector(1, 0, 0), -(90.0 - TILT_DEG))
cutter.translate(P0)

achieved = cutter.Placement.Rotation.multVec(App.Vector(0, 0, 1))
err = (achieved - N).Length
print(f"face normal achieved ({achieved.x:.3f},{achieved.y:.3f},{achieved.z:.3f}) "
      f"target ({N.x:.3f},{N.y:.3f},{N.z:.3f}) err {err:.4f}")
assert err < 1e-6, "cutter rotation does not produce the face plane"

wedge = stock.cut(cutter)

# --- pocket for the fixed band, cut along -N --------------------------------
def axis_cyl(radius, start_depth, length):
    """Cylinder along -N: starts start_depth BEHIND the face plane and runs
    length deeper. start_depth < 0 begins in front of the face (overcut)."""
    # rotate(X, 90 + TILT_DEG) maps the cylinder's +Z axis onto -N:
    # (0,0,1) -> (0, -sin(90+a), cos(90+a)) = (0, -cos(a), -sin(a)) = -N.
    cyl = Part.makeCylinder(radius, length, App.Vector(0, 0, 0))
    cyl.rotate(App.Vector(0, 0, 0), App.Vector(1, 0, 0), 90.0 + TILT_DEG)

    achieved = cyl.Placement.Rotation.multVec(App.Vector(0, 0, 1))
    err = (achieved + N).Length
    assert err < 1e-6, f"axis cylinder rotation wrong (err {err})"

    origin = P0 - N * start_depth
    cyl.translate(origin)
    return cyl

wedge = wedge.cut(axis_cyl(BORE_R,           -5.0, POCKET_D + 5.0))
wedge = wedge.cut(axis_cyl(BOSS_DIA / 2 + 2, POCKET_D, BOSS_DEPTH + 3.0))

# --- cable slot: from the boss cavity, down and out the back ----------------
# The boss cavity center sits at P0 - N*(POCKET_D + BOSS_DEPTH/2).
cav = P0 - N * (POCKET_D + BOSS_DEPTH / 2)
slot = Part.makeBox(CABLE_W, cav.y + 30, 30,
                    App.Vector(-CABLE_W / 2, -10, 2.0))
wedge = wedge.cut(slot)

obj = doc.addObject("Part::Feature", "base_v1")
obj.Shape = wedge
doc.recompute()

doc.saveAs(os.path.join(OUT, "spotify_knob_base.FCStd"))

mesh = doc.addObject("Mesh::Feature", "base_mesh")
mesh.Mesh = MeshPart.meshFromShape(Shape=wedge, LinearDeflection=0.08,
                                   AngularDeflection=0.4, Relative=False)
Mesh.export([mesh], os.path.join(OUT, "base_v1.stl"))

fb = wedge.BoundBox
print(f"base: {fb.XLength:.1f} x {fb.YLength:.1f} x {fb.ZLength:.1f} mm | "
      f"bore {BORE_R * 2:.2f} | pocket {POCKET_D} | tilt {TILT_DEG} | "
      f"device center at z={CENTER_H}")
print("exported:", os.path.join(OUT, "base_v1.stl"))
