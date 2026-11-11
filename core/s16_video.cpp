/*
 * s16-pi - System 16B video: two scrolling tile layers of 16 pages, a text
 * layer, zooming sprites, palette with shadow / highlight. One line at a time.
 *
 * Tile layers, sprites, priorities and palette as MAME draws them
 * (segaic16.cpp, sega16sp.cpp, segas16b_v.cpp: Aaron Giles), re-implemented
 * for line by line drawing.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "s16.h"
#include <stdlib.h>
#include <string.h>

// graphics decoded once at load
static u8 *s_pTilePix;			// tiles: 64 bytes each, 3 bits per pixel
static unsigned s_nTiles;
static const u16 *s_pSprites;		// sprite ROMs as words in the host's order
static u16 *s_pSprWords;
static unsigned s_nSprBanks;		// of 0x10000 words
static bool s_bAltBanks, s_bS18;
static unsigned s_nBankSize;

void s16_video_init (const u8 *pTiles, size_t nTilesSize, const u8 *pSprites, size_t nSpritesSize,
		     bool bAltBanks, unsigned nBankSize, bool bS18)
{
	s_bAltBanks = bAltBanks;
	s_nBankSize = nBankSize;
	s_bS18 = bS18;
	// tiles: three bit planes, one third of the region each; the last third
	// is the most significant bit
	const size_t nPlane = nTilesSize / 3;
	s_nTiles = (unsigned) (nPlane / 8);
	s_pTilePix = (u8 *) malloc ((size_t) s_nTiles * 64);
	for (unsigned t = 0; t < s_nTiles; t++)
	{
		for (unsigned r = 0; r < 8; r++)
		{
			const u8 b0 = pTiles[t * 8 + r], b1 = pTiles[nPlane + t * 8 + r], b2 = pTiles[2 * nPlane + t * 8 + r];
			for (unsigned x = 0; x < 8; x++)
			{
				const unsigned s = 7 - x;
				s_pTilePix[t * 64 + r * 8 + x] = (u8) (((b2 >> s) & 1) << 2 | ((b1 >> s) & 1) << 1 | ((b0 >> s) & 1));
			}
		}
	}

	// sprites: big endian words
	const size_t nWords = nSpritesSize / 2;
	s_pSprWords = (u16 *) malloc (nWords * 2);
	for (size_t i = 0; i < nWords; i++)
	{
		s_pSprWords[i] = (u16) (pSprites[i * 2] << 8 | pSprites[i * 2 + 1]);
	}
	s_pSprites = s_pSprWords;
	s_nSprBanks = (unsigned) (nSpritesSize / 0x20000);
}

unsigned s16_video_tiles (void)		{ return s_nTiles; }
unsigned s16_video_sprite_banks (void)	{ return s_nSprBanks; }

void s16_video_exit (void)
{
	free (s_pTilePix);
	free (s_pSprWords);
	s_pTilePix = 0;
	s_pSprWords = 0;
}

void s16_video_reset (TS16Video *pV)
{
	memset (pV, 0, sizeof *pV);
	for (u8 i = 0; i < 8; i++)
	{
		pV->TileBank[i] = i;
	}
	// which ROM each of the 16 bank numbers selects (the 171-5358 boards differ)
	static const u8 Default[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	static const u8 Alternate[16] = {0, 255, 255, 255, 255, 255, 255, 3, 255, 255, 255, 2, 255, 1, 0, 255};
	memcpy (pV->SprBank, s_bAltBanks ? Alternate : Default, sizeof pV->SprBank);
	pV->bDisplay = true;
	s18_vdp_reset (&pV->Vdp);
	for (unsigned i = 0; i < 0x800; i++)
	{
		s16_video_palette_w (pV, i, 0);
	}
}

// ---- palette ---------------------------------------------------------------

// Each gun has 5 bits through resistors of 3.9K, 2K, 1K, 500 and 250 ohm;
// bit 15 of a colour drives a sixth, 470 ohm, that pulls the three guns down
// (shadow) or up (highlight) when a sprite asks for it.
static u8 s_Normal[32], s_Shadow[32], s_Hilight[32];

static void InitWeights (void)
{
	static const double R[6] = {3900, 2000, 1000, 500, 250, 470};
	double fSum5 = 0, fSum6 = 0;
	for (int i = 0; i < 5; i++) fSum5 += 1 / R[i];
	fSum6 = fSum5 + 1 / R[5];
	for (int v = 0; v < 32; v++)
	{
		double f = 0;
		for (int i = 0; i < 5; i++)
		{
			if (v >> i & 1) f += 1 / R[i];
		}
		s_Normal[v] = (u8) (255 * f / fSum5 + 0.5);
		s_Shadow[v] = (u8) (255 * f / fSum6 + 0.5);
		s_Hilight[v] = (u8) (255 * (f + 1 / R[5]) / fSum6 + 0.5);
	}
}

void s16_video_palette_w (TS16Video *pV, unsigned nIndex, u16 nData)
{
	if (s_Normal[31] == 0)
	{
		InitWeights ();
	}
	pV->PalRam[nIndex] = nData;
	//  sBGR BBBB GGGG RRRR: bit 0 of each gun is in bits 12-14
	const unsigned r = ((nData >> 12) & 0x01) | ((nData << 1) & 0x1e);
	const unsigned g = ((nData >> 13) & 0x01) | ((nData >> 3) & 0x1e);
	const unsigned b = ((nData >> 14) & 0x01) | ((nData >> 7) & 0x1e);
	const u8 *pFx = (nData & 0x8000) ? s_Hilight : s_Shadow;
	pV->Colors[nIndex] = (u32) (s_Normal[r] << 16 | s_Normal[g] << 8 | s_Normal[b]);
	pV->Colors[nIndex + 0x800] = (u32) (pFx[r] << 16 | pFx[g] << 8 | pFx[b]);
}

// ---- tile layers -------------------------------------------------------------

void s16_video_latch (TS16Video *pV)
{
	for (int i = 0; i < 4; i++)
	{
		pV->Pages[i] = pV->TextRam[0xe80 / 2 + i];
		pV->YScroll[i] = pV->TextRam[0xe90 / 2 + i];
		pV->XScroll[i] = pV->TextRam[0xe98 / 2 + i];
	}
}

// One line of a scrolling layer (nWhich 0 = foreground, 1 = background). Each
// pixel gets the tile's colour and pen (0-1023) and, in bit 15, its priority;
// pen 0 is written only when bOpaque.
// The layer is a 2x2 arrangement of pages of 64x32 tiles; rows of 8 lines can
// have their own x scroll or switch to the alternate scroll and pages, columns
// of 16 pixels their own y scroll.
static void LayerLine (const TS16Video *pV, int nWhich, int y, u16 *pLine)
{
	const u16 *pText = pV->TextRam;
	const u16 nRow = pText[0xf80 / 2 + 0x20 * nWhich + y / 8];
	u16 nXScroll = pV->XScroll[nWhich], nYScroll = pV->YScroll[nWhich], nPages = pV->Pages[nWhich];
	const bool bColumns = (nYScroll & 0x8000) != 0;
	u16 nEffX = (nXScroll & 0x8000) ? nRow : nXScroll;
	bool bAlt = false;
	if (nRow & 0x8000)				// this row shows the alternate tilemap
	{
		nEffX = pV->XScroll[nWhich + 2];
		nYScroll = pV->YScroll[nWhich + 2];
		nPages = pV->Pages[nWhich + 2];
		bAlt = true;
	}
	const unsigned nX0 = (0xc0 - nEffX) & 0x3ff;

	unsigned nTileX = ~0u, nTileRow = 0;
	const u8 *pPix = 0;
	u16 nBase = 0;
	unsigned nVY = (unsigned) (y + nYScroll) & 0x1ff;
	for (int x = 0; x < S16_WIDTH; x++)
	{
		const unsigned nVX = (nX0 + (unsigned) x) & 0x3ff;
		if (bColumns && !bAlt && ((x + 8) & 15) == 0)
		{
			nVY = (unsigned) (y + pText[0xf16 / 2 + 0x20 * nWhich + (x + 8) / 16]) & 0x1ff;
			nTileX = ~0u;
		}
		else if (bColumns && !bAlt && x == 0)
		{
			nVY = (unsigned) (y + pText[0xf16 / 2 + 0x20 * nWhich]) & 0x1ff;
		}
		if ((nVX >> 3) != nTileX)
		{
			nTileX = nVX >> 3;
			const unsigned nQuad = (nVX >> 9) | ((nVY >> 8) << 1);
			const unsigned nPage = (nPages >> (nQuad * 4)) & 0x0f;
			const u16 nData = pV->TileRam[nPage * 0x800 + ((nVY & 0xff) >> 3) * 64 + (nTileX & 63)];
			unsigned nCode = nData & 0x1fff;
			nCode = (unsigned) pV->TileBank[nCode / s_nBankSize] * s_nBankSize + nCode % s_nBankSize;
			pPix = s_pTilePix + (size_t) (nCode % s_nTiles) * 64;
			nBase = (u16) ((nData & 0x8000) | ((nData >> 6) & 0x7f) << 3);
			nTileRow = (nVY & 7) * 8;
		}
		pLine[x] = (u16) (nBase | pPix[nTileRow + (nVX & 7)]);
	}
}

// the text layer: 64x28 tiles, the screen shows columns 24-63
static void TextLine (const TS16Video *pV, int y, u16 *pLine)
{
	const u16 *pRow = pV->TextRam + (y >> 3) * 64 + 24;
	const unsigned nTileRow = (unsigned) (y & 7) * 8;
	for (int tx = 0; tx < S16_WIDTH / 8; tx++)
	{
		const u16 nData = pRow[tx];
		const unsigned nCode = (unsigned) pV->TileBank[0] * s_nBankSize + (nData & 0x1ff);
		const u8 *pPix = s_pTilePix + (size_t) (nCode % s_nTiles) * 64 + nTileRow;
		const u16 nBase = (u16) ((nData & 0x8000) | ((nData >> 9) & 0x07) << 3);
		for (int k = 0; k < 8; k++)
		{
			pLine[tx * 8 + k] = (u16) (nBase | pPix[k]);
		}
	}
}

// ---- sprites ---------------------------------------------------------------

// The sprites of one line into pLine (0xffff = nothing), from sprite RAM as it
// is now, in list order (a later sprite covers an earlier one).
// Each sprite goes down from its top line to its bottom line, moving its
// address by its pitch at every line, and once more when the vertical zoom
// accumulator overflows: the row for this line is found by walking down from
// the top. Position 184 is the left edge of the screen; a row ends at a pixel
// 15, pixel 0 is transparent; the horizontal zoom skips a pixel whenever its
// accumulator overflows. (The hardware keeps its place in two scratch words of
// each sprite, +A and +E; they are not written here.)
// Returns false if no sprite touches the line.
static bool SpriteLine (const TS16Video *pV, int y, u16 *pLine)
{
	bool bAny = false;
	for (const u16 *pData = pV->SprRam; pData < pV->SprRam + 0x400; pData += 8)
	{
		if (pData[2] & 0x8000)			// end of the list
		{
			break;
		}
		const int nBottom = pData[0] >> 8, nTop = pData[0] & 0xff;
		if (y < nTop || y >= nBottom || (pData[2] & 0x4000))
		{
			continue;			// not on this line, or hidden
		}
		unsigned nBank = pV->SprBank[(pData[4] >> 8) & 0x0f];
		if (nBank == 255)
		{
			continue;
		}
		if (s_nSprBanks)
		{
			nBank %= s_nSprBanks;
		}

		// down to this line
		const int nPitch = (int8_t) (pData[2] & 0xff);
		const unsigned nVZoom = (pData[5] >> 5) & 0x1f;
		u16 nAddr = pData[3];
		unsigned nVAcc = 0;
		for (int r = nTop; r <= y; r++)
		{
			nAddr = (u16) (nAddr + nPitch);
			nVAcc += nVZoom;
			if (nVAcc & 0x20)
			{
				nAddr = (u16) (nAddr + nPitch);
				nVAcc &= 0x1f;
			}
		}

		if (!bAny)
		{
			memset (pLine, 0xff, S16_WIDTH * sizeof (u16));
			bAny = true;
		}
		const u16 *pRom = s_pSprites + 0x10000 * (size_t) nBank;
		const u16 nColPri = (u16) (((pData[4] & 0xff) << 4) | (((pData[1] >> 9) & 0x0f) << 12));
		const bool bFlip = (pData[2] & 0x100) != 0;
		const int nHZoom = pData[5] & 0x1f;
		int nAcc = 4 * nHZoom;
		int x = (int) (pData[1] & 0x1ff) - 184;
		const int nEnd = x + 511;
		nAddr = bFlip ? (u16) (nAddr + 1) : (u16) (nAddr - 1);
		while (x < nEnd)
		{
			nAddr = bFlip ? (u16) (nAddr - 1) : (u16) (nAddr + 1);
			const u16 nPixels = pRom[nAddr];
			int nPix = 0;
			for (int k = 0; k < 4; k++)
			{
				nPix = bFlip ? (nPixels >> (k * 4)) & 0x0f : (nPixels >> (12 - k * 4)) & 0x0f;
				nAcc = (nAcc & 0x3f) + nHZoom;
				if (nAcc < 0x40)
				{
					if (x >= 0 && x < S16_WIDTH && nPix != 0 && nPix != 15)
					{
						pLine[x] = (u16) (nColPri | nPix);
					}
					x++;
				}
			}
			if (nPix == 15)
			{
				break;
			}
		}
	}
	return bAny;
}

// ---- one line of the picture ---------------------------------------------------

// Priorities as MAME: the background sets 1 (2 for its priority tiles), the
// foreground 2 (4), the text 4 (8); a sprite of priority p shows where
// (1 << p) is above what the tiles set. A sprite of colour 63 does not show:
// it turns the pixel under it into its shadow / highlight colour.
// System 18: the VDP's picture goes in below one of the layers (bits 1-2 of
// the mixing value: 0 above the background's opaque pass, 1 above the
// background, 2 above the foreground, 3 above the text); with bit 0 it also
// sets that layer's own priority bit (as segas18_v.cpp).
void s16_video_line (const TS16Video *pV, int y, u32 *pOut)
{
	if (!pV->bDisplay)
	{
		memset (pOut, 0, S16_WIDTH * sizeof (u32));
		return;
	}
	u16 Layer[S16_WIDTH], Pix[S16_WIDTH], Spr[S16_WIDTH], Vdp[S16_WIDTH];
	u8 Pri[S16_WIDTH];

	int nVdpLayer = -1;
	u8 nVdpPri = 0;
	if (s_bS18 && pV->bVdpEnable)
	{
		nVdpLayer = (pV->nVdpMixing >> 1) & 3;
		nVdpPri = (pV->nVdpMixing & 1) ? (u8) (1 << nVdpLayer) : 0;
		s18_vdp_line (&pV->Vdp, y, Vdp);
	}
	auto DrawVdp = [&] (int nLayer)
	{
		if (nLayer != nVdpLayer) return;
		for (int x = 0; x < S16_WIDTH; x++)
		{
			const u16 r = Vdp[x];
			if ((r & 0x100) && (r & 0x0f))
			{
				Pix[x] = (u16) (0x1000 + (r & 0x3f));
				Pri[x] |= nVdpPri;
			}
		}
	};

	LayerLine (pV, 1, y, Layer);			// background: opaque, then its pixels' priorities
	for (int x = 0; x < S16_WIDTH; x++)
	{
		Pix[x] = Layer[x] & 0x3ff;
		Pri[x] = 0;
	}
	if (nVdpLayer == 0)
	{
		DrawVdp (0);
		for (int x = 0; x < S16_WIDTH; x++)
		{
			const u16 p = Layer[x];
			if (p & 7) Pix[x] = p & 0x3ff;
		}
	}
	for (int x = 0; x < S16_WIDTH; x++)
	{
		const u16 p = Layer[x];
		if (p & 7) Pri[x] |= (p & 0x8000) ? 2 : 1;
	}
	DrawVdp (1);
	LayerLine (pV, 0, y, Layer);			// foreground
	for (int x = 0; x < S16_WIDTH; x++)
	{
		const u16 p = Layer[x];
		if (p & 7)
		{
			Pix[x] = p & 0x3ff;
			Pri[x] |= (p & 0x8000) ? 4 : 2;
		}
	}
	DrawVdp (2);
	TextLine (pV, y, Layer);
	for (int x = 0; x < S16_WIDTH; x++)
	{
		const u16 p = Layer[x];
		if (p & 7)
		{
			Pix[x] = p & 0x3ff;
			Pri[x] |= (p & 0x8000) ? 8 : 4;
		}
	}
	DrawVdp (3);
	if (SpriteLine (pV, y, Spr))
	{
		for (int x = 0; x < S16_WIDTH; x++)
		{
			const u16 p = Spr[x];
			if (p != 0xffff && (1 << ((p >> 10) & 3)) > Pri[x])
			{
				if ((p & 0x03f0) == 0x03f0)
				{
					if (Pix[x] < 0x800) Pix[x] += 0x800;	// (not over the VDP's colours)
				}
				else
				{
					Pix[x] = 0x400 | (p & 0x3ff);
				}
			}
		}
	}
	for (int x = 0; x < S16_WIDTH; x++)
	{
		pOut[x] = pV->Colors[Pix[x]];
	}
}
