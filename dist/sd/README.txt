s16-pi 0.1.0 - Bare-metal SEGA System 16 emulator for Raspberry Pi 3 and 4
============================================================================

WORK IN PROGRESS

  A first version to try on the Pi: two games for now, Altered Beast
  (altbeast.zip, MAME's "set 8") and Golden Axe (goldnaxe.zip, MAME's
  "set 6"), the ones with the 8751. Picture, sound, save state and the
  menus are there; DIP switches and the other games are not yet.

SET-UP

  1. Use a microSD card formatted as FAT32, with a standard partition
     table.
  2. Copy EVERYTHING in this folder to the card's root.
  3. Put altbeast.zip and goldnaxe.zip (MAME romsets) in the s16pi
     folder.
  4. Connect a USB gamepad and the display (on a Pi 4, HDMI 0, next to the
     power socket). Insert the card and switch on.

CONTROLS

  The first time a pad model is connected, the CONTROLS screen opens by
  itself: press the button you want for A, B, C, D, SELECT/COIN, START.

  Game list: Up/Down to choose, Left/Right to jump by 8, A or Start to
  play, D for OPTIONS. A long title scrolls.

  In game:
  Select (short press)      Insert a coin
  Select held 1 second      In-game menu
  Select + A                Screenshot: s16pi/shots/<game>_NNN.png
  Select + B                Lazy finger on/off
  Select + C                Status line on/off
  Select + D                Credits

  Always switch off with POWER OFF. For help, s16pi/LOG.TXT is all that
  is needed.

----------------------------------------------------------------------------

s16-pi is a human-led, vibe-coded experiment by Alessio Scanderebech,
written with Claude. Free software, licensed under GPLv3 (see LICENSES.txt).

SEGA is a trademark of its owner. Games are not included.
