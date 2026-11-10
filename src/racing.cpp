/*
 * s16-pi - beam racing state shared by the game loop (see racing.h)
 */
#include "racing.h"
#include "kernel.h"
#include "mcline.h"
#include "smp.h"

TRacing g_Race = {-1, FALSE, FALSE, 0, 0, 0};

// HVS registers at bus address 0x7E400000
#if RASPPI == 4
#define HVS_BASE	0xFE400000UL
#define HVS_GEN		5
#else
#define HVS_BASE	0x3F400000UL
#define HVS_GEN		4
#endif

static u32 HvsRead (unsigned nOffset)
{
	return *(volatile u32 *) (uintptr) (HVS_BASE + nOffset);
}

static u64 ReadTicks (void)
{
	return Ticks ();
}

void RaceCreate (void)
{
	if (g_Race.pRaster == 0)
	{
		g_Race.pRaster = new CRaster (HvsRead, ReadTicks, TicksFreq (), HVS_GEN);
		g_Race.pRacer = new CBeamRacer (g_Race.pRaster);
	}
}

// wait for the row's turn
void RaceLineHook (void *, int nLine)
{
	if (nLine < S16_FIRST_LINE || nLine >= S16_FIRST_LINE + GAME_H)
	{
		return;
	}
	unsigned nRow = (unsigned) (nLine - S16_FIRST_LINE);
	u64 nNow = Ticks ();
	g_Race.pRacer->RowArrived (nRow, nNow);
	u64 nRelease = g_Race.pRacer->RowRelease (nRow);
	if ((s64) (nNow - nRelease) < 0)
	{
		u64 nEnd;
		do
		{
			g_Race.pRaster->Sample ();
		}
		while ((s64) ((nEnd = Ticks ()) - nRelease) < 0);
		g_Race.nWaitTicks += nEnd - nNow;
	}
	else if ((nRow & 15) == 0)
	{
		g_Race.pRaster->Sample ();
	}
}

void RaceRowDone (unsigned nRow)
{
	__asm__ volatile ("dsb st" ::: "memory");	// the pixels have left the CPU
	g_Race.pRacer->RowDone (nRow, Ticks ());
}

// Pi 3 and 4 with multicore: find the HVS channel and measure it (about 0.7 s)
void CKernel::SetupBeamRacing (void)
{
	g_Race.bReady = FALSE;
	RaceCreate ();
	if (g_Race.nCfg == 0)
	{
		Log ("Beam racing: off (beamracing=off)", COLOR_TEXT);
		return;
	}
	if (!m_bMultiCore || !McAlive () || McFailed ())
	{
		Log ("Beam racing: needs multicore, off", COLOR_TEXT);
		return;
	}
	static char Report[600];
	boolean bOK = g_Race.pRaster->Probe (m_Video.GetWidth (), m_Video.GetHeight (), Report, sizeof Report);
	// the report can be long: log it in pieces
	for (const char *p = Report; *p; )
	{
		char Line[MaxLineLen];
		size_t n = strlen (p);
		if (n > MaxLineLen - 3) n = MaxLineLen - 3;
		Line[0] = Line[1] = ' ';
		memcpy (Line + 2, p, n);
		Line[n + 2] = '\0';
		Log (Line, COLOR_DIM);
		p += n;
	}
	if (bOK)
	{
		bOK = g_Race.pRaster->Calibrate (40, Report, sizeof Report);
		Log (Report, bOK ? COLOR_TEXT : COLOR_ERR);
	}
	g_Race.bReady = bOK;
	Log (bOK ? "Beam racing: ready" : "Beam racing: not available, double buffer + frame delay",
	     bOK ? COLOR_OK : COLOR_ERR);
}
