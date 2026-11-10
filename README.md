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
- **Core** (`core/`): the System 16B machine, written by hand on MAME's
  logic and checked against MAME 0.264 on the Linux harness.
  - `s16.cpp`: 68000 (Musashi), Z80, the 315-5195 memory mapper, I/O, time
    kept in scanlines (262 per frame, the CPUs take turns 8 times a line),
    save states.
  - `i8751.cpp`: the 8751 protection MCU of some boards (it drives the
    mapper). Written for s16-pi, not taken from MAME.
  - `s16_video.cpp`: tile layers, text, sprites and palette, one line at a
    time. Sprites are read from sprite RAM as it is at each line (MAME draws
    the frame at its end); the scroll registers are latched at line 261 as
    in MAME.
  - `s16_sound.cpp`: YM2151 (ymfm), uPD7759 in slave mode, and the board's
    analogue section from the components of MAME's netlist, as digital
    filters (within 1 dB of MAME up to 8 kHz).
  - Multicore: the lines are drawn on core 1 from the machine's own video
    RAM; every write to what the drawing reads waits for the lines handed
    over (`VideoSync`).
- **Games**: `core/s16_games.inc` is still written by hand, with two sets:
  Altered Beast (`altbeast`, 8751, 171-5521 board) and Golden Axe
  (`goldnaxe`, 8751, 171-5797 board). No DIP switches yet.
- **Checked against MAME** (attract mode): Altered Beast's frames are
  identical pixel for pixel, apart from a logo that changes colour in the
  middle of a frame (MAME shows the change a frame early); its demo starts
  2 frames earlier than in MAME, a difference in the boot's timing not yet
  explained. Golden Axe's frames are identical too, demo included; the
  still screens of its boot come up to 4 frames earlier.
- **Not there yet**: FD1094 / FD1089 / MC8123 encryption (most sets),
  flip screen, the game and DIP tables from MAME, hiscores,
  System 18.

## Plan

1. System 16B: game and DIP tables generated from MAME's `segas16b.cpp`,
   FD1094 / FD1089 / MC8123, hiscores.
2. System 18 on top of it (VDP, YM3438 x2, RF5C68).

## Layout

```
core/        the System 16B machine (s16_api.h is all the front-end sees)
harness/     Linux test harness: make, ./s16test <roms> altbeast -s 300 -w
common/      shared with neo-pi, nes-pi and m92-pi (pads, audio, beam racing, TV filter)
src/         the s16-pi front-end (as m92-pi's, see its README, 4.1)
third_party/ musashi (68000) and z80 as in Geolith, ymfm, miniz, snes_ntsc, rpi-firmware
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
