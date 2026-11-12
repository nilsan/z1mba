; z1mba tool-change test: asks for T2, then T4. No spindle, no cutting.
; Same start as z1mba-air.nc. Each M6 parks the head and, after the front button,
; measures the bit on the tool sensor; Z goes back to clearance after each change.
G21
G90
G53 G0 Z-1
G53 G0 X-100 Y-100
G91
G1 X-10 F600
G1 X10
G4 S5
T2 M6
G53 G0 Z-1
G4 S5
T4 M6
G53 G0 Z-1
G4 S5
G90
M2
