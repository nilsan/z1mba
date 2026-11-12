# z1mba todo

## Now
- [x] Probe the Z1: multi-client, UDP state, tool-change sequence (docs/Z1-PROTOCOL.md)
- [x] ESPHome component: UDP listener, status polls, tracker, entities; native tests
- [ ] Hardware test on our kit (Freenove S3 + WeAct 2.9"), simplest first, each against the gcode/ air jobs:
  - [x] 1. `z1mba-host.yaml` on a PC (no ESP32)
  - [ ] 2. `z1mba-standalone.yaml` without its `screen:` line: the ESP32 alone, logs + status page next to z1probe
  - [ ] 3. `z1mba-standalone.yaml` with the 2.9" screen
  - [ ] 4. `z1mba.yaml`: the screen plus Home Assistant
- [ ] Home Assistant: notification automations (tool change with the bit, alarm, job done)

## Next
- [x] Choose the screen: 2.9" WeAct e-paper, black/white/red (296x128)
- [x] E-paper package: tool-change screen, red for "act now", redraw as soon as the wanted bit holds
- [x] Open for other screen sizes: layout from the display's size, tested natively at a few e-paper sizes (only the 2.9" ships and is tested on hardware)
- [x] Runs without Home Assistant: HA is a package; standalone config with a status page
- [ ] On the real 2.9" (step 3): pins, colours, refresh time, fonts that read across the room
- [ ] Bit type pictogram (flat, ball, V-bit, drill...) next to the number, once the type is known
- [ ] Screen preview on the host build (ESPHome `sdl` display) to see the real fonts without hardware
- [ ] Bit type and size per T-number: read the ;@MKR|TOOL header of the running job from the Z1 (FILE_* transfer, header only)
- [ ] Bit names/types shared with cnc-facing's catalogue (flat, ball, engraving, corn, drill, chamfer)
- [-] ~~Tool-change screen in the bit's colour~~ dropped: e-paper, and type + size as text says more than a colour
- [ ] Test: alarm and H: codes, pause/hold, a long job, `progress` (file name, time left)
- [ ] Job plan: next bit in N min (read the file from the Z1, or get it from cnc-facing)
- [ ] Bit life in Home Assistant (cut time per bit)

## Logo
- [ ] Zimba silhouette from open elevation data (pick the viewing direction), simplified SVG
- [ ] Use it on the boot screen, README and HA device
- [ ] Mill it on the Z1: V-bit outline, raised silhouette, or 3D relief

## Upstream / elsewhere
- [ ] ha-makera-z1: offer a PR exposing the wanted tool (T: third field)
- [ ] cnc-facing checker: `G4 S10` is read as spindle speed S10 (false rpm-low warning)
