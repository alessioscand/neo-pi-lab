/*
 * s16-pi - the System 16B and System 18 machines (see s16.h)
 *
 * System 18: as MAME's segas18.cpp (Aaron Giles): the same 315-5195 mapper
 * with its own regions, the 315-5296 I/O chip, the VDP (s18_vdp.cpp) and its
 * own sound board (s18_sound.cpp).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "s16.h"
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "m68k.h"
#include "z80.h"
size_t s16_m68k_state_size (void);		// s16_m68kstate.c
void s16_m68k_state_save (void *pDst);
void s16_m68k_state_load (const void *pSrc);
}

#include "s16_games.inc"

// ---- clocks ------------------------------------------------------------------
// System 16B: pixel clock 25.1748 MHz / 4, 400 pixel clocks per line, 262
// lines: 60.05 Hz. System 18: 262 lines at 57.23 Hz (MAME: measured on a
// board), 342 pixels a line.
// CPU cycles per line, as 16.16 fixed point: 68000 at 10 MHz, Z80 at 5 MHz
// (System 18: 8 MHz), 8751 at 8 MHz / 12.
#define LINE_HZ_16B	(25174800.0 / 4 / 400)
#define LINE_HZ_18	(57.23 * 262)
#define FIX(hz, line)	((u32) ((hz) / (line) * 65536.0 + 0.5))
static u32 s_nMainPerLine, s_nSoundPerLine, s_nMcuPerLine;
#define SLICES		8		// the CPUs take turns this many times per line

// ---- the machine's state -----------------------------------------------------

static const TS16Game *s_pGame;
static u8 *s_pRegion[RGN_COUNT];
static bool s_bLoaded;
static bool s_bS18;

static struct TMachine
{
	u8 WorkRam[0x4000];
	u8 SoundRam[0x2000];		// (System 16B: 2 KB)
	u8 Mapper[0x20];		// the 315-5195's registers
	u8 nToSound, nFromSound;
	bool bSoundIrq;
	bool bMainReset;		// the mapper holds the 68000 in reset
	s32 nMainFrac, nSoundFrac, nMcuFrac;	// cycles each CPU is owed, 16.16 (negative: it ran ahead)
	int nMainStall;			// 68000 cycles to lose (see McuPortWrite)
	u32 nSoundBank;			// offset in the sound region
	u8 nFdState;			// FD1094: the state the program chose,
	bool bFdIrq;			// and whether an interrupt is being served
	u16 Mult[2];			// 171-5797: the 315-5248 multiplier
	struct TCompare { u16 Regs[16]; u8 nBit; } Compare[2];	// and its two 315-5250
	u8 Player[4], nSystem;		// from the front-end, active high
	u16 nDsw;
	int nLine;
	u32 nFrame;
	// System 18
	u8 IoLatch[8], nIoDir, nIoCnt;	// the 315-5296
	bool bSoundNmi;			// the mapper's line to the Z80's NMI
	u32 nMainLineDone;		// 68000 cycles run in this line (the VDP's H counter)
	// Laser Ghost: the guns' positions (from the front-end) and the MSM6253 A/D
	u8 GunX[3], GunY[3];		// 0-255, left to right and top to bottom
	u8 nAdcShift;			// the converted value, shifted out a bit at a time
	u8 nAdcSelect;			// channels 2 and 3: gun 2 (0) or gun 3 (1)
} M;
static TS16Video V;
static z80 s_Z80;
static i8751_cpu s_Mcu;

// the front-end's hooks
static TS16LineCB *s_pLineCB;		static void *s_pLineCtx;
static TS16LineHook *s_pHook;		static void *s_pHookCtx;
static TS16LineSink *s_pSink;		static void *s_pSinkCtx;
static void (*s_pSync) (void *);	static void *s_pSyncCtx;

static inline void VideoSync (void)	{ if (s_pSync) s_pSync (s_pSyncCtx); }

// ---- 68000 address space -----------------------------------------------------
// The 315-5195 places 8 regions anywhere in the 16 MB space; whatever is left
// reads and writes the mapper's own registers. One entry per 64 KB.

enum { K_MAPPER, K_ROM, K_WORK, K_SPRITE, K_PALETTE, K_TILE, K_TEXT, K_IO, K_BANK, K_MATH, K_COMPARE, K_VDP };
// pRead: what the 68000 reads there; pOpcodes: what it fetches as opcodes (the
// same, without an encrypted CPU). A null pointer on a ROM page: not decrypted
// yet (see the encrypted CPUs below). bBankWrite: a write to this ROM sets the
// banks of the System 18 171-5987 board.
static struct TPage { u8 nKind; bool bBankWrite; u32 nMask; const u8 *pRead, *pOpcodes; u32 nRom; } s_Page[256];

static void MapPages (unsigned nBase, unsigned nSizeMask, u8 nKind, u32 nMask, const u8 *pMem, u32 nLength,
		      u32 nRomOffset)
{
	for (unsigned a = nBase; a <= nBase + nSizeMask && a < 0x1000000; a += 0x10000)
	{
		TPage &P = s_Page[a >> 16];
		P.nKind = nKind;
		P.bBankWrite = false;
		P.nMask = nMask;
		P.pRead = P.pOpcodes = pMem;
		P.nRom = 0;
		if (nKind == K_ROM)
		{
			// the ROM repeats in its region; nothing past its end
			const u32 nOffs = nRomOffset + ((a - nBase) & (nLength - 1));
			if (nOffs >= s_pGame->RegionSize[RGN_MAIN])
			{
				P.nKind = K_MAPPER;
				P.pRead = P.pOpcodes = 0;
				continue;
			}
			P.nRom = nOffs;
			P.pRead = s_pGame->nCpu == CPU_FD1089A || s_pGame->nCpu == CPU_FD1089B ? 0 : pMem + nOffs;
			P.pOpcodes = s_pGame->nCpu == CPU_68000 ? P.pRead : 0;
		}
	}
}

static const u32 s_SizeMask[4] = {0x00ffff, 0x01ffff, 0x07ffff, 0x1fffff};

// tile RAM, with the text RAM in its second 64 KB (or over all of a 64 KB region)
static void MapTiles (u32 nBase, u32 nSizeMask)
{
	MapPages (nBase, nSizeMask, K_TILE, 0xffff, 0, 0, 0);
	for (u32 a = nBase; a <= nBase + nSizeMask; a += 0x10000)
	{
		if (nSizeMask == 0xffff || ((a - nBase) & 0x10000))
		{
			MapPages (a, 0xffff, K_TEXT, 0x0fff, 0, 0, 0);
		}
	}
}

// as segas18_state::memory_mapper
static void UpdateMapping18 (void)
{
	const u8 *pRom = s_pRegion[RGN_MAIN];
	const u8 nBoard = s_pGame->nBoard;
	const bool bBig = s_pGame->RegionSize[RGN_MAIN] > 0x100000;	// 171-5987 with 2 MB
	for (int nIndex = 7; nIndex >= 0; nIndex--)
	{
		const u32 nSizeMask = s_SizeMask[M.Mapper[0x10 + 2 * nIndex] & 3];
		const u32 nBase = ((u32) M.Mapper[0x11 + 2 * nIndex] << 16) & ~nSizeMask;
		switch (nIndex)
		{
		case 7: MapPages (nBase, nSizeMask, K_IO, 0x3fff, 0, 0, 0); break;
		case 6: MapPages (nBase, nSizeMask, K_PALETTE, 0x0fff, 0, 0, 0); break;
		case 5: MapTiles (nBase, nSizeMask); break;
		case 4: MapPages (nBase, nSizeMask, K_SPRITE, 0x07ff, 0, 0, 0); break;
		case 3: MapPages (nBase, nSizeMask, K_WORK, 0x3fff, M.WorkRam, 0, 0); break;
		case 2:
			if (nBoard != BOARD_S18_SHADOW) MapPages (nBase, nSizeMask, K_VDP, 0, 0, 0, 0);
			break;
		case 1:
			if (nBoard == BOARD_S18_SHADOW)
			{
				MapPages (nBase, nSizeMask, K_VDP, 0, 0, 0, 0);
			}
			else
			{
				const u32 nLength = bBig ? 0x100000 : 0x80000;
				MapPages (nBase, nSizeMask, K_ROM, 0xffff, pRom, nLength, nLength);
				for (u32 a = nBase; nBoard == BOARD_S18_5987 && a <= nBase + nSizeMask && a < 0x1000000; a += 0x10000)
				{
					if (s_Page[a >> 16].nKind == K_ROM) s_Page[a >> 16].bBankWrite = true;
				}
			}
			break;
		case 0:
			MapPages (nBase, nSizeMask, K_ROM, 0xffff, pRom, bBig ? 0x100000 : 0x80000, 0);
			break;
		}
	}
}

// as MAME's sega_315_5195_mapper_device::update_mapping and segas16b_state::memory_mapper
static void UpdateMapping (void)
{
	const u32 *SizeMask = s_SizeMask;
	for (unsigned i = 0; i < 256; i++)
	{
		s_Page[i].nKind = K_MAPPER;
		s_Page[i].bBankWrite = false;
		s_Page[i].nMask = 0;
		s_Page[i].pRead = s_Page[i].pOpcodes = 0;
		s_Page[i].nRom = 0;
	}
	if (s_bS18)
	{
		UpdateMapping18 ();
		return;
	}
	const u8 *pRom = s_pRegion[RGN_MAIN];
	const bool bSmall = s_pGame->nBoard == BOARD_5358_SMALL, b5358 = bSmall || s_pGame->nBoard == BOARD_5358;
	const bool b5797 = s_pGame->nBoard == BOARD_5797;
	for (int nIndex = 7; nIndex >= 0; nIndex--)		// region 0 has the last word
	{
		const u32 nSizeMask = SizeMask[M.Mapper[0x10 + 2 * nIndex] & 3];
		const u32 nBase = ((u32) M.Mapper[0x11 + 2 * nIndex] << 16) & ~nSizeMask;
		switch (nIndex)
		{
		case 7: MapPages (nBase, nSizeMask, K_IO, 0x3fff, 0, 0, 0); break;
		case 6: MapPages (nBase, nSizeMask, K_PALETTE, 0x0fff, 0, 0, 0); break;
		case 5: MapTiles (nBase, nSizeMask); break;
		case 4: MapPages (nBase, nSizeMask, K_SPRITE, 0x07ff, 0, 0, 0); break;
		case 3: MapPages (nBase, nSizeMask, K_WORK, 0x3fff, M.WorkRam, 0, 0); break;
		case 2:
			if (b5358)	MapPages (nBase, nSizeMask, K_ROM, 0xffff, pRom, 0x20000, bSmall ? 0x20000 : 0x40000);
			else if (b5797)	MapPages (nBase, nSizeMask, K_COMPARE, 0, 0, 0, 0);
			else		MapPages (nBase, nSizeMask, K_BANK, 0, 0, 0, 0);
			break;
		case 1:
			if (b5358)	MapPages (nBase, nSizeMask, K_ROM, 0xffff, pRom, 0x20000, bSmall ? 0x10000 : 0x20000);
			else if (b5797)	MapPages (nBase, nSizeMask, K_MATH, 0, 0, 0, 0);
			else		MapPages (nBase, nSizeMask, K_ROM, 0xffff, pRom, 0x40000, 0x40000);
			break;
		case 0:
			MapPages (nBase, nSizeMask, K_ROM, 0xffff, pRom, b5358 ? 0x20000 : b5797 ? 0x80000 : 0x40000, 0);
			break;
		}
	}
}

static u16 MainRead16 (u32 a);
static void MainWrite16 (u32 a, u16 d, u16 nMask);

// ---- the encrypted CPUs ------------------------------------------------------------
// FD1094: the opcodes are encrypted, the data is not, and the key changes with
// the chip's state: 256 states, chosen by the program (CMPI.L #$xxxxFFFF,D0),
// plus one for the interrupts. A page of opcodes is decrypted the first time it
// is fetched in a state and kept; a change of state only forgets the pointers.
// FD1089: opcodes and data are both encrypted, each in its own way, and the key
// depends on where the mapper has put the ROM: a page is decrypted the first
// time it is used at an address and kept until it moves.
// The first 8 bytes of an FD1094 program decrypt in a special way: here they
// are taken to be at address 0, where every board has them.

#define ROM_PAGES	32		// 64 KB pages of program ROM, at most
static u8 *s_pFd1094[256][ROM_PAGES];	// by state and ROM page
static struct TFd1089 { u8 *pOpcodes, *pData; u32 nRom; } s_Fd1089[256];	// by page of the 68000

static void CryptFree (void)
{
	for (unsigned i = 0; i < 256; i++)
	{
		for (unsigned k = 0; k < ROM_PAGES; k++)
		{
			free (s_pFd1094[i][k]);
			s_pFd1094[i][k] = 0;
		}
		free (s_Fd1089[i].pOpcodes);
		free (s_Fd1089[i].pData);
		s_Fd1089[i].pOpcodes = s_Fd1089[i].pData = 0;
	}
}

// pages decrypted so far: a few ms each, so a frame that decrypts is slow for
// a reason of its own (see s16_slow_work)
static unsigned s_nDecrypted;
unsigned s16_slow_work (void)	{ return s_nDecrypted; }

static u8 Fd1094State (void)	{ return M.bFdIrq ? s16_fd1094_irq_state (s_pRegion[RGN_KEY]) : M.nFdState; }

// the chip changes state: every ROM page is looked up again at its next fetch
static void Fd1094Changed (void)
{
	for (unsigned i = 0; i < 256; i++)
	{
		if (s_Page[i].nKind == K_ROM) s_Page[i].pOpcodes = 0;
	}
}

// fills in the pointers of a ROM page that has none; false: no memory
static bool PageDecrypt (TPage &P, unsigned nPage)
{
	const u8 *pSrc = s_pRegion[RGN_MAIN] + P.nRom;
	if (s_pGame->nCpu == CPU_FD1094)
	{
		u8 *&p = s_pFd1094[Fd1094State ()][(P.nRom >> 16) % ROM_PAGES];
		if (!p)
		{
			p = (u8 *) malloc (0x10000);
			if (!p)
			{
				CryptFree ();		// start again from this page
				p = (u8 *) malloc (0x10000);
				if (!p) return false;
				Fd1094Changed ();
			}
			s16_fd1094_decrypt (s_pRegion[RGN_KEY], Fd1094State (), P.nRom, pSrc, p, 0x10000);
			s_nDecrypted++;
		}
		P.pOpcodes = p;
		return true;
	}
	TFd1089 &D = s_Fd1089[nPage];
	if (!D.pOpcodes || D.nRom != P.nRom)
	{
		if (!D.pOpcodes) D.pOpcodes = (u8 *) malloc (0x10000);
		if (!D.pData) D.pData = (u8 *) malloc (0x10000);
		if (!D.pOpcodes || !D.pData) return false;
		D.nRom = P.nRom;
		s_nDecrypted++;
		s16_fd1089_decrypt (s_pGame->nCpu == CPU_FD1089B, s_pRegion[RGN_KEY], nPage << 16, pSrc,
				    D.pOpcodes, D.pData, 0x10000);
	}
	P.pOpcodes = D.pOpcodes;
	P.pRead = D.pData;
	return true;
}

// what an unmapped address returns: the word the 68000 has just fetched
static u16 OpenBus (void)
{
	static bool bRecurse = false;
	if (bRecurse)
	{
		return 0xffff;
	}
	bRecurse = true;
	const u16 n = MainRead16 (m68k_get_reg (0, M68K_REG_PC) & 0xfffffe);
	bRecurse = false;
	return n;
}

static void MainIrq (int nLevel)	{ m68k_set_irq ((unsigned) nLevel); }
static void MainReset (void);

static void MapperWrite (unsigned nOffset, u8 nData)
{
	nOffset &= 0x1f;
	const u8 nOld = M.Mapper[nOffset];
	M.Mapper[nOffset] = nData;
	switch (nOffset)
	{
	case 0x02:			// 3 = hold the 68000 in reset, 0 = let it go
		if ((nOld ^ nData) & 3)
		{
			const bool bReset = (nData & 3) == 3;
			if (M.bMainReset && !bReset)
			{
				MainReset ();
			}
			M.bMainReset = bReset;
			m68k_end_timeslice ();
		}
		break;
	case 0x03:			// a byte for the sound CPU, with its interrupt
		M.nToSound = nData;
		if (s_bS18)		// (System 18: the NMI, an edge)
		{
			if (!M.bSoundNmi) z80_pulse_nmi (&s_Z80);
			M.bSoundNmi = true;
			break;
		}
		M.bSoundIrq = true;
		z80_assert_irq (&s_Z80, 0xff);
		break;
	case 0x04:			// interrupt lines of the 68000, inverted: $B asks for IRQ 4
		if ((nData & 7) != 7)
		{
			MainIrq (~nData & 7);
		}
		break;
	case 0x05:			// 1 = write the latch (00, 01) to the address in 0A-0C,
					// 2 = read the address in 07-09 into the latch
		if (nData == 0x01)
		{
			const u32 a = (u32) M.Mapper[0x0a] << 17 | (u32) M.Mapper[0x0b] << 9 | (u32) M.Mapper[0x0c] << 1;
			MainWrite16 (a & 0xffffff, (u16) (M.Mapper[0x00] << 8 | M.Mapper[0x01]), 0xffff);
		}
		else if (nData == 0x02)
		{
			const u32 a = (u32) M.Mapper[0x07] << 17 | (u32) M.Mapper[0x08] << 9 | (u32) M.Mapper[0x09] << 1;
			const u16 n = MainRead16 (a & 0xffffff);
			M.Mapper[0x00] = (u8) (n >> 8);
			M.Mapper[0x01] = (u8) n;
		}
		break;
	default:
		if (nOffset >= 0x10 && nOld != nData)
		{
			UpdateMapping ();
		}
		break;
	}
}

// bWide: read by the 68000 (the rest of the word is the open bus)
static u16 MapperRead (unsigned nOffset, bool bWide)
{
	nOffset &= 0x1f;
	switch (nOffset)
	{
	case 0x00: case 0x01:	return M.Mapper[nOffset];
	case 0x02:		return (M.Mapper[0x02] & 3) == 3 ? 0x00 : 0x0f;
	case 0x03:
		if (s_bS18 && s_pGame->nMcu == MCU_8751) s_Mcu.set_int (1, false);
		return M.nFromSound;
	default:		return bWide ? OpenBus () : 0xff;
	}
}

// inputs, active low as the board reads them
static u8 InputPort (int nPort)
{
	const TS16Inputs *pIn = s_pGame->pInputs;
	u8 Bits[8];
	memcpy (Bits, pIn->Port[nPort], 8);
	for (unsigned i = 0; i < pIn->nConds; i++)	// first the bits that are 0 now
	{
		const TS16CondBit &C = pIn->pConds[i];
		if (C.nPort == nPort) Bits[C.nBit] = 0;
	}
	for (unsigned i = 0; i < pIn->nConds; i++)
	{
		const TS16CondBit &C = pIn->pConds[i];
		if (C.nPort == nPort && (M.nDsw & C.nDswMask) == C.nDswValue) Bits[C.nBit] = C.nCode;
	}
	const u8 *pBits = Bits;
	u8 n = 0xff;
	for (int b = 0; b < 8; b++)
	{
		const u8 c = pBits[b];
		if (c & IN_PLAYER)
		{
			if (M.Player[(c >> 4) & 3] & (1 << (c & 7))) n &= (u8) ~(1 << b);
		}
		else if (c & IN_SYSTEM)
		{
			if (M.nSystem & (1 << (c & 7))) n &= (u8) ~(1 << b);
		}
	}
	return n;
}

static u16 IoRead (u32 nOffset)
{
	nOffset &= 0x1fff;
	switch (nOffset & (0x3000 / 2))
	{
	case 0x1000 / 2:
		return InputPort (nOffset & 3);
	case 0x2000 / 2:
		return (nOffset & 1) ? (M.nDsw & 0xff) : (M.nDsw >> 8);	// SW1, SW2
	}
	return OpenBus ();
}

static void IoWrite (u32 nOffset, u16 nData)
{
	nOffset &= 0x1fff;
	if ((nOffset & (0x3000 / 2)) == 0)
	{
		// bit 5: display on (bit 6, flip screen, is not emulated)
		const bool bDisplay = (nData & 0x20) != 0;
		if (bDisplay != V.bDisplay)
		{
			VideoSync ();
			V.bDisplay = bDisplay;
		}
	}
}

// ---- System 18: the 315-5296 I/O chip, the video control latch ------------------
// Ports A-C: players 1-3, D: outputs (flip, coin counters), E: service, F:
// SW1, G: SW2, H: the tile banks of the 171-5874 / shadow boards. CNT1: the
// display, CNT2: the VDP. D. D. Crew's 8255 adds players 3 and 4 at 3020-3026.

static void IoPortOut18 (int nPort, u8 d)
{
	if (nPort == 7 && s_pGame->nBoard != BOARD_S18_5987)
	{
		VideoSync ();
		for (int i = 0; i < 4; i++)
		{
			V.TileBank[i] = (u8) ((d & 0xf) * 4 + i);
			V.TileBank[4 + i] = (u8) (((d >> 4) & 0xf) * 4 + i);
		}
	}
}

static void IoCnt18 (u8 d)
{
	const bool bDisplay = d & 2, bVdp = d & 4;
	if (bDisplay != V.bDisplay || bVdp != V.bVdpEnable)
	{
		VideoSync ();
		V.bDisplay = bDisplay;
		V.bVdpEnable = bVdp;
	}
}

static void IoReset18 (void)
{
	memset (M.IoLatch, 0, sizeof M.IoLatch);
	M.nIoDir = M.nIoCnt = 0;
	for (int i = 0; i < 8; i++) IoPortOut18 (i, 0);
}

static u8 IoChipRead (unsigned nReg)
{
	nReg &= 0x3f;
	if (nReg < 8)
	{
		if (M.nIoDir & (1 << nReg)) return M.IoLatch[nReg];
		switch (nReg)
		{
		case 0: return InputPort (0);
		case 1: return InputPort (1);
		case 2: return InputPort (2);
		case 4: return InputPort (3);
		case 5: return (u8) M.nDsw;		// SW1
		case 6: return (u8) (M.nDsw >> 8);	// SW2
		default: return 0xff;
		}
	}
	switch (nReg)
	{
	case 0x8: return 'S';
	case 0x9: return 'E';
	case 0xa: return 'G';
	case 0xb: return 'A';
	case 0xc: case 0xe: return M.nIoCnt;
	case 0xd: case 0xf: return M.nIoDir;
	}
	return 0xff;
}

static void IoChipWrite (unsigned nReg, u8 d)
{
	nReg &= 0x3f;
	if (nReg < 8)
	{
		if (M.nIoDir & (1 << nReg)) IoPortOut18 ((int) nReg, d);
		M.IoLatch[nReg] = d;
	}
	else if (nReg == 0xe)
	{
		IoCnt18 (d);
		M.nIoCnt = d;
	}
	else if (nReg == 0xf)
	{
		for (int i = 0; i < 8; i++)
		{
			if ((M.nIoDir ^ d) & (1 << i)) IoPortOut18 (i, (d & (1 << i)) ? M.IoLatch[i] : 0);
		}
		M.nIoDir = d;
	}
}

// Laser Ghost (MAME's lghost_custom_io_r/w): writing 0x3010-0x3016 loads the
// converter's channel (offset & 3) into its shift register, reading shifts it
// out MSB first on D7. Channel 0 and 1: gun 1, Y (bottom = 0) and X; 2 and 3:
// the same for gun 2 or gun 3, as chosen by bit 0 of 0x3020.
static u8 AdcChannel (unsigned n)
{
	const unsigned g = n < 2 ? 0 : (M.nAdcSelect ? 2 : 1);
	return (n & 1) ? M.GunX[g] : (u8) (255 - M.GunY[g]);
}

static u16 IoRead18 (u32 nOffset)
{
	nOffset &= 0x1fff;
	switch (nOffset & (0x3000 / 2))
	{
	case 0x0000 / 2: case 0x1000 / 2:
		return (u16) ((OpenBus () & 0xff00) | IoChipRead (nOffset));
	case 0x2000 / 2:
		return (nOffset & 1) ? (u16) (M.nDsw & 0xff) : InputPort (3);
	}
	if (s_pGame->bPpi && nOffset >= 0x3020 / 2 && nOffset <= 0x3024 / 2)
	{
		return InputPort (4 + (nOffset & 3));
	}
	if (s_pGame->bGuns && nOffset >= 0x3010 / 2 && nOffset <= 0x3016 / 2)
	{
		const u8 b = M.nAdcShift & 0x80;
		M.nAdcShift <<= 1;
		return (u16) ((OpenBus () & 0xff00) | b | 0x7f);
	}
	return OpenBus ();
}

static void IoWrite18 (u32 nOffset, u16 d, u16 nMask)
{
	nOffset &= 0x1fff;
	if (!(nMask & 0x00ff))
	{
		return;
	}
	if (s_pGame->bGuns && nOffset >= 0x3010 / 2 && nOffset <= 0x3016 / 2)
	{
		M.nAdcShift = AdcChannel (nOffset & 3);
		return;
	}
	if (s_pGame->bGuns && nOffset == 0x3020 / 2)
	{
		M.nAdcSelect = d & 1;
		return;
	}
	switch (nOffset & (0x3000 / 2))
	{
	case 0x0000 / 2: case 0x1000 / 2:
		IoChipWrite (nOffset, (u8) d);
		break;
	case 0x2000 / 2:
		if (V.nVdpMixing != (u8) d)
		{
			VideoSync ();
			V.nVdpMixing = (u8) d;
		}
		break;
	}
}

// 171-5987: the tile banks (offsets 0-7) and the sprite banks (8-15) of
// 2 x 128 KB each
static void Rom5987Bank (u32 nOffset, u16 d, u16 nMask)
{
	if (!(nMask & 0x00ff))
	{
		return;
	}
	nOffset &= 0xf;
	unsigned n = d & 0xff;
	VideoSync ();
	if (nOffset < 8)
	{
		const unsigned nMax = s16_video_tiles () >> 10;
		if (nMax && n >= nMax) n %= nMax;
		V.TileBank[nOffset] = (u8) n;
	}
	else
	{
		const unsigned nMax = s16_video_sprite_banks () / 2;
		V.SprBank[(nOffset - 8) * 2 + 0] = n >= nMax ? 255 : (u8) (n * 2 + 0);
		V.SprBank[(nOffset - 8) * 2 + 1] = n >= nMax ? 255 : (u8) (n * 2 + 1);
	}
}

// the VDP: its H counter from the 68000's place in the line
static u16 VdpBusRead (u32 a)	{ return MainRead16 (a & 0xfffffe); }

static u16 VdpRead (u32 a)
{
	const u32 nPerLine = s_nMainPerLine >> 16;
	const u32 nDone = M.nMainLineDone + (u32) m68k_cycles_run ();
	int nHpos = (int) (nDone * 342 / nPerLine);
	if (nHpos > 341) nHpos = 341;
	return s18_vdp_read (&V.Vdp, a & 0xf, M.nLine, nHpos);
}

static void VdpWrite (u32 a, u16 d, u16 nMask)
{
	VideoSync ();
	TS18VdpBus Bus = {VdpBusRead, 0};
	s18_vdp_write (&V.Vdp, a & 0xf, d, nMask, V.Colors + 0x1000, &Bus);
	if (Bus.nStall)
	{
		M.nMainStall += (int) Bus.nStall;
		m68k_end_timeslice ();
	}
}

// ---- 171-5797: the math chips ---------------------------------------------------
// As MAME's sega_315_5248_multiplier_device and sega_315_5250_compare_timer_device.
// On this board nothing clocks the 315-5250's timer and its interrupt and sound
// lines go nowhere: only the compare part is alive.

static u16 MultRead (u32 nOffset)
{
	const s32 n = (s32) (s16) M.Mult[0] * (s16) M.Mult[1];
	switch (nOffset & 3)
	{
	case 0:  return M.Mult[0];
	case 1:  return M.Mult[1];
	case 2:  return (u16) (n >> 16);
	default: return (u16) n;
	}
}

static void CompareExecute (TMachine::TCompare &C, bool bHistory)
{
	const s16 b1 = (s16) C.Regs[0], b2 = (s16) C.Regs[1], v = (s16) C.Regs[2];
	const s16 nMin = b1 < b2 ? b1 : b2, nMax = b1 > b2 ? b1 : b2;
	if (v < nMin)		{ C.Regs[7] = (u16) nMin; C.Regs[3] = 0x8000; }
	else if (v > nMax)	{ C.Regs[7] = (u16) nMax; C.Regs[3] = 0x4000; }
	else			{ C.Regs[7] = (u16) v;	  C.Regs[3] = 0x0000; }
	if (bHistory)
	{
		C.Regs[4] |= (u16) ((C.Regs[3] == 0) << (C.nBit++ & 15));
	}
}

static u16 CompareRead (const TMachine::TCompare &C, u32 nOffset)
{
	switch (nOffset & 15)
	{
	case 0x0: return C.Regs[0];
	case 0x1: return C.Regs[1];
	case 0x2: return C.Regs[2];
	case 0x3: return C.Regs[3];
	case 0x4: return C.Regs[4];
	case 0x5: return C.Regs[1];
	case 0x6: return C.Regs[2];
	case 0x7: return C.Regs[7];
	}
	return 0xffff;
}

static void CompareWrite (TMachine::TCompare &C, u32 nOffset, u16 d, u16 nMask)
{
#define COMBINE(r)	C.Regs[r] = (u16) ((C.Regs[r] & ~nMask) | (d & nMask))
	switch (nOffset & 15)
	{
	case 0x0: COMBINE (0); CompareExecute (C, false); break;
	case 0x1: COMBINE (1); CompareExecute (C, false); break;
	case 0x2: COMBINE (2); CompareExecute (C, true); break;
	case 0x4: C.Regs[4] = 0; C.nBit = 0; break;
	case 0x6: COMBINE (2); CompareExecute (C, false); break;
	case 0x8: case 0xc: COMBINE (8); break;
	case 0xa: case 0xe: COMBINE (10); break;
	case 0xb: case 0xf: C.Regs[11] = d & 0xff; break;
	}
#undef COMBINE
}

static u16 MathRead (u32 nOffset)
{
	nOffset &= 0x1fff;
	switch (nOffset & (0x3000 / 2))
	{
	case 0x0000 / 2: return MultRead (nOffset);
	case 0x1000 / 2: return CompareRead (M.Compare[0], nOffset);
	}
	return OpenBus ();
}

static void MathWrite (u32 nOffset, u16 d, u16 nMask)
{
	nOffset &= 0x1fff;
	switch (nOffset & (0x3000 / 2))
	{
	case 0x0000 / 2:
		M.Mult[nOffset & 1] = (u16) ((M.Mult[nOffset & 1] & ~nMask) | (d & nMask));
		break;
	case 0x1000 / 2:
		CompareWrite (M.Compare[0], nOffset, d, nMask);
		break;
	case 0x2000 / 2:		// the two tile banks
		if (nMask & 0x00ff)
		{
			const unsigned b = nOffset & 1;
			if (V.TileBank[b] != (d & 7)) { VideoSync (); V.TileBank[b] = d & 7; }
		}
		break;
	}
}

static u16 MainRead16 (u32 a)
{
	const TPage &P = s_Page[a >> 16];
	switch (P.nKind)
	{
	case K_ROM:
		if (!P.pRead && !PageDecrypt (s_Page[a >> 16], a >> 16)) return 0xffff;
		[[fallthrough]];
	case K_WORK: { const u8 *p = P.pRead + (a & P.nMask & ~1u); return (u16) (p[0] << 8 | p[1]); }
	case K_SPRITE:	return V.SprRam[(a & 0x7ff) >> 1];
	case K_PALETTE:	return V.PalRam[(a & 0xfff) >> 1];
	case K_TILE:	return V.TileRam[(a & 0xffff) >> 1];
	case K_TEXT:	return V.TextRam[(a & 0xfff) >> 1];
	case K_IO:	return s_bS18 ? IoRead18 ((a & 0x3fff) >> 1) : IoRead ((a & 0x3fff) >> 1);
	case K_VDP:	return VdpRead (a);
	case K_MATH:	return MathRead ((a & 0x3fff) >> 1);
	case K_COMPARE:	return CompareRead (M.Compare[1], (a & 0xffff) >> 1);
	default:	return (u16) (0xff00 | (MapperRead (a >> 1, true) & 0xff));
	}
}

static void MainWrite16 (u32 a, u16 d, u16 nMask)
{
	const TPage &P = s_Page[a >> 16];
	switch (P.nKind)
	{
	case K_WORK: {
		u8 *p = M.WorkRam + (a & 0x3ffe);
		if (nMask & 0xff00) p[0] = (u8) (d >> 8);
		if (nMask & 0x00ff) p[1] = (u8) d;
		break; }
	case K_SPRITE: {
		u16 &r = V.SprRam[(a & 0x7ff) >> 1];
		const u16 n = (u16) ((r & ~nMask) | (d & nMask));
		if (n != r) { VideoSync (); r = n; }
		break; }
	case K_PALETTE: {
		const unsigned i = (a & 0xfff) >> 1;
		const u16 n = (u16) ((V.PalRam[i] & ~nMask) | (d & nMask));
		if (n != V.PalRam[i])
		{
			VideoSync ();
			s16_video_palette_w (&V, i, n);
		}
		break; }
	case K_TILE: {
		u16 &r = V.TileRam[(a & 0xffff) >> 1];
		const u16 n = (u16) ((r & ~nMask) | (d & nMask));
		if (n != r) { VideoSync (); r = n; }
		break; }
	case K_TEXT: {
		u16 &r = V.TextRam[(a & 0xfff) >> 1];
		const u16 n = (u16) ((r & ~nMask) | (d & nMask));
		if (n != r) { VideoSync (); r = n; }
		break; }
	case K_IO:
		if (s_bS18)	IoWrite18 ((a & 0x3fff) >> 1, d, nMask);
		else		IoWrite ((a & 0x3fff) >> 1, d);
		break;
	case K_VDP:	VdpWrite (a, d, nMask); break;
	case K_BANK:			// 171-5521 / 5704: the two tile banks
		if (nMask & 0x00ff)
		{
			const unsigned b = (a >> 1) & 1;
			if (V.TileBank[b] != (d & 7)) { VideoSync (); V.TileBank[b] = d & 7; }
		}
		break;
	case K_MATH:	MathWrite ((a & 0x3fff) >> 1, d, nMask); break;
	case K_COMPARE:	CompareWrite (M.Compare[1], (a & 0xffff) >> 1, d, nMask); break;
	case K_MAPPER:
		if (nMask & 0x00ff) MapperWrite (a >> 1, (u8) d);
		break;
	case K_ROM:
		if (P.bBankWrite) Rom5987Bank (a >> 1, d, nMask);
		break;
	default:	break;
	}
}

// Musashi's memory interface
extern "C" {
unsigned m68k_read_memory_8 (unsigned a)
{
	a &= 0xffffff;
	const TPage &P = s_Page[a >> 16];
	if (P.pRead) return P.pRead[a & P.nMask];
	const u16 n = MainRead16 (a & ~1u);
	return (a & 1) ? (n & 0xff) : (n >> 8);
}
unsigned m68k_read_memory_16 (unsigned a)
{
	a &= 0xffffff;
	const TPage &P = s_Page[a >> 16];
	if (P.pRead) { const u8 *p = P.pRead + (a & P.nMask); return (unsigned) (p[0] << 8 | p[1]); }
	return MainRead16 (a);
}
unsigned m68k_read_memory_32 (unsigned a)
{
	return m68k_read_memory_16 (a) << 16 | m68k_read_memory_16 (a + 2);
}
void m68k_write_memory_8 (unsigned a, unsigned d)
{
	a &= 0xffffff;
	if (a & 1)	MainWrite16 (a & ~1u, (u16) (d & 0xff), 0x00ff);
	else		MainWrite16 (a, (u16) ((d & 0xff) << 8), 0xff00);
}
void m68k_write_memory_16 (unsigned a, unsigned d)
{
	MainWrite16 (a & 0xffffff, (u16) d, 0xffff);
}
void m68k_write_memory_32 (unsigned a, unsigned d)
{
	MainWrite16 (a & 0xffffff, (u16) (d >> 16), 0xffff);
	MainWrite16 ((a + 2) & 0xffffff, (u16) d, 0xffff);
}
// opcodes and their operands; PC-relative data is data
unsigned m68k_read_immediate_16 (unsigned a)
{
	a &= 0xffffff;
	TPage &P = s_Page[a >> 16];
	if (P.nKind != K_ROM)
	{
		return MainRead16 (a & ~1u);
	}
	if (!P.pOpcodes && !PageDecrypt (P, a >> 16))
	{
		return 0xffff;
	}
	const u8 *p = P.pOpcodes + (a & 0xfffe);
	return (unsigned) (p[0] << 8 | p[1]);
}
unsigned m68k_read_immediate_32 (unsigned a)
{
	return m68k_read_immediate_16 (a) << 16 | m68k_read_immediate_16 (a + 2);
}
unsigned m68k_read_pcrelative_8 (unsigned a)	{ return m68k_read_memory_8 (a); }
unsigned m68k_read_pcrelative_16 (unsigned a)	{ return m68k_read_memory_16 (a); }
unsigned m68k_read_pcrelative_32 (unsigned a)	{ return m68k_read_memory_32 (a); }

// an interrupt is taken: the line goes back (MAME's HOLD_LINE), autovectored
int s16_m68k_int_ack (int)
{
	m68k_set_irq (0);
	if (s_pGame->nCpu == CPU_FD1094 && !M.bFdIrq)
	{
		M.bFdIrq = true;
		Fd1094Changed ();
	}
	return (int) M68K_INT_ACK_AUTOVECTOR;
}

// FD1094, as MAME's fd1094_device::change_state: CMPI.L #$xxxxFFFF,D0 sets the
// state (and with $01xx leaves the interrupt state too), RTE leaves it
void s16_m68k_cmpild (unsigned nValue, int nReg)
{
	if (s_pGame->nCpu != CPU_FD1094 || nReg != 0 || (nValue & 0xffff) != 0xffff)
	{
		return;
	}
	const unsigned n = nValue >> 16;
	switch (n & 0x300)
	{
	case 0x000: M.nFdState = (u8) n; break;
	case 0x100: M.nFdState = (u8) n; M.bFdIrq = false; break;
	case 0x200: M.bFdIrq = true; break;
	case 0x300: M.bFdIrq = false; break;
	}
	Fd1094Changed ();
}

void s16_m68k_rte (void)
{
	if (s_pGame->nCpu == CPU_FD1094 && M.bFdIrq)
	{
		M.bFdIrq = false;
		Fd1094Changed ();
	}
}
}

// the 68000's reset; the FD1094 goes back to its first state
static void MainReset (void)
{
	M.nFdState = 0;
	M.bFdIrq = false;
	if (s_pGame->nCpu == CPU_FD1094)
	{
		Fd1094Changed ();
	}
	m68k_pulse_reset ();
}

// ---- 8751 ---------------------------------------------------------------------
// It sees the mapper's registers as its external memory, reads coins and
// starts on port 1, and is interrupted at each vertical blank.

static u8 McuXRead (void *, u16 a)		{ return (a & 0xff) < 0x20 ? (u8) MapperRead (a, false) : 0xff; }
static void McuXWrite (void *, u16 a, u8 d)	{ if ((a & 0xff) < 0x20) MapperWrite (a, d); }
static u8 McuPortRead (void *, int nPort)	{ return nPort == 1 && !s_bS18 ? InputPort (0) : 0xff; }
static void McuPortWrite (void *, int nPort, u8)
{
	if (s_bS18)			// (not connected on System 18)
	{
		return;
	}
	// as MAME (spin_68k_w): a write to port 1 takes the bus from the 68000 for
	// a while; without it the two fight over the mapper (Golden Axe hangs)
	if (nPort == 1)
	{
		M.nMainStall = 20000;
		m68k_end_timeslice ();
	}
}

// ---- sound CPU ---------------------------------------------------------------------

static void SoundNmi (void)	{ z80_pulse_nmi (&s_Z80); }

// MC-8123: the program's opcodes, decrypted apart from its data
static u8 *s_pSoundOpcodes;

static u8 SoundRead (void *, u16 a);
extern "C" u8 s16_z80_opcode (void *, u16 a)
{
	return a < 0x8000 && s_pSoundOpcodes ? s_pSoundOpcodes[a] : SoundRead (0, a);
}

static u8 SoundRead (void *, u16 a)
{
	const u8 *pRom = s_pRegion[RGN_SOUND];
	if (a < 0x8000)		return pRom[a];
	if (a < 0xe000)		return pRom[0x10000 + M.nSoundBank + (a - 0x8000)];
	if (a == 0xe800)	{ M.bSoundIrq = false; z80_clr_irq (&s_Z80); return M.nToSound; }
	if (a >= 0xf800)	return M.SoundRam[a & 0x7ff];
	return 0xff;
}

static void SoundWrite (void *, u16 a, u8 d)
{
	if (a >= 0xf800) M.SoundRam[a & 0x7ff] = d;
}

static u8 SoundPortIn (z80 *, u16 nPort)
{
	switch (nPort & 0xc0)
	{
	case 0x00: return s16_sound_ym_r (nPort);
	case 0x80: return s16_sound_upd_idle () ? 0x80 : 0x00;	// /BUSY
	case 0xc0: M.bSoundIrq = false; z80_clr_irq (&s_Z80); return M.nToSound;
	}
	return 0xff;
}

static void SoundPortOut (z80 *, u16 nPort, u8 d)
{
	if ((nPort & 0xc0) == 0x00)
	{
		s16_sound_ym_w (nPort, d);
	}
	else if ((nPort & 0xc0) == 0x80)
	{
		s16_sound_upd_w (d);
	}
	else if ((nPort & 0xc0) == 0x40 && s_pGame->RegionSize[RGN_SOUND] > 0x10000)
	{
		// uPD7759 control and the sample ROM bank. /MD first: if it went low
		// together with /RESET, no sample must start
		s16_sound_upd_control (!(d & 0x80), (d & 0x40) != 0);
		const u32 nSize = s_pGame->RegionSize[RGN_SOUND] - 0x10000;
		u32 nBank;
		if (s_pGame->nBoard == BOARD_5358 || s_pGame->nBoard == BOARD_5358_SMALL)
		{
			nBank = 0;
			if (!(d & 0x04)) nBank = 0x00000;
			if (!(d & 0x08)) nBank = 0x10000;
			if (!(d & 0x10)) nBank = 0x20000;
			if (!(d & 0x20)) nBank = 0x30000;
			nBank += (d & 0x03) * 0x4000;
		}
		else if (s_pGame->nBoard == BOARD_5797)
		{
			nBank = ((d & 0x08) >> 3) * 0x40000 + ((d & 0x10) >> 4) * 0x20000 + (d & 0x07) * 0x4000;
		}
		else
		{
			nBank = ((d & 0x08) >> 3) * 0x20000 + (d & 0x07) * 0x4000;
		}
		M.nSoundBank = nSize ? nBank % nSize : 0;
	}
}

// ---- System 18 sound CPU ------------------------------------------------------------
// 0000-9FFF program, A000-BFFF a bank of 8 KB of the sound region, C000-DFFF
// the RF5C68 (registers, then a 4 KB window on its wave RAM), E000-FFFF RAM.
// Ports: 80 and 90 the two YM3438, A0 the bank, C0 the mapper's latches.

static void Sound18Irq (bool bOn)
{
	if (bOn)	z80_assert_irq (&s_Z80, 0xff);
	else		z80_clr_irq (&s_Z80);
}

static u8 Sound18Read (void *, u16 a)
{
	const u8 *pRom = s_pRegion[RGN_SOUND];
	if (a < 0xa000)		return pRom[a];
	if (a < 0xc000)		return pRom[(M.nSoundBank + (a - 0xa000)) % s_pGame->RegionSize[RGN_SOUND]];
	if (a < 0xd000)		return 0xff;
	if (a < 0xe000)		return s18_sound_rf_mem_r (a);
	return M.SoundRam[a & 0x1fff];
}

static void Sound18Write (void *, u16 a, u8 d)
{
	if (a >= 0xe000)	M.SoundRam[a & 0x1fff] = d;
	else if (a >= 0xd000)	s18_sound_rf_mem_w (a, d);
	else if (a >= 0xc000)	s18_sound_rf_w (a, d);
}

static u8 Sound18PortIn (z80 *, u16 nPort)
{
	nPort &= 0xff;
	switch (nPort & 0xf0)
	{
	case 0x80: return s18_sound_ym_r (0, nPort);
	case 0x90: return s18_sound_ym_r (1, nPort);
	case 0xc0: case 0xd0: M.bSoundNmi = false; return M.nToSound;
	}
	return 0xff;
}

static void Sound18PortOut (z80 *, u16 nPort, u8 d)
{
	nPort &= 0xff;
	switch (nPort & 0xf0)
	{
	case 0x80: s18_sound_ym_w (0, nPort, d); break;
	case 0x90: s18_sound_ym_w (1, nPort, d); break;
	case 0xa0: case 0xb0: M.nSoundBank = (u32) d * 0x2000; break;
	case 0xc0: case 0xd0:
		M.nFromSound = d;
		if (s_pGame->nMcu == MCU_8751) s_Mcu.set_int (1, true);
		break;
	}
}

static void SoundHooks (void)
{
	s_Z80.read_byte = s_bS18 ? Sound18Read : SoundRead;
	s_Z80.write_byte = s_bS18 ? Sound18Write : SoundWrite;
	s_Z80.port_in = s_bS18 ? Sound18PortIn : SoundPortIn;
	s_Z80.port_out = s_bS18 ? Sound18PortOut : SoundPortOut;
	s_Z80.userdata = 0;
}

// ---- the machine ----------------------------------------------------------------

static void Reset (void)
{
	memset (&M.WorkRam, 0, sizeof M.WorkRam);
	memset (&M.SoundRam, 0, sizeof M.SoundRam);
	memset (M.Mapper, 0, sizeof M.Mapper);
	M.nToSound = M.nFromSound = 0;
	M.bSoundIrq = false;
	M.bMainReset = false;
	M.nMainFrac = M.nSoundFrac = M.nMcuFrac = 0;
	M.nMainStall = 0;
	M.nSoundBank = 0;
	M.nLine = S16_HEIGHT;
	M.nFrame = 0;
	M.bSoundNmi = false;
	M.nMainLineDone = 0;
	s16_video_reset (&V);
	if (s_bS18)
	{
		V.bDisplay = false;
		IoReset18 ();
	}
	UpdateMapping ();
	MainReset ();
	z80_reset (&s_Z80);
	if (s_bS18)	s18_sound_reset ();
	else		s16_sound_reset ();
	if ((s_pGame->nMcu == MCU_8751))
	{
		s_Mcu.reset ();
	}
}

int s16_load (int nGame, TS16Fetch *pFetch, void *pCtx, char *pMissing, size_t nMissing)
{
	s16_unload ();
	if (nGame < 0 || nGame >= s16_num_games ())
	{
		return 0;
	}
	if (nMissing) pMissing[0] = '\0';
	const TS16Game *pGame = &s_Games[nGame];
	for (int r = 0; r < RGN_COUNT; r++)
	{
		const u32 nSize = pGame->RegionSize[r];
		s_pRegion[r] = nSize ? (u8 *) malloc (nSize) : 0;
		if (nSize && !s_pRegion[r])
		{
			s16_unload ();
			return 0;
		}
		if (nSize) memset (s_pRegion[r], 0xff, nSize);
	}
	u8 *pTemp = 0;
	u32 nTempSize = 0;
	const char *pHave = 0;
	for (int i = 0; i < pGame->nRoms; i++)
	{
		const TS16Rom &R = pGame->pRoms[i];
		u8 *pDst = s_pRegion[R.nRegion] + R.nOffset;
		const u32 nLength = R.nLength ? R.nLength : R.nSize;
		if (R.nLoad == LOAD_BYTES && !R.nLength)
		{
			if (!pFetch (pCtx, R.pName, R.nCRC, R.nSize, pDst)) goto missing;
			continue;
		}
		if (R.nSize > nTempSize)
		{
			pHave = 0;
			free (pTemp);
			pTemp = (u8 *) malloc (nTempSize = R.nSize);
		}
		if (pHave != R.pName)			// (the parts of a file follow each other)
		{
			pHave = 0;
			if (!pTemp || !pFetch (pCtx, R.pName, R.nCRC, R.nSize, pTemp)) goto missing;
			pHave = R.pName;
		}
		if (R.nLoad == LOAD_BYTES)
		{
			memcpy (pDst, pTemp + R.nFrom, nLength);
			continue;
		}
		for (u32 k = 0; k < nLength; k++)
		{
			pDst[k * 2] = pTemp[R.nFrom + k];	// (the offset says which byte of the word)
		}
		continue;
	missing:
		if (nMissing)
		{
			strncpy (pMissing, R.pName, nMissing - 1);
			pMissing[nMissing - 1] = '\0';
		}
		free (pTemp);
		s16_unload ();
		return 0;
	}
	free (pTemp);

	s_pGame = pGame;
	s_bS18 = S18Board (pGame->nBoard);
	{
		const double fLine = s_bS18 ? LINE_HZ_18 : LINE_HZ_16B;
		s_nMainPerLine = FIX (10000000.0, fLine);
		s_nSoundPerLine = FIX (s_bS18 ? 8000000.0 : 5000000.0, fLine);
		s_nMcuPerLine = FIX (8000000.0 / 12, fLine);
	}
	s16_video_init (s_pRegion[RGN_TILES], pGame->RegionSize[RGN_TILES],
			s_pRegion[RGN_SPRITES], pGame->RegionSize[RGN_SPRITES],
			pGame->nBoard == BOARD_5358 || pGame->nBoard == BOARD_5358_SMALL,
			s_bS18 ? 0x400 : 0x1000, s_bS18);
	memset (&M, 0, sizeof M);
	M.nDsw = pGame->pInputs->nDswDefault;
	memset (M.GunX, 128, sizeof M.GunX);
	memset (M.GunY, 128, sizeof M.GunY);

	m68k_init ();
	m68k_set_cpu_type (M68K_CPU_TYPE_68000);
	z80_init (&s_Z80);
	SoundHooks ();
	if (s_bS18)	s18_sound_init (Sound18Irq);
	else		s16_sound_init (SoundNmi);
	if (pGame->bMc8123)
	{
		s_pSoundOpcodes = (u8 *) malloc (0x8000);
		if (!s_pSoundOpcodes)
		{
			s16_unload ();
			return 0;
		}
		s16_mc8123_decode (s_pRegion[RGN_SOUNDKEY], s_pRegion[RGN_SOUND], s_pSoundOpcodes, 0x8000);
	}
	if (pGame->nMcu == MCU_8751)
	{
		const i8751_cpu::TBus Bus = {0, McuXRead, McuXWrite, McuPortRead, McuPortWrite};
		s_Mcu.init (s_pRegion[RGN_MCU], pGame->RegionSize[RGN_MCU], Bus);
	}
	s_bLoaded = true;
	Reset ();
	return 1;
}

void s16_unload (void)
{
	if (s_bLoaded)
	{
		VideoSync ();
		s16_video_exit ();
	}
	for (int r = 0; r < RGN_COUNT; r++)
	{
		free (s_pRegion[r]);
		s_pRegion[r] = 0;
	}
	CryptFree ();
	free (s_pSoundOpcodes);
	s_pSoundOpcodes = 0;
	s_bLoaded = false;
	s_bS18 = false;
	s_pGame = 0;
}

void s16_reset (void)
{
	if (s_bLoaded)
	{
		VideoSync ();
		const u16 nDsw = M.nDsw;
		Reset ();
		M.nDsw = nDsw;
	}
}

// the CPUs for one line, in turns
static void RunLine (void)
{
	for (int s = 0; s < SLICES; s++)
	{
		M.nMainFrac += (s32) (s_nMainPerLine / SLICES);
		int nMain = M.nMainFrac >> 16;
		if (nMain > 0 && M.nMainStall > 0)	// the 68000 loses these cycles
		{
			const int n = nMain < M.nMainStall ? nMain : M.nMainStall;
			M.nMainStall -= n;
			M.nMainFrac -= n << 16;
			nMain -= n;
		}
		if (nMain > 0)
		{
			const int nRun = M.bMainReset ? nMain : m68k_execute (nMain);
			M.nMainFrac -= nRun << 16;
			M.nMainLineDone += (u32) nRun;
		}

		if ((s_pGame->nMcu == MCU_8751))
		{
			M.nMcuFrac += (s32) (s_nMcuPerLine / SLICES);
			if (M.nMcuFrac >= 0x10000)
			{
				M.nMcuFrac -= s_Mcu.run (M.nMcuFrac >> 16) << 16;
			}
		}

		M.nSoundFrac += (s32) (s_nSoundPerLine / SLICES);
		if (M.nSoundFrac >= 0x10000)
		{
			const unsigned nUsed = z80_step_n (&s_Z80, (unsigned) (M.nSoundFrac >> 16));
			M.nSoundFrac -= (s32) (nUsed << 16);
			if (s_bS18)	s18_sound_advance (nUsed);
			else		s16_sound_advance (nUsed);
		}
	}
}

// The vertical blank without a real 8751: the interrupt and, for the two games
// whose 8751 is not dumped, what MAME does in its place (tturf_i8751_sim,
// wb3_i8751_sim): the sound command and the inputs pass through work RAM.
static void McuStandIn (void)
{
	MainIrq (4);
	u8 *const w = M.WorkRam;
	if (s_pGame->nMcu == MCU_SIM_TTURF)
	{
		if (w[0x1d0])
		{
			MapperWrite (0x03, w[0x1d1]);
			w[0x1d0] = 0;
		}
		w[0x1e6] = InputPort (0);	w[0x1e7] = 0;
		w[0x1e8] = InputPort (1);	w[0x1e9] = 0;
		w[0x1ea] = InputPort (3);	w[0x1eb] = 0;
	}
	else if (s_pGame->nMcu == MCU_SIM_WB3)
	{
		if (w[0x009])
		{
			MapperWrite (0x03, w[0x008]);
			w[0x009] = 0;
		}
	}
}

void s16_run_frame (void)
{
	if (!s_bLoaded)
	{
		return;
	}
	for (int i = 0; i < S16_LINES; i++)
	{
		const int nLine = M.nLine;
		if (s_pHook)
		{
			s_pHook (s_pHookCtx, nLine);
		}
		if (nLine == S16_HEIGHT)		// vertical blank
		{
			if (s_pGame->nMcu == MCU_8751)	s_Mcu.set_int (0, true);
			else				McuStandIn ();
			if (s_bS18)			s18_vdp_vblank (&V.Vdp);
		}
		else if (nLine == S16_LINES - 1)	// the scroll registers for the next frame
		{
			VideoSync ();
			s16_video_latch (&V);
		}
		else if (nLine == 0)
		{
			if ((s_pGame->nMcu == MCU_8751))	s_Mcu.set_int (0, false);
			if (s_bS18)			s18_vdp_frame (&V.Vdp);
		}
		if (nLine < S16_HEIGHT)
		{
			if (s_pSink)
			{
				const TS16LineRegs Regs = {nLine};
				s_pSink (s_pSinkCtx, nLine, &Regs);
			}
			else if (s_pLineCB)
			{
				u32 Line[S16_WIDTH];
				s16_video_line (&V, nLine, Line);
				s_pLineCB (s_pLineCtx, nLine, Line);
			}
		}
		M.nMainLineDone = 0;
		RunLine ();
		M.nLine = (nLine + 1) % S16_LINES;
	}
	M.nFrame++;
}

void s16_render_line (const TS16LineRegs *, int y, u32 *pOut)
{
	s16_video_line (&V, y, pOut);
}

// ---- the rest of the interface -----------------------------------------------

int s16_num_games (void)			{ return (int) (sizeof s_Games / sizeof s_Games[0]); }
int s16_find (const char *pName)
{
	for (int i = 0; i < s16_num_games (); i++)
	{
		if (strcmp (s_Games[i].pName, pName) == 0) return i;
	}
	return -1;
}
static const TS16Game *Game (int n)		{ return n >= 0 && n < s16_num_games () ? &s_Games[n] : 0; }
const char *s16_game_name (int n)		{ return Game (n) ? Game (n)->pName : ""; }
const char *s16_game_parent (int n)		{ return Game (n) ? Game (n)->pParent : ""; }
const char *s16_game_desc (int n)		{ return Game (n) ? Game (n)->pDesc : ""; }
const char *s16_game_year (int n)		{ return Game (n) ? Game (n)->pYear : ""; }
const char *s16_game_maker (int n)		{ return Game (n) ? Game (n)->pMaker : ""; }
int s16_game_players (int n)			{ return Game (n) ? Game (n)->pInputs->nPlayers : 0; }
int s16_game_buttons (int n)			{ return Game (n) ? Game (n)->pInputs->nButtons : 0; }

int s16_dip_count (int n)
{
	int c = 0;
	if (Game (n))
	{
		for (const TS16DipDef *d = Game (n)->pInputs->pDips; d->pName; d++) c++;
	}
	return c;
}

int s16_dip_get (int n, int i, TS16Dip *pDip)
{
	if (i < 0 || i >= s16_dip_count (n))
	{
		return 0;
	}
	const TS16DipDef &D = Game (n)->pInputs->pDips[i];
	pDip->pName = D.pName;
	pDip->pLocation = D.pLocation;
	pDip->nMask = D.nMask;
	pDip->nDefault = D.nDefault;
	pDip->nSettings = D.nCount;
	pDip->nCondMask = pDip->nCondValue = 0;
	pDip->nCondOp = 0;
	return 1;
}

const char *s16_dip_setting_name (int n, int i, int nSetting, u16 *pValue)
{
	if (i < 0 || i >= s16_dip_count (n))
	{
		return 0;
	}
	const TS16DipDef &D = Game (n)->pInputs->pDips[i];
	if (nSetting < 0 || nSetting >= D.nCount)
	{
		return 0;
	}
	const TS16DipSetting &S = Game (n)->pInputs->pSettings[D.nFirst + nSetting];
	if (pValue) *pValue = S.nValue;
	return S.pName;
}

u16 s16_dsw_default (int n)			{ return Game (n) ? Game (n)->pInputs->nDswDefault : 0xffff; }

void s16_set_player (int p, u8 nBits)		{ if (p >= 0 && p < 4) M.Player[p] = nBits; }
void s16_set_system (u8 nBits)			{ M.nSystem = nBits; }
void s16_set_gun (int p, u8 nX, u8 nY)		{ if (p >= 0 && p < 3) { M.GunX[p] = nX; M.GunY[p] = nY; } }
int s16_guns (void)				{ return s_pGame && s_pGame->bGuns ? 3 : 0; }
void s16_set_dsw (u16 nDsw)			{ M.nDsw = nDsw; }

void s16_set_line_callback (TS16LineCB *pCB, void *pCtx)	{ s_pLineCB = pCB; s_pLineCtx = pCtx; }
void s16_set_line_hook (TS16LineHook *pHook, void *pCtx)	{ s_pHook = pHook; s_pHookCtx = pCtx; }
void s16_set_line_sink (TS16LineSink *pSink, void *pCtx)	{ s_pSink = pSink; s_pSinkCtx = pCtx; }
void s16_set_video_sync (void (*pSync) (void *), void *pCtx)	{ s_pSync = pSync; s_pSyncCtx = pCtx; }

int s16_audio_read (int16_t *pDst, int nMaxFrames)
{
	return s_bS18 ? s18_sound_read (pDst, nMaxFrames) : s16_sound_read (pDst, nMaxFrames);
}

// ---- save states -------------------------------------------------------------------
// A header, then the machine, the video RAM, the three CPUs and the sound, one
// after the other. A state is for the game (and the build) that made it.

struct TStateHead
{
	char Magic[8];			// "S16PI\0" and the format's version
	char Game[16];
	u32 nSize;
};
static const char s_StateMagic[8] = {'S', '1', '6', 'P', 'I', 0, 0, 3};

// the sound's part has no fixed size: room for it (System 18: 64 KB of wave RAM too)
#define STATE_SOUND_MAX		0x14000

size_t s16_state_size (void)
{
	return sizeof (TStateHead) + sizeof M + sizeof V + s16_m68k_state_size () + sizeof s_Z80
	       + s_Mcu.state_size () + STATE_SOUND_MAX;
}

int s16_state_save (void *pBuffer, size_t nSize)
{
	if (!s_bLoaded || nSize < s16_state_size ())
	{
		return 0;
	}
	VideoSync ();
	u8 *p = (u8 *) pBuffer + sizeof (TStateHead);
	memcpy (p, &M, sizeof M);				p += sizeof M;
	memcpy (p, &V, sizeof V);				p += sizeof V;
	s16_m68k_state_save (p);				p += s16_m68k_state_size ();
	memcpy (p, &s_Z80, sizeof s_Z80);			p += sizeof s_Z80;
	memcpy (p, s_Mcu.state_ptr (), s_Mcu.state_size ());	p += s_Mcu.state_size ();
	const size_t nSound = s_bS18 ? s18_sound_state (p, STATE_SOUND_MAX, true) : s16_sound_state (p, STATE_SOUND_MAX, true);
	if (nSound == 0)
	{
		return 0;
	}
	p += nSound;

	TStateHead H;
	memset (&H, 0, sizeof H);
	memcpy (H.Magic, s_StateMagic, sizeof H.Magic);
	strncpy (H.Game, s_pGame->pName, sizeof H.Game - 1);
	H.nSize = (u32) (p - (u8 *) pBuffer);
	memcpy (pBuffer, &H, sizeof H);
	return (int) H.nSize;
}

int s16_state_load (const void *pBuffer, size_t nSize)
{
	TStateHead H;
	if (!s_bLoaded || nSize < sizeof H)
	{
		return 0;
	}
	memcpy (&H, pBuffer, sizeof H);
	const size_t nFixed = sizeof H + sizeof M + sizeof V + s16_m68k_state_size () + sizeof s_Z80 + s_Mcu.state_size ();
	if (memcmp (H.Magic, s_StateMagic, sizeof H.Magic) != 0 || strncmp (H.Game, s_pGame->pName, sizeof H.Game) != 0
	    || H.nSize > nSize || H.nSize <= nFixed)
	{
		return 0;
	}
	VideoSync ();
	const u8 *p = (const u8 *) pBuffer + sizeof H;
	const u8 Player[4] = {M.Player[0], M.Player[1], M.Player[2], M.Player[3]};
	const u8 nSystem = M.nSystem;
	const u16 nDsw = M.nDsw;
	u8 Gun[6];
	memcpy (Gun, M.GunX, 3);
	memcpy (Gun + 3, M.GunY, 3);
	memcpy (&M, p, sizeof M);				p += sizeof M;
	memcpy (&V, p, sizeof V);				p += sizeof V;
	s16_m68k_state_load (p);				p += s16_m68k_state_size ();
	memcpy (&s_Z80, p, sizeof s_Z80);			p += sizeof s_Z80;
	memcpy (s_Mcu.state_ptr (), p, s_Mcu.state_size ());	p += s_Mcu.state_size ();
	if ((s_bS18 ? s18_sound_state ((u8 *) p, H.nSize - nFixed, false) : s16_sound_state ((u8 *) p, H.nSize - nFixed, false)) == 0)
	{
		Reset ();
		return 0;
	}
	// what belongs to now, not to the state: the Z80's hooks, pads and switches
	SoundHooks ();
	memcpy (M.Player, Player, sizeof Player);
	memcpy (M.GunX, Gun, 3);
	memcpy (M.GunY, Gun + 3, 3);
	M.nSystem = nSystem;
	M.nDsw = nDsw;
	UpdateMapping ();
	return 1;
}
size_t s16_hi_size (void)			{ return 0; }
int s16_hi_ready (void)				{ return 0; }
void s16_hi_get (u8 *)				{}
void s16_hi_put (const u8 *)			{}

// diagnostics for the harness
u32 s16_main_pc (void)				{ return m68k_get_reg (0, M68K_REG_PC); }
u32 s16_mcu_pc (void)				{ return s_Mcu.pc (); }
