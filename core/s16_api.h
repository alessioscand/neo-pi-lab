/*
 * s16-pi - C interface to the System 16 core
 *
 * The core is C++ with the standard library; the Circle front-end is built
 * without it. This header is all the front-end sees.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef S16_API_H
#define S16_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the picture: 320x224, 262 lines per frame; lines 0-223 are visible and a
 * frame starts at line 224, with the vertical blank */
#define S16_WIDTH	320
#define S16_HEIGHT	224
#define S16_LINES	262

/* ---- games (MAME romsets) --------------------------------------------- */
int s16_num_games (void);
int s16_find (const char *pName);			/* -1 = not a set the core knows */
const char *s16_game_name (int nGame);			/* "goldnaxe" */
const char *s16_game_parent (int nGame);		/* "" or the parent's name */
const char *s16_game_desc (int nGame);			/* "Golden Axe (World)" */
const char *s16_game_year (int nGame);
const char *s16_game_maker (int nGame);
int s16_game_players (int nGame);
int s16_game_buttons (int nGame);

/* ---- DIP switches -------------------------------------------------------
 * The value of each switch is (dsw & mask): SW1 in the low byte, SW2 in the
 * high byte. */
typedef struct
{
	const char *pName;		/* "Lives" */
	const char *pLocation;		/* "SW2:3,4" */
	uint16_t nMask, nDefault;
	int nSettings;
	uint16_t nCondMask, nCondValue;	/* shown only when (dsw & mask) ==/!= value */
	int nCondOp;			/* 0 none, 1 equals, 2 not equals */
} TS16Dip;
int s16_dip_count (int nGame);
int s16_dip_get (int nGame, int nDip, TS16Dip *pDip);
const char *s16_dip_setting_name (int nGame, int nDip, int nSetting, uint16_t *pValue);
uint16_t s16_dsw_default (int nGame);

/* ---- the machine ---------------------------------------------------------- */

/* reads one ROM file into pDst (exactly nSize bytes); returns non-zero if found */
typedef int TS16Fetch (void *pCtx, const char *pName, uint32_t nCRC, uint32_t nSize, uint8_t *pDst);

/* builds the machine for a game; 0 on failure (pMissing gets the ROM's name) */
int s16_load (int nGame, TS16Fetch *pFetch, void *pCtx, char *pMissing, size_t nMissing);
void s16_unload (void);
void s16_reset (void);

/* one frame: from the start of the vertical blank (line 224) to the next */
void s16_run_frame (void);
// a count that grows when the core does one-off slow work (an encrypted page of
// program decrypted the first time it runs): such a frame is not a measure of the game
unsigned s16_slow_work (void);

/* players 0-3, active high: bit 0 right, 1 left, 2 down, 3 up, 4 button 1,
 * 5 button 2, 6 button 3 */
void s16_set_player (int nPlayer, uint8_t nBits);
/* bits 0-3 start 1-4, bits 4-7 coin 1-4 (active high) */
void s16_set_system (uint8_t nBits);
/* Laser Ghost: s16_guns () is the number of guns of the game (0 = none). Gun
   p (0-2) aims at pixel x, y; its crosshair is drawn on the picture if on.
   s16_gun_adjust does the game's gun adjustment (asked for while the work RAM
   is blank): call it each frame from the start, t = frames since then, instead
   of s16_set_aim, and pass the trigger/bomb bits it gives to s16_set_player,
   until it returns 0. s16_nvram: the battery-kept RAM to save and load (0 bytes: none). */
int s16_guns (void);
unsigned s16_gun_recoils (int p);	/* times gun p's recoil came on, so far */
int s16_gun_in_play (int p);		/* player p is in the game (so can shoot) */
void s16_set_aim (int p, int x, int y);
void s16_set_crosshair (int p, int bOn);
int s16_gun_adjust (int t, uint8_t Fire[3]);
size_t s16_nvram_size (void);
uint8_t *s16_nvram (void);
void s16_set_dsw (uint16_t nDSW);

/* sound: 48 kHz stereo frames made so far */
int s16_audio_read (int16_t *pDst, int nMaxFrames);

/* video: S16_WIDTH pixels 0x00RRGGBB, one line at a time */
typedef void TS16LineCB (void *pCtx, int y, const uint32_t *pRGB);
void s16_set_line_callback (TS16LineCB *pCB, void *pCtx);
/* called at the start of each of the S16_LINES lines, before it is drawn */
typedef void TS16LineHook (void *pCtx, int nLine);
void s16_set_line_hook (TS16LineHook *pHook, void *pCtx);

/* multicore: lines handed to a sink, to be drawn on another core with
 * s16_render_line; the core calls the sync function before it changes
 * anything the drawing reads (video RAM, palette, scroll registers), and the
 * function must return only when every line handed over has been drawn.
 * (TS16LineRegs is the place for what a line needs from its own moment; for
 * now everything is read from the machine, kept still by the sync.) */
typedef struct { int nLine; } TS16LineRegs;
typedef void TS16LineSink (void *pCtx, int y, const TS16LineRegs *pRegs);
void s16_set_line_sink (TS16LineSink *pSink, void *pCtx);	/* 0 = draw on this core */
void s16_set_video_sync (void (*pSync) (void *), void *pCtx);
void s16_render_line (const TS16LineRegs *pRegs, int y, uint32_t *pOut);

/* save states */
size_t s16_state_size (void);
int s16_state_save (void *pBuffer, size_t nSize);	/* returns the size written, 0 = failed */
int s16_state_load (const void *pBuffer, size_t nSize);

/* hiscores (MAME's hiscore.dat): size of the .hi file, 0 = none for this game;
 * ready once the game has set its table up; get / put read and write it */
size_t s16_hi_size (void);
int s16_hi_ready (void);
void s16_hi_get (uint8_t *pDst);
void s16_hi_put (const uint8_t *pSrc);

/* diagnostics */
uint32_t s16_main_pc (void);
uint32_t s16_mcu_pc (void);

#ifdef __cplusplus
}
#endif

#endif
