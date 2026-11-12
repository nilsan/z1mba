# z1mba

<img src="assets/logo/z1mba-mark.svg" alt="The Zimba's silhouette" width="280">

[![build](https://github.com/nilsan/z1mba/actions/workflows/build.yml/badge.svg)](https://github.com/nilsan/z1mba/actions/workflows/build.yml)

**Z1 Multi-Bit Assistant.** An ESP32 that follows a [Makera Z1](https://www.makera.com/)
desktop CNC over the LAN and tells you which bit to load at a tool change, on its own
e-paper screen and, if you use it, in Home Assistant.

Named after the Zimba (2643 m) in Vorarlberg, Austria, "the Matterhorn of Vorarlberg",
with a 1 for the Z1. The logo is its skyline as seen from the Saulakopf.

z1mba takes the idea of [BitCue](https://github.com/journeyonlabs/bitcue), a Raspberry
Pi job screen for the Z1, to a cheap ESP32 running [ESPHome](https://esphome.io), and
can hand notifications to [Home Assistant](https://www.home-assistant.io). See
[Credits and inspiration](#credits-and-inspiration).

> **Written with Claude.** The code, tests and docs in this repository were written by
> [Claude Code](https://claude.com/claude-code), Anthropic's AI coding agent, working with
> the author: the author sets the direction, runs the tests on a real Z1 and reviews what
> goes in. Treat it like any other young open source project and check it against your own
> machine.

> **Status: early.** The component is tested against a real Z1 (as a Linux program) and
> compiles for the ESP32-S3, but has not run on ESP32 hardware yet. The screen layout is
> tested at every preset size, but has not been on a real e-paper yet. Not affiliated with
> Makera.

## How it works

The Z1 talks to the LAN in two ways, and z1mba listens to both. Details, test logs and a
step-by-step tool change are in [docs/Z1-PROTOCOL.md](docs/Z1-PROTOCOL.md).

- **UDP 3333 announcement**, once a second and on every change:
  `Makera_Z1_000000,192.168.1.50,2222,0,Tool`. The last field is the state. This is the
  trigger: it arrives the moment the machine stops for a tool change.
- **TCP 2222 status query** (`?` in a binary frame): the bit in the spindle, the bit it
  wants, job time, alarm reason. z1mba connects, asks and disconnects: right away when the
  UDP state changes, then every 1 s around a tool change, 2 s during a job, 10 s when idle.

On firmware 1.1.3 the Z1 serves several clients at once, so z1mba, Makera Studio and the
[ha-makera-z1](https://github.com/Kannix2005/ha-makera-z1) integration can all poll at the
same time. Tested with uploads, jobs and tool changes from Studio while z1mba-style polls
ran: 90 of 90 answered, no errors in Studio.

**Read-only.** The only frame z1mba sends is the status query. It cannot move the
machine, start or stop a job, or change a setting.

## Home Assistant entities

Optional: see [Without Home Assistant](#without-home-assistant).

| Entity | Meaning |
|---|---|
| State | Idle, Running, Tool change, Hold, Paused, Alarm, Homing, Sleep, Offline |
| Tool / Next tool | the bit in the spindle; the bit the machine wants (empty when none is pending) |
| Tool change | on while the machine waits for the front button |
| Job active, Job time | a file is loaded; seconds of machining (the clock stops at a tool change) |
| Alarm, Alarm reason | e.g. Cover open, Emergency stop |
| Online, Machine IP, Tool length, WiFi signal | diagnostics |

Notifications ("Load T3", "Alarm: cover open", "Job done") are ordinary Home Assistant
automations on these entities.

## Hardware

- An ESP32 with Wi-Fi. The config is for a Freenove ESP32-S3-WROOM Lite (N8R8); its PSRAM
  is there for a screen. Power it from any USB charger: the Z1 has no usable 5 V outside
  the case.
- A WeAct 2.9" black/white/red e-paper (296x128, SPI), optional. See [Screen](#screen).

No wiring to the Z1 is needed.

## Screen

E-paper suits the job: the picture only has to change when the machine does, it is
readable in any light, and it keeps the last picture without power. At a tool change it
shows the bit to load in white on red, the bit coming out, and the job clock; otherwise
the bit in the spindle, an alarm and its reason, or the mountain while the Z1 is off.

z1mba is built and tested with one screen: the WeAct 2.9" black/white/red e-paper
(296x128), preset `screens/weact-2.9in-3c.yaml`. The layout is worked out from the
display's width and height (the big bit number beside the details on a wide screen, above
them on a tall or square one), and the native tests also draw it at a few other e-paper
sizes. Another screen should therefore mostly be a matter of copying the preset and
changing the `model` (any model of ESPHome's `epaper_spi` display), the size and the font
sizes; `tests/run.sh` then draws every state at that size and fails if anything runs off
the edge or overlaps. Untested on real hardware, though: contributions welcome.

Pins are substitutions in the board package (`screen_clk`, `screen_mosi`, `screen_cs`,
`screen_dc`, `screen_reset`, `screen_busy`). Override them in your own config, or leave
the `screen:` line out to run without a screen.

The screen redraws only when what it shows changes. A new bit, mode or alarm is drawn
once it has held for 1.5 s, and the job clock (in 5-minute steps) at most every 5 minutes,
so a long job costs a refresh every few minutes rather than a constant flicker. The
`z1_screen:` options `settle`, `calm_refresh` and `minute_step` change this.

## Getting started

```sh
cp secrets.example.yaml secrets.yaml   # Wi-Fi and an encryption key
esphome run z1mba.yaml                  # first time over USB, then over Wi-Fi
```

Home Assistant finds the device through ESPHome. z1mba finds the Z1 from its UDP
announcement, so the shared config needs nothing about your machine.

`z1mba.yaml` is put together from packages, so the parts can be swapped one line at a time:

```yaml
packages:
  board: !include packages/board-freenove-s3.yaml      # the ESP32 and the screen's pins
  core: !include packages/core.yaml                    # the z1 component and its entities
  wifi: !include packages/wifi.yaml                    # Wi-Fi and encrypted OTA updates
  home_assistant: !include packages/home-assistant.yaml  # optional
  screen: !include screens/weact-2.9in-3c.yaml         # optional, any preset
```

For another ESP32 board, copy `packages/board-freenove-s3.yaml` and change the chip and
the pins.

### Without Home Assistant

z1mba does not need Home Assistant: the screen reads the machine directly. Drop the
`home_assistant:` line, and add `packages/web.yaml` for a read-only status page with the
same entities on `http://z1mba.local/`. `z1mba-standalone.yaml` is that config:

```sh
esphome run z1mba-standalone.yaml
```

Without the API, ESPHome also skips its "no Home Assistant connected" reboot, so the
device can run on a network with no HA at all. Updates still go over the air with the
encryption key from `secrets.yaml`.

### Your machine

Settings that belong to your machine go in a file of your own that git ignores
(`*.local.yaml`), on top of the shared config:

```yaml
# z1mba.local.yaml
packages:
  base: !include z1mba.yaml

z1:
  host: 192.168.1.50                 # pin the address (give the Z1 a fixed DHCP lease)
  machine_name: Makera_Z1_000000     # only follow this one, if there are several
```

Then build and flash that file instead: `esphome run z1mba.local.yaml`.

To try it without an ESP32, run the same component as a Linux program on a PC on the same
LAN as the Z1. It logs state changes to the terminal:

```sh
esphome run z1mba-host.yaml
```

## Tools

- `tools/z1probe.py`: probe a Z1 from any computer, standard-library Python only. Listen
  for announcements, poll status, test what a second client gets. `--log runs/NAME.jsonl`
  writes JSON lines. `runs/` is ignored by git: raw logs name your machine and your LAN.
- `tools/make_fixture.py`: turn a probe log into an anonymised replay file for the C++
  tests. Machine names become `Makera_Z1_000000`, addresses `192.168.1.50` and up.
- `tools/make_logo.py`: the logo SVGs and the screen's skyline (`components/z1/z1_logo.h`)
  from a hand trace of the photo.
- `tools/screen_presets.py`: lists the screen presets for the layout test.
- `gcode/`: air-only test jobs (no spindle, Z at the machine's clearance height), one with
  two tool changes.

## Development

```
components/z1/           ESPHome external component
  z1_proto.h             frames, status and announcement parsing   (plain C++)
  z1_tracker.h           merges UDP + TCP, decides when to poll    (plain C++)
  z1_client.h            non-blocking UDP listener and TCP query   (POSIX sockets)
  z1_screen.h            what the screen says, the layout for any size, when to redraw  (plain C++)
  z1_logo.h              the skyline, generated by tools/make_logo.py
  z1_component.*         ESPHome glue: loop(), entities
components/z1_screen/    ESPHome glue: draws z1_screen.h on any ESPHome display
packages/                board, core, wifi, home-assistant, web, screen
screens/                 the e-paper preset (WeAct 2.9")
tests/                   native C++ tests; fixtures/ are real sessions, anonymised and replayed
docs/Z1-PROTOCOL.md      what the Z1 sends, as tested
```

```sh
tests/run.sh                                 # C++ tests, g++ with AddressSanitizer and UBSan
Z1_SVG=/tmp/z1 tests/run.sh                  # the same, plus an SVG of every screen and state
python3 -m unittest discover -s tools        # tool tests
uvx ruff check components tools              # lint
esphome compile z1mba.yaml                   # firmware
```

The logic is plain C++ with no ESPHome includes, so it builds and runs natively in
milliseconds. The screen draws through a small canvas interface: ESPHome's display on the
device, a recorder in the tests. The socket client uses POSIX calls, which ESP-IDF provides through lwIP, so
the same code runs in the tests against a fake Z1 on localhost.

Captured a new situation on your machine (an alarm, a pause, another firmware)? A probe log
of it, turned into a fixture, is the most useful contribution there is. See
[todo.md](todo.md) for what is planned.

## Credits and inspiration

z1mba builds on what these projects worked out and published. Its code is written from
scratch; the one thing taken over is the list of halt-reason codes from ha-makera-z1 (MIT).

- [BitCue](https://github.com/journeyonlabs/bitcue) by JourneyOn Labs (GPL-3.0): the idea
  of a tool-change screen in the bit's colour, bit life per bit, and its notes on the Z1's
  status reply, frames and job tracking.
- [ha-makera-z1](https://github.com/Kannix2005/ha-makera-z1) by Kannix2005 (MIT): a Home
  Assistant integration for the Z1 that polls with short connections. It showed that polling
  alongside Makera Studio is practical.
- [MakeraZ1Firmware](https://github.com/MakeraInc/MakeraZ1Firmware) and
  [CarveraController](https://github.com/MakeraInc/CarveraController) by Makera: the frame
  format, the status fields and the tool-change commands, read from the source.
- The [Carvera Community](https://github.com/Carvera-Community) docs and controller, and
  [unofficial-makera-cli](https://github.com/MattRuhland/unofficial-makera-cli), for the
  UDP announcement and its state field.
- [cnc-facing](https://github.com/nilsan/cnc-facing), a facing-job generator and `.nc`
  checker for the Z1 by the same author: its checker caught the `T2 M6` word order in the
  test jobs, and its bit catalogue will feed the bit names and types.

## How it was made

z1mba is written with [Claude Code](https://claude.com/claude-code). The author decides
what to build, runs the machine tests (the Z1, its tool changes and Makera Studio) and
reviews the changes. Claude researched the Z1's protocol and the projects above, wrote the
probe, the component, the tests and the docs, and traced the logo. The "Results" in
[docs/Z1-PROTOCOL.md](docs/Z1-PROTOCOL.md) were observed on a real Z1; the rest of that file
says where each detail comes from.

## Logo

The logo is the Zimba's skyline from the Saulakopf, traced from a photo by Friedrich
Böhringer (CC BY-SA 2.5), so the logo files are CC BY-SA 2.5 too. Details and the variants
(filled mark, an outline for milling with a V-bit, a square icon) are in
[assets/logo](assets/logo/README.md).

## Support

z1mba is free and open source. If it saved you a walk to the machine or a broken bit, you
can [buy me a coffee on Ko-fi](https://ko-fi.com/nilsan). It is never expected, and always
appreciated.

## License

[Mozilla Public License 2.0](LICENSE).
