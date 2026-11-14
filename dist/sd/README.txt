s16-pi 0.3.2 - Bare-metal SEGA System 16 emulator for Raspberry Pi 3 and 4
============================================================================

WORK IN PROGRESS

  A version to try on the Pi: the System 16B and System 18 games that
  play with a stick and buttons on a horizontal screen (Altered Beast,
  Golden Axe, Shinobi, Alien Syndrome, Aurail, Wonder Boy III, ESWAT,
  Cotton ... and on System 18 Shadow Dancer, Moonwalker, Alien Storm,
  D. D. Crew, Bloxeed, Clutch Hitter, Laser Ghost), 80 MAME sets,
  protected ones included. Picture, sound, save state, DIP switches and the menus are
  there. Vertical games and hiscores are not yet.

  Laser Ghost is played with the pads instead of its 3 guns: the stick
  moves the crosshair (blue, yellow and red for players 1, 2 and 3), A
  fires, B is the bomb. The first time, the game asks for its gun
  adjustment: s16-pi does it by itself in about 15 seconds (ADJUSTING
  THE GUNS), then keeps it in saves/lghost.nv with the rest of the
  board's battery-kept RAM.

  System 18 boards run at 57.23 frames a second: here they run at the
  screen's 60, a little faster (music a little higher) than the real
  thing.

SET-UP

  1. Use a microSD card formatted as FAT32, with a standard partition
     table.
  2. Copy EVERYTHING in this folder to the card's root.
  3. Put your games in the s16pi folder as MAME romsets, one .zip per
     set with MAME's names (goldnaxe.zip, mwalk.zip ...), from a
     recent MAME: the protected sets need their .key file. A clone
     needs its parent's zip beside it.
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
