# s16-pi - Technical notes (lab)

Bare-metal SEGA System 16 emulator for Raspberry Pi 3 and 4 (no operating
system: Circle, AArch64), the sibling of neo-pi, nes-pi and m92-pi. This
repository is the lab: s16-pi is not released yet.

## State

- **Front-end**: m92-pi's, adapted to the System 16 picture (320x224, 262
  lines, the frame starts at line 224 with the vertical blank). `common/`
  is the same as in the three other projects, byte for byte.
- **Menus**: the new look (petrol grid, dark panels with a white frame,
  crimson band; Cyberwire for the game list, Matinee for the rest), drawn
  at 320x224 like the game. Long titles scroll in the list. The logo is a
  placeholder (`CUI::Logo`).
- **Core**: none yet. `core/s16_stub.cpp` is a placeholder behind the
  real interface (`core/s16_api.h`): a game is a test card drawn one line
  at a time through the same hooks the machine will use (line hook, line
  sink for core 1, video sync), so list, menus, pads, multicore lines and
  beam racing can be tried first. Its game table is a short hand-written
  list of System 16B parents; no ROM is read, no DIP switches, no sound.

## Plan

1. System 16B machine (68000, Z80, YM2151, UPD7759, tiles and sprites one
   line at a time, 315-5195 mapper, FD1094 / FD1089) on a Linux harness,
   checked against MAME; game and DIP tables generated from MAME's
   `segas16b.cpp`.
2. System 18 on top of it (VDP, YM3438 x2, RF5C68).

## Layout

```
core/        s16_api.h (the interface), s16_stub.cpp (placeholder core)
common/      shared with neo-pi, nes-pi and m92-pi (pads, audio, beam racing, TV filter)
src/         the s16-pi front-end (as m92-pi's, see its README, 4.1)
third_party/ miniz, snes_ntsc, rpi-firmware
patches/     Circle patches (USB polling, XInput pads)
dist/sd/     what goes on the SD card
test/        race_sim.cpp (beam racing host test)
```

Differences from m92-pi's front-end: 4 buttons (A B C D; the games use up
to three), CONTROLS asks A B C D SELECT START, DIP switches are one 16-bit value (SW1 low byte, SW2 high byte),
no CPU SPEED, no EEPROM, no cabinet voicing.

## Building

As m92-pi (toolchain and Circle copies: see neo-pi's `BUILD.md`):
`./build.sh all`, `./build.sh qemu`, `./make_release.sh`.
