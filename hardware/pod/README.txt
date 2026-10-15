DHVANI payload pod v2 - side-scan sonar hull section (version B, wires folded to <= 80 mm at the edges)
Outer size: 245 mm long (+2.5 mm connector collars), 202 mm diameter (+5 mm hull rings)

PRINT (PLA, 0.2 mm layers, 3 walls, 15% infill)
  DHVANI_Pod_lower.stl          flat underside down. Supports: tree, build-plate only (for the strip and ring ledges)
  DHVANI_Pod_upper.stl          cut face down. Supports: tree, on (inside the dome)
  DHVANI_Pod_saddle_PRINT_2.stl print TWO. Flat side down, no supports
  DHVANI_Pod_assembled_VIEW_ONLY.stl   for viewing / slides only - do not print

COLOURS
  Hull: yellow or orange. Transducer strips: paint black (or pause and swap filament).

HARDWARE
  4 x M3 x 10 self-tapping screws (dome to lower half, 2 per end cap)
  Double-sided foam tape (boards in pockets, buzzer behind the sound-hole grid)

EDIT
  Open DHVANI_Pod_FINAL_v2.scad in OpenSCAD, set PART at the top
  ("lower" / "upper" / "saddle" / "assembled"), press F6, File > Export > Export as STL.
