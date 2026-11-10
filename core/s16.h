/*
 * s16-pi - the System 16B machine: 68000, Z80, the 315-5195 memory mapper,
 * the optional 8751 protection MCU, I/O. The logic follows MAME's driver
 * (segas16b.cpp, 315_5195.cpp, Aaron Giles), re-implemented by hand; time is
 * kept in scanlines, as the front-end needs them.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef S16_H
#define S16_H

#include "s16_api.h"
#include "i8751.h"
#include <stdint.h>
#include <stddef.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// ---- game description (core/s16_games.inc) --------------------------------

enum { RGN_MAIN, RGN_TILES, RGN_SPRITES, RGN_SOUND, RGN_MCU, RGN_KEY, RGN_COUNT };
enum { LOAD_BYTES, LOAD_EVEN, LOAD_ODD };		// plain, or every other byte (16-bit ROM pairs)
enum { BOARD_5358_SMALL, BOARD_5358, BOARD_5521, BOARD_5704 };
enum { CPU_68000, CPU_FD1094, CPU_FD1089A, CPU_FD1089B };

struct TS16Rom
{
	const char *pName;
	u32 nCRC, nSize;
	u8 nRegion, nLoad;
	u32 nOffset;
};

struct TS16Game
{
	const char *pName, *pParent, *pDesc, *pYear, *pMaker;
	const TS16Rom *pRoms;
	int nRoms;
	u32 RegionSize[RGN_COUNT];
	u8 nBoard, nCpu;
	bool bMcu;				// an 8751 drives the mapper
	u16 nDswDefault;
	u8 nPlayers, nButtons;
};

// ---- video (s16_video.cpp) --------------------------------------------------

struct TS16Video
{
	// what the 68000 writes (big endian words as the CPU sees them)
	u16 TileRam[0x8000];			// 16 pages of 64x32 tiles
	u16 TextRam[0x800];			// 64x28 text layer, and the scroll registers
	u16 PalRam[0x800];
	u16 SprRam[0x400];
	u8 TileBank[2];
	bool bDisplay;

	// latched at line 261 for the next frame: page select, y scroll, x scroll
	// of foreground, background and their alternates
	u16 Pages[4], YScroll[4], XScroll[4];

	u32 Colors[0x1000];			// 2048 colours, then their shadow / highlight
};

void s16_video_init (const u8 *pTiles, size_t nTilesSize, const u8 *pSprites, size_t nSpritesSize,
		     bool bAltBanks);
void s16_video_exit (void);
void s16_video_reset (TS16Video *pV);
void s16_video_palette_w (TS16Video *pV, unsigned nIndex, u16 nData);
void s16_video_latch (TS16Video *pV);			// line 261
void s16_video_sprites (TS16Video *pV);			// line 0: the sprites of the coming frame
void s16_video_line (const TS16Video *pV, int y, u32 *pOut);

#endif
