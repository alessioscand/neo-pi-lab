/*
 * s16-pi - BENCH and MCTEST: started by an empty file of that name in the
 * s16pi folder, on the last game played
 */
#include "kernel.h"
#include "frame.h"
#include "mcline.h"
#include "profile.h"
#include "stats.h"
#include "s16_api.h"
#include <circle/machineinfo.h>

static_assert (BENCH_FRAMES <= STATS_MAX_VALUES, "ComputeStats sorts at most STATS_MAX_VALUES");

#define BENCH_PROFILE_US 200		// profiler: one sample every 200 us
#define BENCH_SLOW_US	16000		// frames from 16 ms on count as slow

// Multicore picture check (s16pi/MCTEST): 60 s of the game's demo, always from
// the same starting state (a save state taken after loading), run 4 times:
// 1 single core (only to start from the same leftovers), 2 single core
// (reference), 3 and 4 with core 1 drawing the lines. Every line of every
// frame of passes 3 and 4 must be identical to pass 2. Results in MCTEST.TXT.
void CKernel::McTest (unsigned nSel)
{
	u32 *pRef = new u32[MCTEST_FRAMES * GAME_H];
	if (pRef == 0)
	{
		Log ("MC test: not enough memory", COLOR_ERR);
		return;
	}
	char Report[2000] = "";
	char Line[160];
	boolean bMulti = m_bMultiCore && McAlive () && !McFailed ();

	auto LineHash = [] (unsigned y) -> u32
	{
		u32 h = 2166136261U;
		const u32 *p = &g_VideoBuf[(y + GAME_SRC_Y) * GAME_SRC_PITCH + GAME_SRC_X];
		for (unsigned x = 0; x < GAME_W; x++)
		{
			h = (h ^ p[x]) * 16777619U;
		}
		return h;
	};

	if (!LoadGame (nSel))
	{
		Log ("MC test: cannot load the game", COLOR_ERR);
		delete [] pRef;
		return;
	}
	McEnable (FALSE);
	size_t nStateSize = s16_state_size ();
	u8 *pState = new u8[nStateSize];
	int nState = s16_state_save (pState, nStateSize);
	ApplyVideoMode ();
	FrameLinesToScreen (FALSE);

	for (unsigned nPass = 0; nPass < 4; nPass++)
	{
		McEnable (nPass >= 2 && bMulti);
		s16_state_load (pState, (size_t) nState);
		snprintf (Line, sizeof Line, "MC test pass %u: %s", nPass + 1,
			  McEnabled () ? "multicore" : "single core");
		Log (Line, COLOR_TEXT);
		SaveLog ();

		unsigned nBadFrames = 0, nFirstBad = 0, nBadLines = 0;
		unsigned nFirstLines = 0, nFirstLo = 0, nFirstHi = 0;
		unsigned t0 = CTimer::GetClockTicks ();
		for (unsigned f = 0; f < MCTEST_FRAMES; f++)
		{
			s16_run_frame ();
			FrameSync ();
			FrameTakeAudio ();
			unsigned nBadHere = 0, nLo = 0, nHi = 0;
			for (unsigned y = 0; y < GAME_H; y++)
			{
				u32 h = LineHash (y);
				if (nPass == 0)
				{
				}
				else if (nPass == 1)
				{
					pRef[f * GAME_H + y] = h;
				}
				else if (h != pRef[f * GAME_H + y])
				{
					if (nBadHere == 0) nLo = y;
					nHi = y;
					nBadHere++;
				}
			}
			if (nBadHere)
			{
				if (nBadFrames == 0)
				{
					nFirstBad = f;
					nFirstLines = nBadHere;
					nFirstLo = nLo;
					nFirstHi = nHi;
				}
				nBadFrames++;
				nBadLines += nBadHere;
			}
			if (f % 60 == 0)
			{
				m_Video.DrawGame (g_VideoBuf);
				char Text[64];
				snprintf (Text, sizeof Text, "MC TEST %u/4  %u/%u S", nPass + 1, f / 60,
					  MCTEST_FRAMES / 60);
				const CScreen::TTextSpan Span = {Text, nBadFrames ? UI_ERR : UI_WHITE};
				m_Video.DrawBandText (TRUE, &Span, 1);
				m_Video.Flip ();
			}
		}
		unsigned nMs = (CTimer::GetClockTicks () - t0) / 1000;

		if (nPass < 2)
		{
			snprintf (Line, sizeof Line, "pass %u, single core: %u frames in %u ms%s\r\n",
				  nPass + 1, MCTEST_FRAMES, nMs, nPass == 1 ? " (reference)" : "");
		}
		else if (nBadFrames == 0)
		{
			snprintf (Line, sizeof Line, "pass %u, %s: %u frames in %u ms, all lines identical%s\r\n",
				  nPass + 1, bMulti ? "multicore" : "single core (no core 1)", MCTEST_FRAMES, nMs,
				  McFailed () ? " (core 1 FAILED)" : "");
		}
		else
		{
			snprintf (Line, sizeof Line, "pass %u, multicore: %u frames in %u ms, DIFFERENT: %u frames, "
				  "%u lines; first frame %u, %u lines (%u-%u)%s\r\n",
				  nPass + 1, MCTEST_FRAMES, nMs, nBadFrames, nBadLines, nFirstBad,
				  nFirstLines, nFirstLo, nFirstHi, McFailed () ? " (core 1 FAILED)" : "");
		}
		strncat (Report, Line, sizeof Report - strlen (Report) - 1);
		Log (Line, nBadFrames ? COLOR_ERR : COLOR_OK);
	}

	McEnable (bMulti);
	FrameLinesToScreen (FALSE);	// as before the test: a BENCH after it measures the drawing apart
	WriteTextFile (MCTEST_PATH, Report);
	Log ("MC test: results in s16pi/MCTEST.TXT", COLOR_OK);
	SaveLog ();
	delete [] pState;
	delete [] pRef;
}

void CKernel::Benchmark (void)
{
	static unsigned Emu[BENCH_FRAMES], Draw[BENCH_FRAMES], Total[BENCH_FRAMES];
	u64 nSamples = 0;

	boolean bQuiet = m_bQuiet;
	ApplyVideoMode ();		// measure the real drawing path (filter, HVS)
	m_bQuiet = TRUE;

	// on screen once per second (outside the timings): the game and, above it,
	// what the benchmark is doing, so it is clear that it is not stuck
	auto Progress = [this] (const char *pPhase, unsigned nFrame, unsigned nFrames)
	{
		char Text[64];
		snprintf (Text, sizeof Text, "BENCHMARK  %s %u/%u S", pPhase,
			  nFrame / 60, nFrames / 60);
		const CScreen::TTextSpan Span = {Text, UI_WHITE};
		m_Video.DrawBandText (TRUE, &Span, 1);
		m_Video.Flip ();
	};

	Log ("Benchmark: warm-up", COLOR_TEXT);
	SaveLog ();

	// 1. warm-up: let the BIOS logo and the game's start-up pass
	for (unsigned i = 0; i < BENCH_WARMUP; i++)
	{
		s16_run_frame ();
		FrameSync ();
		FrameTakeAudio ();
		FrameDraw ();
		if (i % 60 == 0)
		{
			Progress ("WARM-UP", i, BENCH_WARMUP);
		}
	}

	Log ("Benchmark: measuring", COLOR_TEXT);
	SaveLog ();

	// 2. measurement, with the sampling profiler running
	CProfiler *pProfiler = new CProfiler (&m_Interrupt);
	boolean bProfile = pProfiler && pProfiler->Start (BENCH_PROFILE_US);

	enum { Worst = 10 };
	unsigned WorstIdx[Worst];
	unsigned nWorst = 0;
	unsigned nSlow = 0, nOver = 0;
	u32 nAllHash = 2166136261U;

	for (unsigned i = 0; i < BENCH_FRAMES; i++)
	{
		if (bProfile) pProfiler->BeginFrame ();
		unsigned t0 = CTimer::GetClockTicks ();
		s16_run_frame ();
		FrameSync ();
		FrameTakeAudio ();
		unsigned t1 = CTimer::GetClockTicks ();
		FrameDraw ();
		unsigned t2 = CTimer::GetClockTicks ();
		boolean bSlow = t2 - t0 >= BENCH_SLOW_US;
		if (bProfile) pProfiler->EndFrame (bSlow);

		// hash of every measured frame (outside the timings): the same game must
		// give the same value with and without multicore
		for (unsigned y = 0; y < GAME_H; y++)
		{
			const u32 *p = &g_VideoBuf[(y + GAME_SRC_Y) * GAME_SRC_PITCH + GAME_SRC_X];
			for (unsigned x = 0; x < GAME_W; x++)
			{
				nAllHash = (nAllHash ^ p[x]) * 16777619U;
			}
		}
		if (i % 60 == 0)
		{
			Progress ("MEASURING", i, BENCH_FRAMES);
		}

		Emu[i] = t1 - t0;
		Draw[i] = t2 - t1;
		Total[i] = t2 - t0;
		nSamples += g_nAudioFrames;
		if (bSlow) nSlow++;
		if (Total[i] > 16664) nOver++;

		// keep the indices of the slowest frames, slowest first
		unsigned k;
		if (nWorst < Worst)
		{
			k = nWorst++;
		}
		else if (Total[i] > Total[WorstIdx[Worst - 1]])
		{
			k = Worst - 1;
		}
		else
		{
			continue;
		}
		while (k > 0 && Total[WorstIdx[k-1]] < Total[i])
		{
			WorstIdx[k] = WorstIdx[k-1];
			k--;
		}
		WorstIdx[k] = i;
	}
	if (bProfile) pProfiler->Stop ();

	m_bQuiet = bQuiet;
	m_Video.SetConsoleMode ();
	RedrawConsole ();

	TStats E, D, T;
	ComputeStats (Emu, BENCH_FRAMES, &E);
	ComputeStats (Draw, BENCH_FRAMES, &D);
	ComputeStats (Total, BENCH_FRAMES, &T);

	char a[16], b[16], c[16], d[16];
	char Report[4096];
	size_t nPos = 0;
	char Line[160];

	CMachineInfo *pInfo = CMachineInfo::Get ();
	const unsigned nFrameUs = 16664;
	unsigned nSpeed = T.nAvg ? (unsigned) ((u64) nFrameUs * 100 / T.nAvg) : 0;

	#define REPORT(color) do { Log (Line, color); \
		nPos += snprintf (Report + nPos, sizeof Report - nPos, "%s\r\n", Line); } while (0)

	snprintf (Line, sizeof Line, "s16-pi %s - %s, %u MB, CPU %u MHz, %u C",
		  PROJECT_VERSION, pInfo->GetMachineName (), pInfo->GetRAMSize (),
		  m_CPUThrottle.GetClockRate () / 1000000, m_CPUThrottle.GetTemperature ());
	REPORT (COLOR_TEXT);
	snprintf (Line, sizeof Line, "Game: %s (measured after %u s of play, for %u s)",
		  m_pGames[m_nLoaded].File, BENCH_WARMUP / 60, BENCH_FRAMES / 60);
	REPORT (COLOR_TEXT);
	char Tag[8];
	VideoModeTag (Tag, sizeof Tag);
	snprintf (Line, sizeof Line, "Screen: mode %s, %ux%u, scale %ux (HVS %ux, CPU %ux), filter %s",
		  Tag, m_Video.GetWidth (), m_Video.GetHeight (), m_Video.GetScale (),
		  m_Video.GetHVSFactor (), m_Video.GetScale () / m_Video.GetHVSFactor (),
		  FilterName (m_nFilter));
	REPORT (COLOR_TEXT);
	snprintf (Line, sizeof Line, "Frame times (ms)            avg       p99     max     min");
	REPORT (COLOR_TITLE);
	FormatMs (a, sizeof a, E.nAvg); FormatMs (b, sizeof b, E.nP99);
	FormatMs (c, sizeof c, E.nMax); FormatMs (d, sizeof d, E.nMin);
	snprintf (Line, sizeof Line, "  emulation + audio       %7s %7s %7s %7s", a, b, c, d);
	REPORT (COLOR_TEXT);
	FormatMs (a, sizeof a, D.nAvg); FormatMs (b, sizeof b, D.nP99);
	FormatMs (c, sizeof c, D.nMax); FormatMs (d, sizeof d, D.nMin);
	snprintf (Line, sizeof Line, "  drawing to HDMI         %7s %7s %7s %7s", a, b, c, d);
	REPORT (COLOR_TEXT);
	FormatMs (a, sizeof a, T.nAvg); FormatMs (b, sizeof b, T.nP99);
	FormatMs (c, sizeof c, T.nMax); FormatMs (d, sizeof d, T.nMin);
	snprintf (Line, sizeof Line, "  total                   %7s %7s %7s %7s", a, b, c, d);
	REPORT (COLOR_TEXT);
	snprintf (Line, sizeof Line, "Top speed: %u%% of a real board", nSpeed);
	REPORT (nSpeed >= 150 ? COLOR_OK : COLOR_ERR);
	snprintf (Line, sizeof Line, "Frames over 16.7 ms (too slow for full speed): %u of %u (%u.%u%%)",
		  nOver, BENCH_FRAMES, nOver * 100 / BENCH_FRAMES, (nOver * 1000 / BENCH_FRAMES) % 10);
	REPORT (nOver ? COLOR_ERR : COLOR_OK);
	snprintf (Line, sizeof Line, "Slowest frames (second.frame: emulation + drawing ms):");
	REPORT (COLOR_TITLE);
	for (unsigned w = 0; w < nWorst; w += 2)
	{
		size_t n = 0;
		Line[0] = 0;
		for (unsigned j = w; j < w + 2 && j < nWorst; j++)
		{
			unsigned f = WorstIdx[j];
			FormatMs (a, sizeof a, Emu[f]); FormatMs (b, sizeof b, Draw[f]);
			n += snprintf (Line + n, sizeof Line - n, "  %3u.%02u: %s + %s",
				       f / 60, f % 60, a, b);
		}
		REPORT (COLOR_TEXT);
	}
	snprintf (Line, sizeof Line, "Audio: %u stereo samples per frame at %u Hz",
		  (unsigned) (nSamples / BENCH_FRAMES), SAMPLE_RATE);
	REPORT (COLOR_DIM);
	// same game + same build options must give the same picture after the
	// same number of frames: used to check compiler optimisations
	u32 nHash = 2166136261U;
	for (unsigned i = 0; i < GAME_SRC_PITCH * GAME_SRC_LINES; i++)
	{
		nHash = (nHash ^ g_VideoBuf[i]) * 16777619U;
	}
	snprintf (Line, sizeof Line, "Check: picture after %u frames = %08X",
		  BENCH_WARMUP + BENCH_FRAMES, nHash);
	REPORT (COLOR_DIM);
	snprintf (Line, sizeof Line, "Check: all %u measured frames = %08X", BENCH_FRAMES, nAllHash);
	REPORT (COLOR_DIM);
	snprintf (Line, sizeof Line, "Multicore: %s", McFailed () ? "FAILED, core 1 stopped answering"
		  : (McEnabled () ? "on, video lines on core 1" : "off"));
	REPORT (McFailed () ? COLOR_ERR : COLOR_DIM);
	if (McFailed ())
	{
		McFailText (Line, sizeof Line, FrameMcFailFrame ());
		REPORT (COLOR_ERR);
	}
	if (bProfile)
	{
		snprintf (Line, sizeof Line, "Profile: %u samples (%u in frames over %u ms) -> PROFILE.TXT",
			  pProfiler->GetTotal (), pProfiler->GetSlow (), BENCH_SLOW_US / 1000);
	}
	else
	{
		snprintf (Line, sizeof Line, "Profile: not available");
	}
	REPORT (COLOR_DIM);

	WriteTextFile (BENCH_PATH, Report);
	if (bProfile)
	{
		const size_t nSize = 1024 * 1024;
		char *pText = new char[nSize];
		if (pText)
		{
			pProfiler->Format (pText, nSize);
			WriteTextFile (PROFILE_PATH, pText);
			delete [] pText;
		}
	}
	delete pProfiler;
	Log ("Results saved to s16pi/BENCH.TXT and s16pi/PROFILE.TXT", COLOR_OK);
	SaveLog ();

	s16_reset ();		// play from a clean boot
	HiStart ();
}
