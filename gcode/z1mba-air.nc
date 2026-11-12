; z1mba connection test: air moves only, about 1 minute.
; Raises Z to the firmware's own clearance height (machine Z-1, 1 mm below the top),
; moves to the middle of the bed (machine X-100 Y-100) and draws four 10 mm squares
; there without moving Z. No spindle, no tool change.
G21
G90
G53 G0 Z-1
G53 G0 X-100 Y-100
G91
G1 X-10 F600
G1 Y-10
G1 X10
G1 Y10
G4 S10
G1 X-10 F600
G1 Y-10
G1 X10
G1 Y10
G4 S10
G1 X-10 F600
G1 Y-10
G1 X10
G1 Y10
G4 S10
G1 X-10 F600
G1 Y-10
G1 X10
G1 Y10
G4 S10
G90
M2
