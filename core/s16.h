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
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

// ---- game description (core/s16_games.inc) --------------------------------

enum { RGN_MAIN, RGN_TILES, RGN_SPRITES, RGN_SOUND, RGN_MCU, RGN_KEY, RGN_SOUNDKEY, RGN_COUNT };
enum { LOAD_BYTES, LOAD_EVEN, LOAD_ODD };		// plain, or every other byte (16-bit ROM pairs)
enum { BOARD_5358_SMALL, BOARD_5358, BOARD_5521, BOARD_5704, BOARD_5797,	// System 16B
       BOARD_S18_SHADOW, BOARD_S18_5874, BOARD_S18_5987 };			// System 18
enum { CPU_68000, CPU_FD1094, CPU_FD1089A, CPU_FD1089B };
enum { MCU_NONE, MCU_8751, MCU_SIM_TTURF, MCU_SIM_WB3 };	// (the last two: not dumped, MAME's stand-ins)

struct TS16Rom
{
	const char *pName;
	u32 nCRC, nSize;			// of the file
	u8 nRegion, nLoad;
	u32 nOffset;
	u32 nFrom, nLength;			// a part of the file only (nLength 0: all of it)
};

struct TS16DipSetting { u16 nValue; const char *pName; };

struct TS16DipDef
{
	u16 nMask, nDefault;			// SW1 in the low byte, SW2 in the high byte
	const char *pName, *pLocation;
	u16 nFirst, nCount;			// its settings, in the input set's table
};

// what a bit of an input port is: 0 nothing, IN_SYSTEM | bit of the
// front-end's system byte, IN_PLAYER | player << 4 | bit of the player's byte
enum { IN_SYSTEM = 0x40, IN_PLAYER = 0x80 };

// the ports: System 16B service, player 1, unused, player 2;
// System 18 player 1, 2, 3, service, and D. D. Crew's extra 3, 4, service
enum { PORTS = 8 };

// a bit that is one thing or another as a DIP switch is set: it is nCode
// when (dsw & nDswMask) == nDswValue
struct TS16CondBit { u8 nPort, nBit, nCode; u16 nDswMask, nDswValue; };

struct TS16Inputs
{
	const TS16DipDef *pDips;
	const TS16DipSetting *pSettings;
	u16 nDswDefault;
	u8 nPlayers, nButtons;
	u8 Port[PORTS][8];
	const TS16CondBit *pConds;		// the bits that depend on a switch, 0 = none
	u8 nConds;
};

struct TS16Game
{
	const char *pName, *pParent, *pDesc, *pYear, *pMaker;
	const TS16Rom *pRoms;
	int nRoms;
	u32 RegionSize[RGN_COUNT];
	const TS16Inputs *pInputs;
	u8 nBoard, nCpu, nMcu;
	bool bMc8123;				// the sound program is encrypted
	bool bPpi;				// System 18: the 8255 of the 4-player D. D. Crew
};

static inline bool S18Board (u8 nBoard)	{ return nBoard >= BOARD_S18_SHADOW; }

// ---- the encrypted CPUs (s16_crypt.cpp) -------------------------------------

void s16_fd1094_decrypt (const u8 *pKey, u8 nState, u32 nAddress, const u8 *pSrc, u8 *pDst, u32 nBytes);
u8 s16_fd1094_irq_state (const u8 *pKey);
void s16_fd1089_decrypt (bool bB, const u8 *pKey, u32 nAddress, const u8 *pSrc, u8 *pOpcodes, u8 *pData, u32 nBytes);
void s16_mc8123_decode (const u8 *pKey, u8 *pRom, u8 *pOpcodes, u32 nLength);

// ---- System 18's VDP (s18_vdp.cpp) -------------------------------------------

struct TS18Vdp
{
	u16 Vram[0x8000];
	u16 Sat[0x200];				// the chip's own copy of the sprite table
	u16 Cram[0x40];
	u16 Vsram[0x40];
	u8 Regs[0x20];
	u16 nCode, nAddr, nPart1, nPart2, nFillLength;
	u8 bPending, bFill, bVblank, bIrq6;
};

struct TS18VdpBus
{
	u16 (*pRead) (u32 nAddr);		// the 68000's memory, for DMA
	unsigned nStall;			// 68000 cycles the DMA takes
};

void s18_vdp_reset (TS18Vdp *pV);
void s18_vdp_write (TS18Vdp *pV, unsigned nOffset, u16 nData, u16 nMask, u32 *pColors, TS18VdpBus *pBus);
u16 s18_vdp_read (TS18Vdp *pV, unsigned nOffset, int nLine, int nHpos);
void s18_vdp_vblank (TS18Vdp *pV);			// line 224
void s18_vdp_frame (TS18Vdp *pV);			// line 0
void s18_vdp_line (const TS18Vdp *pV, int y, u16 *pRaw);	// 0 = nothing, else 0x100 | colour

// ---- video (s16_video.cpp) --------------------------------------------------

struct TS16Video
{
	// what the 68000 writes (big endian words as the CPU sees them)
	u16 TileRam[0x8000];			// 16 pages of 64x32 tiles
	u16 TextRam[0x800];			// 64x28 text layer, and the scroll registers
	u16 PalRam[0x800];
	u16 SprRam[0x400];
	u8 TileBank[8];				// System 16B uses two
	u8 SprBank[16];				// the sprite ROM each bank number selects
	bool bDisplay;

	// latched at line 261 for the next frame: page select, y scroll, x scroll
	// of foreground, background and their alternates
	u16 Pages[4], YScroll[4], XScroll[4];

	u32 Colors[0x1040];			// 2048 colours, their shadow / highlight, the VDP's 64

	// System 18
	TS18Vdp Vdp;
	u8 nVdpMixing;				// where the VDP's picture goes among the layers
	bool bVdpEnable;
};

// bAltBanks: the sprite banks of the 171-5358 boards; nBankSize: tiles in a
// tile bank (System 16B 4096, System 18 1024)
void s16_video_init (const u8 *pTiles, size_t nTilesSize, const u8 *pSprites, size_t nSpritesSize,
		     bool bAltBanks, unsigned nBankSize, bool bS18);
unsigned s16_video_tiles (void);
unsigned s16_video_sprite_banks (void);		// of 0x20000 bytes
void s16_video_exit (void);
void s16_video_reset (TS16Video *pV);
void s16_video_palette_w (TS16Video *pV, unsigned nIndex, u16 nData);
void s16_video_latch (TS16Video *pV);			// line 261
void s16_video_line (const TS16Video *pV, int y, u32 *pOut);

// ---- sound (s16_sound.cpp) ---------------------------------------------------

void s16_sound_init (void (*pNmi) (void));
void s16_sound_reset (void);
void s16_sound_advance (unsigned nCycles);		// Z80 cycles gone by
u8 s16_sound_ym_r (unsigned nOffset);
void s16_sound_ym_w (unsigned nOffset, u8 nData);
void s16_sound_upd_control (bool bMd, bool bReset);
void s16_sound_upd_w (u8 nData);
bool s16_sound_upd_idle (void);
int s16_sound_read (int16_t *pDst, int nMaxFrames);
size_t s16_sound_state (u8 *pBuf, size_t nMax, bool bSave);	// returns the bytes used, 0 = failed

// ---- System 18 sound (s18_sound.cpp) -----------------------------------------

void s18_sound_init (void (*pIrq) (bool));
void s18_sound_reset (void);
void s18_sound_advance (unsigned nCycles);		// Z80 cycles gone by
u8 s18_sound_ym_r (int nChip, unsigned nOffset);
void s18_sound_ym_w (int nChip, unsigned nOffset, u8 nData);
void s18_sound_rf_w (unsigned nOffset, u8 nData);
u8 s18_sound_rf_mem_r (unsigned nOffset);
void s18_sound_rf_mem_w (unsigned nOffset, u8 nData);
int s18_sound_read (int16_t *pDst, int nMaxFrames);
size_t s18_sound_state (u8 *pBuf, size_t nMax, bool bSave);

#endif
