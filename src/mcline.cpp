/*
 * s16-pi - the video lines drawn on core 1 (see mcline.h)
 *
 * One ring of jobs: core 0 writes only s_nHead, core 1 only s_nTail.
 */
#include "mcline.h"
#include "smp.h"
#include <circle/util.h>

extern "C" int snprintf (char *pBuffer, size_t nSize, const char *pFormat, ...);

struct TMcJob
{
	int		y;
	TS16LineRegs	Regs;
};

#define MC_RING 512				// power of 2, more than one frame of lines
static TMcJob s_Ring[MC_RING];
static unsigned s_nHead = 0;
static unsigned s_nTail = 0;

// core 0 only
static u32 *s_pFrame;
static unsigned s_nPitch;
static TMcLineDone *s_pDone;
static boolean s_bEnabled = FALSE;
static boolean s_bFailed = FALSE;

static boolean s_bAlive = FALSE;		// set once by core 1

// diagnostics: written by core 1, read by core 0 when core 1 is found stuck
static unsigned s_nBeat, s_nStage, s_nLine, s_nLines;
static unsigned s_nFailHead, s_nFailTail, s_nFailStage, s_nFailLine, s_nFailLines, s_nFailBeatMoved;
static u64 s_nFreq;

static void Draw (const TMcJob &J)
{
	s16_render_line (&J.Regs, J.y, s_pFrame + J.y * s_nPitch);
	if (s_pDone)
	{
		s_pDone ((unsigned) J.y);
	}
}

// core 1 stuck: draw what is left on core 0, then single core. s_nTail stays
// core 1's: if it is only slow, at worst a line is drawn twice.
static void Fail (void)
{
	unsigned nTail = SharedLoad (s_nTail);
	s_nFailHead = s_nHead;
	s_nFailTail = nTail;
	s_nFailStage = SharedLoad (s_nStage);
	s_nFailLine = SharedLoad (s_nLine);
	s_nFailLines = SharedLoad (s_nLines);
	s_nFreq = TicksFreq ();
	unsigned nBeat = SharedLoad (s_nBeat);
	SignalEvent ();
	u64 nUntil = Ticks () + TicksFreq () / 50;	// 20 ms: is core 1 still turning?
	while (Ticks () < nUntil)
	{
	}
	s_nFailBeatMoved = SharedLoad (s_nBeat) - nBeat;
	s_bEnabled = FALSE;			// Post() draws the next lines itself
	s_bFailed = TRUE;
	for (unsigned t = nTail; t != s_nHead; ++t)
	{
		Draw (s_Ring[t & (MC_RING - 1)]);
	}
}

void McSync (void)
{
	if (!s_bEnabled || SharedAcquire (s_nTail) == s_nHead)
	{
		return;
	}
	u64 nLimit = Ticks () + TicksFreq () / 2;
	while (SharedAcquire (s_nTail) != s_nHead)
	{
		if (Ticks () > nLimit)
		{
			Fail ();
			return;
		}
		WaitEvent ();
	}
}

static void SyncThunk (void *)
{
	McSync ();
}

// core 0: the core hands a line over
static void Post (void *, int y, const TS16LineRegs *pRegs)
{
	if (s_bEnabled && s_nHead - SharedAcquire (s_nTail) >= MC_RING)
	{
		McSync ();				// queue full (never in practice)
	}
	if (!s_bEnabled)
	{
		TMcJob J;				// single core: draw here
		J.y = y;
		J.Regs = *pRegs;
		Draw (J);
		return;
	}
	unsigned h = s_nHead;
	TMcJob &J = s_Ring[h & (MC_RING - 1)];
	J.y = y;
	J.Regs = *pRegs;
	SharedRelease (s_nHead, h + 1);
	SignalEvent ();
}

// single core: the core draws the line itself and hands it over finished
static void LineDoneHere (void *, int y, const uint32_t *pRGB)
{
	memcpy (s_pFrame + y * s_nPitch, pRGB, s_nPitch * 4);
	if (s_pDone)
	{
		s_pDone ((unsigned) y);
	}
}

void McSetup (u32 *pFrame, unsigned nPitch, TMcLineDone *pDone)
{
	s_pFrame = pFrame;
	s_nPitch = nPitch;
	s_pDone = pDone;
}

void McEnable (boolean bOn)
{
	McSync ();
	s_bEnabled = bOn && McAlive () && !s_bFailed;
	// the core's hooks (set again after every game load)
	s16_set_line_callback (LineDoneHere, 0);
	s16_set_line_sink (s_bEnabled ? Post : 0, 0);
	s16_set_video_sync (s_bEnabled ? SyncThunk : 0, 0);
	__asm__ volatile ("dsb ish" ::: "memory");
}

boolean McEnabled (void)	{ return s_bEnabled; }
boolean McFailed (void)		{ return s_bFailed; }
boolean McAlive (void)		{ return SharedAcquire (s_bAlive); }

void McWorker (void)
{
	SharedRelease (s_bAlive, (boolean) TRUE);
	SignalEvent ();
	for (;;)
	{
		unsigned t = s_nTail;
		while (SharedAcquire (s_nHead) == t)
		{
			SharedStore (s_nBeat, s_nBeat + 1);
			WaitEvent ();
		}
		SharedStore (s_nBeat, s_nBeat + 1);
		const TMcJob &J = s_Ring[t & (MC_RING - 1)];
		SharedStore (s_nStage, 1u);
		SharedStore (s_nLine, (unsigned) J.y);
		Draw (J);
		SharedStore (s_nLines, s_nLines + 1);
		SharedStore (s_nStage, 0u);
		SharedRelease (s_nTail, t + 1);
		SignalEvent ();				// wake core 0 if it waits
	}
}

void McFailText (char *pOut, size_t nSize, unsigned nFrame)
{
	snprintf (pOut, nSize, "  core 1 stuck at frame %u: queue %u/%u, %s line %u, "
		  "%u lines, beat %s, %llu Hz",
		  nFrame, s_nFailTail, s_nFailHead,
		  s_nFailStage ? "drawing" : "waiting after", s_nFailLine,
		  s_nFailLines, s_nFailBeatMoved ? "moving" : "stopped",
		  (unsigned long long) s_nFreq);
}
