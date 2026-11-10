/*
 * s16-pi - the video lines drawn on core 1
 *
 * The core (core 0: V33, V35, sound) hands each visible line over as soon as it
 * starts, with the few registers it must be drawn with (scroll, layer control,
 * video control); core 1 draws the queued lines in order and passes each one on
 * (to the screen with the scanlines, or to core 2 for COMPOSITE). Before
 * anything the drawing reads changes (VRAM, palette, sprite buffer), the core
 * calls McSync: core 0 waits until core 1 has drawn every queued line, so the
 * picture is identical to the single core one. Games change these almost only
 * in the vertical blank: the wait is rare and short.
 *
 * Same scheme as neo-pi's patches/geolith-mc.py (wake-ups with DSB + SEV,
 * a stuck core 1 detected after 0.5 s: its lines are drawn on core 0 and the
 * game goes on single core).
 */
#ifndef MCLINE_H
#define MCLINE_H

#include <circle/types.h>
#include "s16_api.h"

// called on core 1 after each line it has drawn into the frame buffer
typedef void TMcLineDone (unsigned nRow);

void McSetup (u32 *pFrame, unsigned nPitch, TMcLineDone *pDone);
void McEnable (boolean bOn);		// on: the core's lines go through core 1
boolean McEnabled (void);
boolean McFailed (void);
boolean McAlive (void);
void McSync (void);			// core 0: wait for every queued line
void McWorker (void);			// core 1's loop, never returns

// diagnostics when core 1 was found stuck
void McFailText (char *pOut, size_t nSize, unsigned nFrame);

#endif
