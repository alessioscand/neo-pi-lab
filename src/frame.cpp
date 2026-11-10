/*
 * s16-pi - the emulated frame (see frame.h)
 */
#include "frame.h"
#include "mcline.h"
#include "racing.h"
#include "smp.h"
#include "tvcore.h"
#include "composite.h"
#include <circle/new.h>
#include "s16_api.h"

u32 g_VideoBuf[GAME_SRC_PITCH * GAME_SRC_LINES] __attribute__ ((aligned (64)));
int16_t g_AudioBuf[8192] __attribute__ ((aligned (64)));
unsigned g_nAudioFrames;

static CScreen *s_pVideo = 0;
static u32 s_Line[GAME_W * 8], s_Dark[GAME_W * 8];	// core 1's scanline buffers
static u32 s_TvLine[2][GAME_W * 8];		// core 2's, and the caller's without it

// read by core 1 for each line; changed only while no line is queued
static boolean s_bLinesToScreen = FALSE;
static boolean s_bComposite = FALSE;
static boolean s_bRaced = FALSE;

static unsigned s_nTvBurst = 0;			// colour burst phase of the frame (0-2)
static unsigned s_nCoreFrames = 0, s_nMcFailFrame = 0;

void FrameTakeAudio (void)
{
	const int nMax = (int) (sizeof g_AudioBuf / sizeof g_AudioBuf[0] / 2);
	g_nAudioFrames = (unsigned) s16_audio_read (g_AudioBuf, nMax);
}

// core 1 (or core 0 single core): a finished line
static void LineDone (unsigned nRow)
{
	if (!SharedLoad (s_bLinesToScreen))
	{
		return;
	}
	if (SharedLoad (s_bComposite))
	{
		TvPost (g_VideoBuf + nRow * GAME_SRC_PITCH,
			(s_nTvBurst + nRow) % COMP_PHASES, nRow, SharedLoad (s_bRaced));
	}
	else
	{
		s_pVideo->DrawGameLine (g_VideoBuf, nRow, s_Line, s_Dark);
		if (SharedLoad (s_bRaced))
		{
			RaceRowDone (nRow);
		}
	}
}

// core 2 (or the caller without it, nSide 1): a filtered line
static void TvLineOut (unsigned nRow, const u32 *pNtsc, boolean bRace, unsigned nSide)
{
	s_pVideo->DrawNtscLine (nRow, pNtsc, COMP_OUT_W, s_TvLine[nSide]);
	if (bRace)
	{
		RaceRowDone (nRow);
	}
}

// COMPOSITE: blargg's own composite look, on the game's lines
static_assert (COMP_OUT_W == PROJECT_TV_OUT_W, "project.h: PROJECT_TV_OUT_W");
static void *s_pTables = 0;

static void CompositeLine (const void *pSource, unsigned nPhase, u32 *pOut)
{
	composite_line (s_pTables, (const unsigned *) pSource, GAME_W, nPhase, pOut);
}

boolean FrameTvInit (void)
{
	if (s_pTables == 0)
	{
		void *p = new u8[composite_size ()];
		if (p == 0)
		{
			return FALSE;
		}
		composite_init (p, 0);
		s_pTables = p;
		TvSetFilter (CompositeLine);
	}
	return TRUE;
}

void FrameSetup (CScreen *pVideo)
{
	s_pVideo = pVideo;
	TvSetOutput (TvLineOut);
	McSetup (g_VideoBuf, GAME_SRC_PITCH, LineDone);
}

void FrameSync (void)
{
	McSync ();
	if (s_bComposite)
	{
		TvSync ();
		// the phase steps through all three values: the colour artifacts crawl
		// along the edges even on a still picture (the classic dot crawl)
		s_nTvBurst = (s_nTvBurst + 1) % COMP_PHASES;
	}
	s_nCoreFrames++;
	if (McFailed () && s_nMcFailFrame == 0)
	{
		s_nMcFailFrame = s_nCoreFrames;
	}
}

void FrameLinesToScreen (boolean bOn)
{
	SharedStore (s_bLinesToScreen, bOn);
}

void FrameSetComposite (boolean bOn)
{
	TvSync ();
	SharedStore (s_bComposite, bOn);
	__asm__ volatile ("dsb ish" ::: "memory");
}

boolean FrameIsComposite (void)
{
	return s_bComposite;
}

void FrameSetRaced (boolean bOn)
{
	SharedStore (s_bRaced, bOn);
}

void FrameDraw (void)
{
	if (s_bLinesToScreen)
	{
		return;				// each line went to the screen as it was drawn
	}
	if (s_bComposite)
	{
		for (unsigned y = 0; y < GAME_H; y++)
		{
			TvPost (g_VideoBuf + (y + GAME_SRC_Y) * GAME_SRC_PITCH + GAME_SRC_X,
				(s_nTvBurst + y) % COMP_PHASES, y, FALSE);
		}
		TvSync ();
		s_nTvBurst = (s_nTvBurst + 1) % COMP_PHASES;
		return;
	}
	s_pVideo->DrawGame (g_VideoBuf);
}

// a fixed colour burst phase, so the dots do not crawl on still text
void FrameDrawPicture (u32 *pBuffer)
{
	if (s_bComposite)
	{
		for (unsigned y = 0; y < GAME_H; y++)
		{
			TvPost (pBuffer + (y + GAME_SRC_Y) * GAME_SRC_PITCH + GAME_SRC_X, y % COMP_PHASES, y, FALSE);
		}
		TvSync ();
		return;
	}
	s_pVideo->DrawGame (pBuffer);
}

unsigned FrameMcFailFrame (void)
{
	return s_nMcFailFrame;
}
