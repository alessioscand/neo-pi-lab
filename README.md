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
  - `s16_crypt.cpp`: the encrypted CPUs, FD1094, FD1089A/B (68000) and
    MC8123 (Z80): MAME's algorithms and tables, taken with the least
    changes. `s16.cpp` decrypts a 64 KB page of program the first time it is
    used (FD1094: once per state of the chip; FD1089: once per address the
    mapper puts it at) and keeps it.
  - ROM boards: 171-5358 (and its small version), 171-5521, 171-5704,
    171-5797 (with its 315-5248 multiplier and 315-5250 compare chips).
- **Games**: `core/s16_games.inc` and `dist/sd/s16pi/list.txt` are written
  by `tools/gen_s16_tables.py` from MAME's `segas16b.cpp`: 57 sets with
  their ROMs, hardware, DIP switches and what each input bit is. Left out
  (the script says why for each): bootlegs and Korean boards, sets not
  working in MAME, System 16C, controls a pad does not have (trackball,
  dial, analog stick, mahjong, two sticks) and, for now, the games on a
  vertical monitor. Tough Turf and Wonder Boy III (set 6), whose 8751 is
  not dumped, use MAME's stand-ins for it.
- **Checked against MAME 0.264** (attract mode, a frame every 150, first
  20 seconds): identical pixel for pixel on `altbeast`, `goldnaxe`,
  `goldnaxe1`, `goldnaxe2`, `goldnaxe3`, `goldnaxeu`, `aliensyn`,
  `aliensyn3`, `aliensyn7`, `aurail`, `aurail1`, `aurailj`, `shinobi4`,
  `wb3`, `wb32`, `wb33`, `wb34`: every CPU kind and every ROM board. The
  only difference: a logo of Altered Beast that changes colour in the
  middle of a frame (MAME shows the change a frame early). The still
  screens of some boots come a few frames earlier than in MAME. The sound
  of the two MC8123 sets follows MAME's after a coin and a start. The
  other 40 sets have not been run: no ROMs here.
- **Not there yet**: flip screen, hiscores, vertical games, System 18.

## Plan

1. System 16B: hiscores, the vertical games (Passing Shot, Time Scanner,
   Action Fighter ...).
2. System 18 on top of it (VDP, YM3438 x2, RF5C68).

## Layout

```
core/        the System 16B machine (s16_api.h is all the front-end sees)
harness/     Linux test harness: make, ./s16test <roms> altbeast -s 300 -w
tools/       gen_s16_tables.py: the game tables from MAME's driver
common/      shared with neo-pi, nes-pi and m92-pi (pads, audio, beam racing, TV filter)
src/         the s16-pi front-end (as m92-pi's, see its README, 4.1)
third_party/ musashi (68000) and z80 from Geolith (z80.c with one addition: a function
             of its own for the opcode fetches, for the MC8123), ymfm, miniz, snes_ntsc,
             rpi-firmware
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
