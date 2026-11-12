# z1mba todo

## Now
- [x] Probe the Z1: multi-client, UDP state, tool-change sequence (docs/Z1-PROTOCOL.md)
- [x] ESPHome component: UDP listener, status polls, tracker, entities; native tests
- [ ] Flash a Freenove S3 and watch a real job next to z1probe
- [ ] Home Assistant: notification automations (tool change with the bit, alarm, job done)

## Next
- [x] Choose the screen: 2.9" WeAct e-paper, black/white/red (296x128)
- [ ] E-paper package: tool-change screen with the bit as text + type pictogram, red for "act now"; start the refresh as soon as the wanted bit is known
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
