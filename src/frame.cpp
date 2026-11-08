/*
 * nes-pi - the emulated frame (see frame.h)
 */
#include "frame.h"
#include "racing.h"
#include "ui.h"
#include "tvcore.h"
#include "smp.h"
#include <circle/util.h>

TPicture g_Pic = {TRUE, NTSC_OFF, TRUE, NES_PALETTE_NESTOPIA, FALSE, {0}, 0};

u32 g_VideoBuf[NES_W * NES_H] __attribute__ ((aligned (64)));
u16 g_IdxBuf[NES_W * NES_H];
u32 g_NtscBuf[NTSC_OUT_W * NES_H] __attribute__ ((aligned (64)));

TVideoSource g_NesSource = {NES_W, NES_H - 2 * OVERSCAN_LINES, NES_W, 0, OVERSCAN_LINES, 0};
// the TV filter's 602-pixel lines are shown half as wide per pixel (bHalfX),
// which gives a 4:3 picture at the same height
TVideoSource g_NtscSource = {NTSC_OUT_W, NES_H - 2 * OVERSCAN_LINES, NTSC_OUT_W, 0, OVERSCAN_LINES, 1};
const TVideoSource g_UISource = {UI_W, UI_H, UI_SRC_PITCH, UI_SRC_X, UI_SRC_Y, 0};

const TVideoSource &FrameSource (void)	{ return g_Pic.nTvMode != NTSC_OFF ? g_NtscSource : g_NesSource; }
const u32 *FrameBuffer (void)		{ return g_Pic.nTvMode != NTSC_OFF ? g_NtscBuf : g_VideoBuf; }

int16_t g_AudioBuf[2 * (AUDIO_FRAME + 64)] __attribute__ ((aligned (64)));
static int16_t s_AudioMono[AUDIO_FRAME + 64];

volatile unsigned g_Pad[2] = {0, 0};
volatile boolean g_bRaceFrame = FALSE;

static unsigned s_nTvBurst = 0;		// colour burst phase of the frame (0-2)
static CScreen *s_pVideo = 0;
static u32 s_Line[PIC_MAX_W * 8], s_Dark[PIC_MAX_W * 8];	// core 0's scratch lines
static u32 s_TvLine[2][PIC_MAX_W * 8], s_TvDark[2][PIC_MAX_W * 8];	// the TV filter's two sides

static void NtscLine (const void *pSource, unsigned nPhase, u32 *pOut)
{
	ntsc_blargg_line ((const unsigned short *) pSource, nPhase, pOut);
}

// a filtered line (core 1, or the caller without it)
static void TvLineOut (unsigned nRow, const u32 *pFiltered, boolean bRace, unsigned nSide)
{
	s_pVideo->DrawGameLine (g_NtscBuf, nRow, s_TvLine[nSide], s_TvDark[nSide]);
	if (bRace)
	{
		RaceRowDone (nRow);
	}
}

// each line, as soon as the PPU finishes it; then the next line waits for its
// turn against the HDMI output (beam racing)
static void NesLineHook (int nLine, const uint16_t *pPixels)
{
	const int nTop = (int) g_NesSource.nY, nRows = (int) g_NesSource.nH;	// lines shown
	if (nLine >= 0 && nLine < NES_H)
	{
		const u32 *pPal = nes_palette ();
		u32 *pDst = &g_VideoBuf[nLine * NES_W];
		memcpy (&g_IdxBuf[nLine * NES_W], pPixels, NES_W * 2);
		unsigned x0 = 0, x1 = NES_W;
		if (g_Pic.bOverscan)
		{
			x0 = OVERSCAN_SIDE;
			x1 = NES_W - OVERSCAN_SIDE;
			for (unsigned x = 0; x < OVERSCAN_SIDE; x++)
			{
				pDst[x] = pDst[NES_W - 1 - x] = 0;
			}
		}
		for (unsigned x = x0; x < x1; x++)
		{
			pDst[x] = pPal[pPixels[x] & 511];
		}
		int nRow = nLine - nTop;
		if (nRow >= 0 && nRow < nRows)
		{
			if (g_Pic.nTvMode != NTSC_OFF)
			{
				// overscan: black sides go into the filter, so they blend in like on a TV
				static u16 Idx[NES_W];
				memcpy (Idx, pPixels, sizeof Idx);
				if (g_Pic.bOverscan)
				{
					for (unsigned x = 0; x < OVERSCAN_SIDE; x++)
					{
						Idx[x] = Idx[NES_W - 1 - x] = 0x0F;
					}
				}
				TvPost (Idx, (s_nTvBurst + (unsigned) nLine) % 3, (unsigned) nRow, g_bRaceFrame,
					&g_NtscBuf[nLine * NTSC_OUT_W]);
			}
			else
			{
				s_pVideo->DrawGameLine (g_VideoBuf, (unsigned) nRow, s_Line, s_Dark);
				if (g_bRaceFrame)
				{
					RaceRowDone ((unsigned) nRow);
				}
			}
		}
	}
	int nNext = nLine + 1 - nTop;
	if (g_bRaceFrame && nNext >= 0 && nNext < nRows)
	{
		RaceRowNext ((unsigned) nNext);
	}
}

void FrameSetup (CScreen *pVideo)
{
	s_pVideo = pVideo;
	nes_set_line_hook (NesLineHook);
	TvSetOutput (TvLineOut);
}

void FrameSetTvMode (int nMode)
{
	TvSync ();
	if (nMode != NTSC_OFF)
	{
		ntsc_blargg_setup (nMode);
		TvSetFilter (NtscLine);
	}
}

// a frame of lines through the TV filter, not drawn (start-up check, in LOG.TXT)
unsigned FrameTvTimeUs (unsigned nLines)
{
	static u32 Out[NTSC_OUT_W];
	static u16 Idx[NTSC_IN_W];
	for (unsigned x = 0; x < NTSC_IN_W; x++)
	{
		Idx[x] = (u16) ((x * 7) & 0x3F);	// many colours, like a busy scene
	}
	TvSetOutput (0);
	u64 t0 = Ticks ();
	for (unsigned i = 0; i < nLines; i++)
	{
		TvPost (Idx, i % 3, i, FALSE, Out);
	}
	TvSync ();
	u64 t1 = Ticks ();
	TvSetOutput (TvLineOut);
	return (unsigned) ((t1 - t0) * 1000000 / TicksFreq ());
}

// vertical blank (the game reads the pads) then the 240 lines, each shown as
// it is finished; audio to stereo
void FrameRun (void)
{
	nes_frame (g_Pad[0], g_Pad[1], s_AudioMono, AUDIO_FRAME);
	if (g_Pic.nTvMode != NTSC_OFF)
	{
		TvSync ();			// every line filtered and on the screen
		// the phase steps through all three values: the colour artifacts crawl
		// along the edges even on a still picture (the classic dot crawl;
		// stronger than a real NES, which alternates only two)
		s_nTvBurst = (s_nTvBurst + 1) % 3;
	}
	for (unsigned i = 0; i < AUDIO_FRAME; i++)
	{
		g_AudioBuf[2*i] = g_AudioBuf[2*i+1] = s_AudioMono[i];
	}
}
