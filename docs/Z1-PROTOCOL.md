# Makera Z1: what z1mba reads, and what was tested

Tested 2026-11-12 on a Z1 (`Makera_Z1_000000`, firmware 1.1.3.0.1.13) with `tools/z1probe.py`
running on two Linux PCs on the same LAN, and Makera Studio connected over Wi-Fi from a Windows
laptop. Machine name and addresses below are placeholders. The Studio session is replayed by
the tests from `tests/fixtures/studio-session.txt` (anonymised). Test jobs: `gcode/`.

## Results

- **The Z1 serves several TCP clients at once on port 2222.** A client holding the connection
  (1 s polls) kept every reply while a second machine connected, queried and left, three times.
  With Studio connected, 90 of 90 polite polls (connect, `?`, close, every 4 s) were answered,
  connect time median 13 ms, max 70 ms. Studio's console, jogging, two uploads and two jobs,
  including two tool changes, showed no errors. The "one client at a time" belief from BitCue
  and the community does not hold on this firmware.
- **The UDP busy flag is always 0**, also with Studio connected. Ignore it.
- **The UDP announcement carries the live state**: `Makera_Z1_000000,192.168.1.50,2222,0,Idle`,
  once a second and again on every change (`Idle`, `Run`, `Tool` seen). It caught both tool
  changes, including a 3.5 s one that the 4 s TCP poll missed. It is the trigger; TCP fills
  in the details.
- **Framed protocol only.** `86 68 | len | type | payload | CRC-16/XMODEM | 55 AA`, `?` as
  type `0xA1`, reply type `0x81`. Plain-text `?` was not needed and is untested.

## Status reply, as seen

```
<Idle|MPos:-1.0000,-1.0000,-1.0000,0.0000,0.0000|WPos:178.1910,37.8360,43.4366,-90.0000,0.0000
|F:0.0,2000.0,100.0|S:0.0,10000.0,100.0,0,22.1,22.9,0,0,0,0|T:4,1.423,-1|L:0, 0, 0, 0.0,100.0
|C:3,1,0,1|E:0,0,0,40,7610|OTA:0,0>
```

- `T:active,tool_length_offset,wanted`. `wanted` is `-1` until the first M6 of a job, then
  holds the last requested tool and is not reset. Only trust it in state `Tool` or right after
  it changes.
- `P:a,percent,seconds` is present only while a file is loaded/playing. The percentage is bytes
  read into the planner, so a small file shows 58-89 % from the first second. Useless for
  short jobs. The first field stayed at 7 through the whole tool-change job, so it is not a
  line count of what has run.
- `Idle` with a `P:` field means a job is loaded but stopped, e.g. during a dwell. `Idle`
  alone does not mean "no job".
- `MPos` reads `-1,-1,-1` (machine clearance position) also after moves. Use `WPos`.
- `E:` and `OTA:` are not in BitCue's or the firmware's field lists. Meaning unknown.

## A tool change, step by step (z1mba-toolchange.nc, `T2 M6`)

| Time | State (TCP) | UDP | `T:` | `P:` seconds | What happens |
|---|---|---|---|---|---|
| 06:41:04 | Idle | | `4,1.423,-1` | 5 | dwell before the change |
| 06:41:08 | Run | | `4,1.423,2` | 9 | M6 starts, head moves to the change position. **Wanted tool known here** |
| 06:41:15 | | Tool | | | stopped, beeps, Studio pops up its tool-change prompt |
| 06:41:16-20 | Tool | | `4,1.423,2` | 16, 16 | waiting for the front button; job clock stopped |
| 06:41:20 | | Run | | | button pressed |
| 06:41:24 | Run | | `-1,1.423,2` | 20 | measuring the new bit on the tool sensor |
| 06:41:32 | Run | | `2,1.380,2` | 28 | T2 active, new length offset |

The second change (T4) did the same; `Tool` lasted 3.5 s (06:42:00-06:42:03). The same
physical bit measured 1.423, 1.380 and 1.379 mm.

## Consequences for z1mba

- Listen on UDP 3333 for state changes. On `Run` → `Tool`, or on any change, poll TCP
  straight away for `T:` and `P:`, then keep polling every few seconds while a job is loaded.
- Show the bit to load as soon as `T:` names a new wanted tool. That's a few seconds before
  `Tool`.
- Several clients can poll at once, so z1mba and the ha-makera-z1 integration can both poll
  without locking out Studio.

## Not tested yet

- Alarm states and the `H:` field, pause/hold, a job that runs for minutes.
- `progress` (file name, remaining time) while Studio is connected.
- Reading the job file from the Z1 (FILE_* frames) to get the tool-change plan.
- Firmware versions other than 1.1.3.
