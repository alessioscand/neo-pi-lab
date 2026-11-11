/*
 * s16-pi - System 18: the 315-5313 VDP (the Mega Drive's video chip) as the
 * board uses it: VRAM, CRAM, VSRAM, registers, 68000 DMA, fill and copy, the
 * two scrolling planes, the window and the sprites, one line at a time. Its
 * interrupts and its PSG are not connected on this board.
 *
 * The logic follows MAME's sega315_5313_device (315_5313.cpp, David
 * Haywood), re-implemented by hand for line by line drawing.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "s16.h"
#include <string.h>

#define REG(n)		(pV->Regs[n])
#define VRAM(a)		(pV->Vram[(a) & 0x7fff])

void s18_vdp_reset (TS18Vdp *pV)
{
	memset (pV, 0, sizeof *pV);
}

// ---- palette ---------------------------------------------------------------------

// 3 bits a gun, through MAME's non-linear levels (normal: level[c * 2])
static u32 CramColour (u16 nData)
{
	static const u8 Level[15] = {0, 29, 52, 70, 87, 101, 116, 130, 144, 158, 172, 187, 206, 228, 255};
	const unsigned r = (nData >> 1) & 7, g = (nData >> 5) & 7, b = (nData >> 9) & 7;
	return (u32) (Level[r * 2] << 16 | Level[g * 2] << 8 | Level[b * 2]);
}

static void CramWrite (TS18Vdp *pV, u32 *pColors, unsigned i, u16 nData)
{
	pV->Cram[i & 0x3f] = nData;
	pColors[i & 0x3f] = CramColour (nData);
}

// ---- ports -----------------------------------------------------------------------

static inline void AddrInc (TS18Vdp *pV)	{ pV->nAddr = (u16) (pV->nAddr + REG (0x0f)); }

static void VramWrite (TS18Vdp *pV, u16 nData)
{
	const bool bH40 = REG (0x0c) & 0x01;
	const u16 nSprBase = (u16) (bH40 ? (REG (0x05) & 0x7e) << 9 : (REG (0x05) & 0x7f) << 9);
	const unsigned nSprSize = bH40 ? 0x400 : 0x200;
	if (pV->nAddr & 1)
	{
		nData = (u16) (nData >> 8 | nData << 8);
	}
	VRAM (pV->nAddr >> 1) = nData;
	// the chip keeps its own copy of what is written to the sprite table
	if (pV->nAddr >= nSprBase && pV->nAddr < nSprBase + nSprSize)
	{
		pV->Sat[(pV->nAddr & (nSprSize - 1)) >> 1] = nData;
	}
	AddrInc (pV);
}

static void VramByte (TS18Vdp *pV, u16 nAddr, u8 nByte)
{
	u16 &w = VRAM (nAddr >> 1);
	w = (nAddr & 1) ? (u16) ((w & 0xff00) | nByte) : (u16) ((w & 0x00ff) | nByte << 8);
}

static void DataWrite (TS18Vdp *pV, u16 nData, u32 *pColors)
{
	pV->bPending = 0;
	if (pV->bFill)
	{
		// as MAME: the low byte first, then the high byte over the whole length
		if (pV->nAddr & 1)	VRAM (pV->nAddr >> 1) = (u16) ((VRAM (pV->nAddr >> 1) & 0xff00) | (nData & 0xff));
		else			VRAM (pV->nAddr >> 1) = (u16) ((VRAM (pV->nAddr >> 1) & 0x00ff) | (nData & 0xff) << 8);
		for (unsigned n = 0; n <= pV->nFillLength; n++)
		{
			if (pV->nAddr & 1)	VRAM (pV->nAddr >> 1) = (u16) ((VRAM (pV->nAddr >> 1) & 0x00ff) | (nData & 0xff00));
			else			VRAM (pV->nAddr >> 1) = (u16) ((VRAM (pV->nAddr >> 1) & 0xff00) | (nData >> 8));
			AddrInc (pV);
		}
		REG (0x13) = REG (0x14) = 0;
		return;
	}
	switch (pV->nCode & 0x0f)
	{
	case 0x1: VramWrite (pV, nData); break;
	case 0x3: CramWrite (pV, pColors, (pV->nAddr & 0x7e) >> 1, nData); AddrInc (pV); break;
	case 0x5: pV->Vsram[(pV->nAddr & 0x7e) >> 1] = nData; AddrInc (pV); break;
	default: break;
	}
}

static void SetRegister (TS18Vdp *pV, unsigned n, u8 nValue)
{
	if (!(REG (0x01) & 0x04) && n > 10)	// mode 4: the registers above 10 are not there
	{
		return;
	}
	REG (n) = nValue;
}

static void DmaSourceBack (TS18Vdp *pV, u32 nSource)
{
	REG (0x13) = REG (0x14) = 0;
	REG (0x15) = (u8) (nSource >> 1);
	REG (0x16) = (u8) (nSource >> 9);
	REG (0x17) = (u8) (nSource >> 17);
}

static u16 DmaRead (TS18VdpBus *pBus, u32 nSource)
{
	if (nSource <= 0x3fffff || nSource >= 0xe00000)
	{
		return pBus->pRead (nSource);
	}
	return 0;
}

static void Dma (TS18Vdp *pV, u32 *pColors, TS18VdpBus *pBus)
{
	if (!(REG (0x01) & 0x10))			// DMA not enabled
	{
		return;
	}
	const unsigned nType = REG (0x17) >> 6;
	const unsigned nCode = pV->nCode & 0x0f;
	if (nType <= 1)					// from the 68000's memory
	{
		u32 nSource = (u32) (REG (0x15) | REG (0x16) << 8 | (REG (0x17) & 0x7f) << 16) << 1;
		u32 nLength = (u32) (REG (0x13) | REG (0x14) << 8) << 1;
		nLength &= 0xffff;
		if (nLength == 0) nLength = 0xffff;
		if (nCode == 0x1)
		{
			pBus->nStall += nLength / 350 + 1;	// (MAME's hack for its time)
			for (u32 i = 0; i < nLength >> 1; i++)
			{
				VramWrite (pV, DmaRead (pBus, nSource));
				nSource += 2;
				if (nSource > 0xffffff) nSource = 0xe00000;
			}
		}
		else if (nCode == 0x3 || nCode == 0x5)
		{
			for (u32 i = 0; i < nLength >> 1; i++)
			{
				if (nCode == 0x5 && pV->nAddr >= 0x80) return;	// (and the registers stay)
				const u16 n = DmaRead (pBus, nSource);
				if (nCode == 0x3)	CramWrite (pV, pColors, (pV->nAddr & 0x7e) >> 1, n);
				else			pV->Vsram[(pV->nAddr & 0x7e) >> 1] = n;
				nSource += 2;
				if (nSource > 0xffffff) nSource = 0xfe0000;
				AddrInc (pV);
			}
		}
		else
		{
			return;
		}
		DmaSourceBack (pV, nSource);
	}
	else if (nType == 2)				// fill: at the next data write
	{
		if (nCode == 0x1 || nCode == 0x3 || nCode == 0x5)
		{
			pV->bFill = 1;
			pV->nFillLength = (u16) (REG (0x13) | REG (0x14) << 8);
		}
	}
	else if ((pV->nCode & 0x10) || nCode == 0x1)	// copy, inside VRAM
	{
		u32 nSource = (u32) (REG (0x15) | REG (0x16) << 8);
		const u16 nLength = (u16) (REG (0x13) | REG (0x14) << 8);
		for (unsigned i = 0; i < nLength; i++)
		{
			const u16 w = VRAM ((nSource & 0xffff) >> 1);
			VramByte (pV, pV->nAddr, (nSource & 1) ? (u8) w : (u8) (w >> 8));
			nSource++;
			AddrInc (pV);
		}
	}
}

static void ControlWrite (TS18Vdp *pV, u16 nData, u32 *pColors, TS18VdpBus *pBus)
{
	pV->bFill = 0;
	if (pV->bPending)
	{
		pV->bPending = 0;
		pV->nPart2 = nData;
	}
	else if ((nData & 0xc000) == 0x8000)
	{
		SetRegister (pV, (nData >> 8) & 0x1f, (u8) nData);
		pV->nCode = 0;
		pV->nAddr = 0;
		return;
	}
	else
	{
		pV->bPending = 1;
		pV->nPart1 = nData;
	}
	pV->nCode = (u16) ((pV->nPart1 & 0xc000) >> 14 | (pV->nPart2 & 0x00f0) >> 2);
	pV->nAddr = (u16) ((pV->nPart1 & 0x3fff) | (pV->nPart2 & 0x0003) << 14);
	if (!pV->bPending && (pV->nCode & 0x20))
	{
		Dma (pV, pColors, pBus);
	}
}

void s18_vdp_write (TS18Vdp *pV, unsigned nOffset, u16 nData, u16 nMask, u32 *pColors, TS18VdpBus *pBus)
{
	switch (nOffset & 0x0e)
	{
	case 0x0: case 0x2:
		if (!(nMask & 0xff00))		nData = (u16) ((nData & 0x00ff) | nData << 8);
		else if (!(nMask & 0x00ff))	nData = (u16) ((nData & 0xff00) | nData >> 8);
		DataWrite (pV, nData, pColors);
		break;
	case 0x4: case 0x6:
		ControlWrite (pV, nData, pColors, pBus);
		break;
	default:			// the H/V counter: nothing to write
		break;
	}
}

// V counter of a 224-line NTSC frame: 00-EA, then E5-FF
static u8 VCounter (int nLine)
{
	return (u8) (nLine <= 0xea ? nLine : nLine - 6);
}

u16 s18_vdp_read (TS18Vdp *pV, unsigned nOffset, int nLine, int nHpos)
{
	switch (nOffset & 0x0e)
	{
	case 0x0: case 0x2: {
		pV->bPending = 0;
		u16 n = 0;
		switch (pV->nCode & 0x0f)
		{
		case 0x0: n = VRAM ((pV->nAddr & 0xfffe) >> 1); AddrInc (pV); break;
		case 0x4: n = pV->Vsram[(pV->nAddr & 0x7e) >> 1]; AddrInc (pV); break;
		case 0x8: n = pV->Cram[(pV->nAddr & 0x7e) >> 1]; AddrInc (pV); break;
		default: break;
		}
		return n; }
	case 0x4: case 0x6: {
		// (MAME's horizontal blank, after position 400, never comes in a 342-pixel line)
		const bool bVblank = pV->bVblank || !(REG (0x01) & 0x40);
		return (u16) (0x3400 | 0x0200 | pV->bIrq6 << 7 | (bVblank ? 0x08 : 0));
		}
	default: {
		int nH = nHpos;
		if (nH > 0xf7) nH -= 0x49;
		return (u16) (VCounter (nLine % 262) << 8 | (nH & 0xff));
		}
	}
}

void s18_vdp_vblank (TS18Vdp *pV)	{ pV->bVblank = 1; pV->bIrq6 = 1; }
void s18_vdp_frame (TS18Vdp *pV)	{ pV->bVblank = 0; }

// ---- one line of the picture -------------------------------------------------------
// Each pixel: the colour (0-63) and, in bit 8, whether something was drawn
// there (bit clear: the backdrop). As MAME's render_videoline_to_videobuffer
// and the raw line it hands to the System 18 mixer.

struct TLine
{
	u32 Video[320];		// colour | 0x10000 sprite | 0x20000 backdrop, | 0x2000 / 0x4000 / 0x8000 shadow, normal, highlight
	u8 High[320];		// high priority tiles: colour | 0x80
	u8 Sprite[1024];	// x + 128
};

// the 8 pixels of a row of a tile: pixel 0 in the top nibble of the first word
static inline void TileRow (const TS18Vdp *pV, u16 nCode, unsigned nRow, u8 *pOut)
{
	const u16 a = (u16) ((nCode & 0x7ff) * 16 + nRow * 2);
	const u16 w0 = VRAM (a), w1 = VRAM (a + 1);
	pOut[0] = (u8) (w0 >> 12);	  pOut[1] = (u8) ((w0 >> 8) & 15);
	pOut[2] = (u8) ((w0 >> 4) & 15); pOut[3] = (u8) (w0 & 15);
	pOut[4] = (u8) (w1 >> 12);	  pOut[5] = (u8) ((w1 >> 8) & 15);
	pOut[6] = (u8) ((w1 >> 4) & 15); pOut[7] = (u8) (w1 & 15);
}

// pixels nStart to nEnd - 1 of the tile at nTileBase (its row: vcolumn); a
// block of 16 pixels always ends inside the line
static inline void DrawTile (const TS18Vdp *pV, TLine *pL, unsigned nTileBase, int nVColumn, int nStart, int nEnd,
			     int &nPos, bool bFg)
{
	const u16 d = VRAM (nTileBase);
	const unsigned nRow = (d & 0x1000) ? (unsigned) (7 - (nVColumn & 7)) : (unsigned) (nVColumn & 7);
	const u16 a = (u16) ((d & 0x7ff) * 16 + nRow * 2);
	u32 nBits = (u32) VRAM (a) << 16 | VRAM (a + 1);	// pixel 0 in the top nibble
	const int nCount = nEnd - nStart;
	if (d & 0x0800)					// mirrored: reverse the nibbles
	{
		nBits = (nBits >> 16) | (nBits << 16);
		nBits = ((nBits >> 8) & 0x00ff00ff) | ((nBits << 8) & 0xff00ff00);
		nBits = ((nBits >> 4) & 0x0f0f0f0f) | ((nBits << 4) & 0xf0f0f0f0);
	}
	nBits <<= 4 * nStart;
	const u8 nColour = (u8) (((d >> 13) & 3) << 4);
	int x = nPos;
	nPos += nCount;
	if (!(d & 0x8000))				// low priority
	{
		if (!nBits) return;
		for (int k = 0; k < nCount; k++, x++, nBits <<= 4)
		{
			const u8 n = (u8) (nBits >> 28);
			if (n) pL->Video[x] = n | nColour;
		}
	}
	else if (bFg)
	{
		for (int k = 0; k < nCount; k++, x++, nBits <<= 4)
		{
			const u8 n = (u8) (nBits >> 28);
			pL->High[x] = n ? (u8) (n | nColour | 0x80) : (u8) (pL->High[x] | 0x80);
		}
	}
	else
	{
		for (int k = 0; k < nCount; k++, x++, nBits <<= 4)
		{
			pL->High[x] = (u8) ((nBits >> 28) | nColour | 0x80);
		}
	}
}

static void SpriteLine (const TS18Vdp *pV, int y, TLine *pL)
{
	const bool bH40 = REG (0x0c) & 0x01;		// (RS1: 40 cells)
	int nMaxSprites = bH40 ? 80 : 64, nMaxPixels = bH40 ? 320 : 256;
	const u16 nBase = (u16) (bH40 ? (REG (0x05) & 0x7e) << 9 : (REG (0x05) & 0x7f) << 9);
	memset (pL->Sprite, 0, sizeof pL->Sprite);
	int nMask = 0, nSprite = 0, nLink;
	do
	{
		const u16 *pSat = pV->Sat + nSprite * 4;
		const int nY = (pSat[0] & 0x1ff) - 128;
		const int nHeight = ((pSat[1] >> 8) & 3) + 1, nWidth = ((pSat[1] >> 10) & 3) + 1;
		nLink = pSat[1] & 0x7f;
		const unsigned nEntry = (unsigned) (nBase >> 1) + (unsigned) nSprite * 4;
		const int nX = VRAM (nEntry + 3) & 0x1ff;
		if (nY <= y && nY + nHeight * 8 > y)
		{
			const u16 w2 = VRAM (nEntry + 2);
			const u16 nAddr = w2 & 0x7ff;
			const bool bXFlip = w2 & 0x800, bYFlip = w2 & 0x1000;
			const u8 nColour = (u8) (((w2 >> 13) & 3) << 4), nPri = (w2 & 0x8000) ? 0x80 : 0x40;
			if (nX == 0) nMask |= 1;
			if (nX > 0 && nX < 0x40) nMask |= 2;
			if (nMask == 3)
			{
				return;
			}
			const int nLine = y - nY, nYTile = nLine >> 3;
			if (nYTile < nHeight)
			{
				for (int xt = 0; xt < nWidth; xt++)
				{
					const int nXTileCode = (bXFlip ? nWidth - xt - 1 : xt) * nHeight;
					const int nYTileCode = bYFlip ? nHeight - nYTile - 1 : nYTile;
					const unsigned nRow = (unsigned) (bYFlip ? 7 - (nLine & 7) : (nLine & 7));
					u8 Pix[8];
					TileRow (pV, (u16) ((nAddr + nXTileCode + nYTileCode) & 0x7ff), nRow, Pix);
					int x = (nX + xt * 8) & 0x1ff;
					for (int k = 0; k < 8; k++)
					{
						const u8 n = Pix[bXFlip ? 7 - k : k];
						if (n && !pL->Sprite[x])
						{
							pL->Sprite[x] = n | nColour | nPri;
						}
						x = (x + 1) & 0x1ff;
						if (--nMaxPixels == 0)
						{
							return;
						}
					}
				}
			}
		}
		nSprite = nLink;
		nMaxSprites--;
	}
	while (nMaxSprites >= 0 && nLink != 0);
}

void s18_vdp_line (const TS18Vdp *pV, int y, u16 *pRaw)
{
	static TLine L;			// (one line at a time, on one core)
	const u8 nBg = REG (0x07) & 0x3f;
	for (int x = 0; x < 320; x++)
	{
		L.Video[x] = nBg | 0x20000;
	}
	memset (L.High, 0, sizeof L.High);

	if ((REG (0x01) & 0x40) && !(REG (0x00) & 0x01))
	{
		SpriteLine (pV, y, &L);

		const u16 nBaseA = (u16) (((REG (0x02) & 0x38) >> 3) << 13);
		const u16 nBaseB = (u16) ((REG (0x04) & 0x07) << 13);
		const unsigned nSize = (REG (0x10) & 3) | ((REG (0x10) >> 4) & 3) << 4;
		const bool bWinRight = REG (0x11) & 0x80, bWinDown = REG (0x12) & 0x80;
		const int nWinHPos = REG (0x11) & 0x1f, nWinVPos = REG (0x12) & 0x1f;
		const unsigned nHRes = (REG (0x0c) >> 7 & 1) | (REG (0x0c) & 1) << 1;
		const int nColumns = nHRes >= 2 ? 40 : 32;
		const int nWinHSize = nHRes >= 2 ? 64 : 32, nWinVSize = 32;
		const u16 nBaseW = (u16) ((REG (0x03) & (nHRes >= 2 ? 0x3c : 0x3e)) << 10);
		const int nScreenH = (REG (0x01) & 0x08) ? 240 : 224;

		int nWinFirstCol, nWinLastCol, nNonFirstCol, nNonLastCol, nWinFirstLine, nWinLastLine;
		bool bBugged = false;
		if (bWinRight)
		{
			nWinFirstCol = nWinHPos * 16;
			nWinLastCol = nColumns * 8;
			if (nWinFirstCol > nWinLastCol) nWinFirstCol = nWinLastCol;
			nNonFirstCol = 0;
			nNonLastCol = nWinFirstCol;
		}
		else
		{
			nWinFirstCol = 0;
			nWinLastCol = nWinHPos * 16;
			if (nWinLastCol > nColumns * 8) nWinLastCol = nColumns * 8;
			nNonFirstCol = nWinLastCol;
			nNonLastCol = nColumns * 8;
			if (nWinLastCol != 0) bBugged = true;
		}
		if (bWinDown)
		{
			nWinFirstLine = nWinVPos * 8;
			nWinLastLine = nScreenH;
			if (nWinFirstLine > nScreenH) nWinFirstLine = nScreenH;
		}
		else
		{
			nWinFirstLine = 0;
			nWinLastLine = nWinVPos * 8;
			if (nWinLastLine > nScreenH) nWinLastLine = nScreenH;
		}
		if (y >= nWinFirstLine && y < nWinLastLine)
		{
			nWinFirstCol = 0;
			nWinLastCol = nColumns * 8;
			nNonFirstCol = nNonLastCol = 0;
		}

		int nHSize = 64, nVSize = 64;
		switch (nSize)
		{
		case 0x00: nHSize = 32;  nVSize = 32; break;
		case 0x01: nHSize = 64;  nVSize = 32; break;
		case 0x02: case 0x12: case 0x22: case 0x32: nHSize = 64; nVSize = 1; break;
		case 0x03: case 0x13: nHSize = 128; nVSize = 32; break;
		case 0x10: case 0x20: nHSize = 32; nVSize = 64; break;
		case 0x11: case 0x21: case 0x31: nHSize = 64; nVSize = 64; break;
		case 0x23: nHSize = 128; nVSize = 64; break;
		case 0x30: nHSize = 32;  nVSize = 128; break;
		case 0x33: nHSize = 128; nVSize = 128; break;
		}

		const u16 nHScrollBase = (u16) ((REG (0x0d) & 0x3f) << 10);
		unsigned nHsIndex = 0;
		switch (REG (0x0b) & 3)
		{
		case 1: nHsIndex = (unsigned) (y & 7) * 2; break;
		case 2: nHsIndex = (unsigned) (y & ~7) * 2; break;
		case 3: nHsIndex = (unsigned) y * 2; break;
		}
		const int nHScrollA = VRAM ((nHScrollBase >> 1) + 0 + nHsIndex);
		const int nHScrollB = VRAM ((nHScrollBase >> 1) + 1 + nHsIndex);
		const bool bColumnV = REG (0x0b) & 0x04;

		auto VScroll = [&] (int nColumn, int nPlane) -> int
		{
			return bColumnV ? pV->Vsram[(nColumn * 2 + nPlane) & 0x3f] : pV->Vsram[nPlane];
		};
		auto TileBase = [&] (u16 nBase, int nVScroll, int nHColumn, int &nVColumn) -> unsigned
		{
			nVColumn = (nVScroll + y) & (nVSize * 8 - 1);
			return ((unsigned) (nBase >> 1) + (unsigned) ((nVColumn >> 3) * nHSize) + (unsigned) nHColumn) & 0x7fff;
		};

		// plane B, in blocks of 16 pixels: a part of a tile, a whole one, the rest
		for (int c = 0; c < nColumns / 2; c++)
		{
			int nPos = c * 16, nVCol;
			const int nPart = 8 - (nHScrollB & 7);
			int vs = (nHScrollB & 0xf) ? VScroll (c - 1, 1) : VScroll (c, 1);
			{ const unsigned nTb = TileBase (nBaseB, vs, ((c * 2 - 1) - (nHScrollB >> 3)) & (nHSize - 1), nVCol); DrawTile (pV, &L, nTb, nVCol, nPart, 8, nPos, false); }
			vs = (nHScrollB & 0xf) ? VScroll (c - 1, 1) : VScroll (c, 1);
			{ const unsigned nTb = TileBase (nBaseB, vs, ((c * 2) - (nHScrollB >> 3)) & (nHSize - 1), nVCol); DrawTile (pV, &L, nTb, nVCol, 0, 8, nPos, false); }
			vs = VScroll (c, 1);
			{ const unsigned nTb = TileBase (nBaseB, vs, ((c * 2 + 1) - (nHScrollB >> 3)) & (nHSize - 1), nVCol); DrawTile (pV, &L, nTb, nVCol, 0, nPart, nPos, false); }
		}

		// the window
		for (int c = nWinFirstCol / 16; c < nWinLastCol / 16; c++)
		{
			int nPos = c * 16;
			const int nVCol = y & (nWinVSize * 8 - 1);
			for (int k = 0; k < 2; k++)
			{
				const int nHCol = (c * 2 + k) & (nWinHSize - 1);
				const unsigned nTb = ((unsigned) (nBaseW >> 1) + (unsigned) ((nVCol >> 3) * nWinHSize) + (unsigned) nHCol) & 0x7fff;
				DrawTile (pV, &L, nTb, nVCol, 0, 8, nPos, true);
			}
		}

		// plane A where the window is not
		for (int c = nNonFirstCol / 16; c < nNonLastCol / 16; c++)
		{
			int nPos = c * 16, nVCol, nHCol;
			const int nPart = 8 - (nHScrollA & 7);
			const bool bPlain = !bBugged || (nHScrollA & 0xf) == 0 || c > nNonFirstCol / 16;
			int vs = (nHScrollA & 0xf) ? VScroll (c - 1, 0) : VScroll (c, 0);
			nHCol = bPlain ? ((c * 2 - 1) - (nHScrollA >> 3)) : ((c * 2 + 1) - (nHScrollA >> 3));
			{ const unsigned nTb = TileBase (nBaseA, vs, nHCol & (nHSize - 1), nVCol); DrawTile (pV, &L, nTb, nVCol, nPart, 8, nPos, true); }
			vs = (nHScrollA & 0xf) ? VScroll (c - 1, 0) : VScroll (c, 0);
			if (bPlain || (nHScrollA & 0xf) < 8)	nHCol = (c * 2) - (nHScrollA >> 3);
			else					nHCol = (c * 2 + 2) - (nHScrollA >> 3);
			{ const unsigned nTb = TileBase (nBaseA, vs, nHCol & (nHSize - 1), nVCol); DrawTile (pV, &L, nTb, nVCol, 0, 8, nPos, true); }
			vs = VScroll (c, 0);
			nHCol = (c * 2 + 1) - (nHScrollA >> 3);
			{ const unsigned nTb = TileBase (nBaseA, vs, nHCol & (nHSize - 1), nVCol); DrawTile (pV, &L, nTb, nVCol, 0, nPart, nPos, true); }
		}

		const bool bSH = REG (0x0c) & 0x08;
		for (int x = 0; x < 320; x++)		// low priority sprites
		{
			const u8 s = L.Sprite[x + 128];
			if (!(s & 0x40)) continue;
			const u8 n = s & 0x3f;
			if (!bSH)				L.Video[x] = n | 0x10000;
			else if (n == 0x0e || n == 0x1e || n == 0x2e) L.Video[x] = n | 0x4000 | 0x10000;
			else if (n == 0x3e)			L.Video[x] |= 0x8000;
			else if (n == 0x3f)			L.Video[x] |= 0x2000;
			else					L.Video[x] = n | 0x10000;
		}
		for (int x = 0; x < 320; x++)		// high priority planes
		{
			const u8 h = L.High[x];
			if (!(h & 0x80)) continue;
			if (h & 0x0f)		L.Video[x] = (h & 0x3f) | (bSH ? 0x4000 : 0);
			else if (bSH)		L.Video[x] |= 0x4000;
		}
		for (int x = 0; x < 320; x++)		// high priority sprites
		{
			const u8 s = L.Sprite[x + 128];
			if (!(s & 0x80)) continue;
			const u8 n = s & 0x3f;
			if (!bSH)		L.Video[x] = n | 0x10000;
			else if (n == 0x3e)	L.Video[x] |= 0x8000;
			else if (n == 0x3f)	L.Video[x] |= 0x2000;
			else			L.Video[x] = n | 0x4000 | 0x10000;
		}
	}

	for (int x = 0; x < 320; x++)
	{
		const u32 d = L.Video[x];
		pRaw[x] = (d & 0x20000) ? 0 : (u16) (0x100 | (d & 0x3f));
	}
}
