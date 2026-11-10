/*
 * s16-pi - HDMI output
 *
 * Console mode: framebuffer at the HDMI resolution (boot text, errors).
 * Game mode: integer scaling of the 320x224 System 16 picture, split between
 * the CPU (draw scale m) and the hardware scaler HVS (factor k), S = m * k:
 *   - the framebuffer is (display / k), so the HVS enlarges it exactly k times
 *     with the same factor in both directions (square pixels, no stretching);
 *   - black borders are drawn by us inside the framebuffer;
 *   - if the display is not divisible by any useful k, k = 1 (CPU only).
 * Filters (applied while drawing, no extra buffering, no added lag):
 *   scanlines in three strengths (dark rows at 75%, 50% or 25%).
 * Side columns (e.g. 720p: the picture fills the height): decided once in
 * SetGameMode; the band text then goes in the columns, drawn by the caller's
 * core (core 0) like DrawBandText: the line drawing (beam racing) does not
 * change.
 */
#ifndef VIDEO_H
#define VIDEO_H

#include "screen.h"
#include "smp.h"
#include <circle/bcmframebuffer.h>
#include <circle/chargenerator.h>

class CVideoOut final : public CScreen
{
public:
	CVideoOut (void);
	~CVideoOut (void);

	boolean Initialize (void) override;

	boolean SetGameMode (unsigned nFilter, boolean bAllowHVS) override;
	boolean SetConsoleMode (void) override;
	boolean IsGameMode (void) const override	{ return m_bGameMode; }

	unsigned GetWidth (void) const override		{ return m_nDispW; }
	unsigned GetHeight (void) const override	{ return m_nDispH; }
	unsigned GetScale (void) const override		{ return m_nScale; }	// S
	unsigned GetHVSFactor (void) const override	{ return m_nK; }	// k
	unsigned GetCPUScale (void) const override	{ return m_nM; }	// m
	unsigned GetFilter (void) const override	{ return m_nFilter; }
	unsigned GetFBWidth (void) const override	{ return m_nWidth; }
	unsigned GetFBHeight (void) const override	{ return m_nHeight; }
	unsigned GetGameY (void) const override		{ return m_nGameY; }

	void SetRacing (boolean bOn) override		{ SharedStore (m_bRacing, bOn); }

	unsigned GetRows (void) const override;
	void Clear (unsigned nBuffer, u32 nColor) override;
	void DrawText (unsigned nBuffer, unsigned nCol, unsigned nRow, const char *pText,
		       u32 nFG, u32 nBG) override;
	void ClearRows (unsigned nBuffer, unsigned nFirstRow, unsigned nRows, u32 nColor) override;

	void DrawGame (const u32 *pSource) override;
	void DrawGameLine (const u32 *pSource, unsigned y, u32 *pLine, u32 *pDark) override
	{
		DrawGameLineTo (SharedLoad (m_bRacing) ? m_nBack ^ 1 : m_nBack, pSource, y, pLine, pDark);
	}
	void DrawNtscLine (unsigned y, const u32 *pNtsc, unsigned nNtscW, u32 *pLine) override;
	void DrawBandText (boolean bTop, const TTextSpan *pSpans, unsigned nSpans) override;
	boolean HasSideColumns (void) const override	{ return m_bSide; }
	void DrawSideText (boolean bRight, const TTextSpan *pSpans, unsigned nSpans) override;

	unsigned GetPixelOrder (void) const override	{ return m_nPixelOrder; }
	unsigned GetWCPages (void) const override	{ return m_nWCPages; }
	boolean IsRedLow (void) const override		{ return m_bRLow; }

	void ShowBuffer (unsigned nBuffer) override;
	boolean Flip (void) override;
	boolean WaitForVSync (void) override;

private:
	unsigned GetColumns (void) const;
	boolean Allocate (unsigned nWidth, unsigned nHeight);
	void DrawGameLineTo (unsigned nBuffer, const u32 *pSource, unsigned y, u32 *pLine, u32 *pDark);
	u32 *Row (unsigned nBuffer, unsigned nY) const
	{
		return m_pBase + (nBuffer * m_nHeight + nY) * m_nPitchPx;
	}
	void DrawChar (unsigned nBuffer, unsigned nX, unsigned nY, char ch, u32 nFG, u32 nBG,
		       unsigned nTextScale, const CCharGenerator &Font);
	// one 8x8 menu-font character (capitals) with its shadow, scale S
	void DrawMenuChar (unsigned nBuf, unsigned x, unsigned y, unsigned ch, u32 nColor, unsigned S);

private:
	CBcmFrameBuffer *m_pFrameBuffer;
	CCharGenerator	 m_Font;		// console font (8x16)

	unsigned m_nDispW, m_nDispH;		// HDMI mode
	unsigned m_nWidth, m_nHeight;		// current framebuffer
	unsigned m_nPitchPx;
	u32	*m_pBase;
	unsigned m_nBack;
	boolean	 m_bGameMode;
	boolean	 m_bRacing;			// read by core 1

	unsigned m_nTextScale;			// console text scale (1 or 2)
	unsigned m_nScale;			// S
	unsigned m_nK;				// HVS factor
	unsigned m_nM;				// CPU draw scale
	unsigned m_nFilter;
	unsigned m_nGameX, m_nGameY;		// game rectangle in the framebuffer
	unsigned m_nStatusY;
	unsigned m_nStatusScale;
	unsigned m_nTopY;
	boolean	 m_bSide;			// status and messages in the side columns

	u32 Native (u32 c) const		// RGB() (R low) -> framebuffer order
	{
		return m_bRLow ? c : (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16);
	}

	u32	*m_pLine;			// one scaled game line (cached memory)
	u32	*m_pDark;			// the same line darkened (scanlines)
	unsigned m_nPixelOrder;			// reported by the firmware
	unsigned m_nWCPages;
	boolean	 m_bRLow;			// red in the low byte of a pixel
};

#endif
