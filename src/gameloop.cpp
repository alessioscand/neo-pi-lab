/*
 * s16-pi - the game loop: pads to the control panels, one emulated frame per
 * screen frame (beam raced, or double buffered with a frame delay), the
 * in-game menu, the messages and the status line in the black bands
 */
#include "kernel.h"
#include "frame.h"
#include "mcline.h"
#include "pacing.h"
#include "racing.h"
#include "stats.h"
#include "smp.h"
#include "fontdata.h"
#include "s16_api.h"

extern "C" {
void *s16pi_png_encode (const void *pRGB, int nWidth, int nHeight, size_t *pSize);
void s16pi_png_free (void *p);
}

#define REPORT_FRAMES	3600		// one statistics line per minute of play

enum { GmSave, GmLoad, GmDips, GmOptions, GmReset, GmList, GmPower, GmCount };

// Before a frame: with beam racing, wait for the moment the plan gives
// against the HDMI scan-out (returns that moment); otherwise, while playing,
// wait the frame delay after the vsync (returns 0). The time spent waiting
// does not count as work: *pT0 moves on by it.
static unsigned WaitForFrameStart (boolean bRace, boolean bPlaying, unsigned nLastVSync, unsigned *pT0)
{
	if (bRace)
	{
		u64 nStart = g_Race.pRacer->PlanFrame (Ticks ());
		unsigned tWait = CTimer::GetClockTicks ();
		while ((s64) (Ticks () - nStart) < 0)
		{
			g_Race.pRaster->Sample ();
		}
		g_Race.pRacer->FrameGo (Ticks ());
		unsigned nGoUs = CTimer::GetClockTicks ();
		*pT0 += nGoUs - tWait;
		return nGoUs;
	}
	if (bPlaying && g_Delay.nUs > 0)
	{
		unsigned tGo = nLastVSync + g_Delay.nUs;
		unsigned tWait = CTimer::GetClockTicks ();
		while ((int) (CTimer::GetClockTicks () - tGo) < 0)
		{
		}
		*pT0 += CTimer::GetClockTicks () - tWait;
	}
	return 0;
}

// The save state file: the whole machine, as s16_state_save writes it
static boolean SaveStateFile (const char *pPath)
{
	boolean bOK = FALSE;
	size_t nSize = s16_state_size ();
	u8 *pState = new u8[nSize];
	int nUsed = pState ? s16_state_save (pState, nSize) : 0;
	FIL File;
	if (nUsed > 0 && f_open (&File, pPath, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		UINT nWritten = 0;
		bOK = f_write (&File, pState, (UINT) nUsed, &nWritten) == FR_OK
		      && nWritten == (UINT) nUsed;
		f_close (&File);
	}
	delete [] pState;
	return bOK;
}

static boolean LoadStateFile (const char *pPath)
{
	boolean bOK = FALSE;
	FIL File;
	if (f_open (&File, pPath, FA_READ) == FR_OK)
	{
		size_t nSize = f_size (&File);
		u8 *pState = new u8[nSize];
		UINT nRead = 0;
		if (pState && f_read (&File, pState, nSize, &nRead) == FR_OK && nRead == nSize)
		{
			McSync ();
			bOK = s16_state_load (pState, nSize) != 0;
		}
		f_close (&File);
		delete [] pState;
	}
	return bOK;
}

void CKernel::RunGame (void)
{
	PlayStart ();
	m_pInput->RestartDiagnostics ();		// log the pad reports of this game too
	m_ActLED.On ();			// steady while playing (no firmware call every second)

	unsigned nLastVSync = CTimer::GetClockTicks ();
	unsigned nNextDeadline = nLastVSync + 16667;
	unsigned nTotalFrames = 0;

	g_Stats.Reset (nLastVSync);
	DelayStatsReset ();
	DelayRestart ();
	g_Race.bGame = g_Race.bReady && McEnabled ();
	if (g_Race.bGame)
	{
		// a game that gave up on beam racing keeps the double buffer until it is left:
		// the next game started from the list tries again
		g_Race.pRacer->Rearm ();
		g_Race.pRacer->SetGeometry (m_Video.GetGameY (), m_Video.GetCPUScale (), m_Video.GetHVSFactor ());
		g_Race.pRacer->Restart ();
		g_Race.pRacer->ResetStats ();
	}
	{
		unsigned nOld;
		m_pInput->TakeChange (&nOld);	// a press from the list does not count
	}
	{
		unsigned a, b, c;
		int d;
		m_Audio.GetStats (&a, &b, &c, &d);	// reset audio statistics
	}

	while (1)
	{
		unsigned t0 = CTimer::GetClockTicks ();
		unsigned t1 = t0;

		if (nTotalFrames % 30 == 0)
		{
			m_pInput->UpdateDevices ();

			int nPad = m_Play.bMenu ? -1 : m_pInput->GetUnmappedPad ();
			if (nPad >= 0)
			{
				// new pad during the game: pause and set it up
				m_pUI->SnapshotGame (g_VideoBuf);
				m_Video.SetRacing (FALSE);
				RunMapping ((unsigned) nPad, TRUE);
				ResetGameStats ();
				for (unsigned p = 0; p < 4; p++) m_Play.nPrev[p] = ~0u;
				unsigned nOld;
				m_pInput->TakeChange (&nOld);	// presses during the set-up do not count
			}
		}

		const boolean bRace = g_Race.bGame && !m_Play.bMenu;
		m_Video.SetRacing (bRace);
		const unsigned nGoUs = WaitForFrameStart (bRace, !m_Play.bMenu, nLastVSync, &t0);
		t1 = t0;
		boolean bOddFrame = FALSE;	// slow for its own reasons: not a measure of the game

		// read controls as late as possible, right before emulating the frame
		unsigned nChangeTicks;
		boolean bChange = m_pInput->TakeChange (&nChangeTicks);
		u32 Pad[4] = {m_pInput->Get (0), m_pInput->Get (1), m_pInput->Get (2), m_pInput->Get (3)};

		if (!m_Play.bMenu)
		{
			TPlayInput In;
			PlayReadPanels (Pad, &In);
			if (In.bOpenCredits)
			{
				// Select + D: credits over the paused game (B closes)
				m_pUI->SnapshotGame (g_VideoBuf);
				m_Video.SetRacing (FALSE);
				RunCredits ();
				PlayWaitRelease ();
				m_Play.nToast = 0;
				m_Play.nClearToast = m_nClearStatus = 2;
				ResetGameStats ();
				continue;
			}
			if (In.bOpenMenu)
			{
				PlayOpenMenu ();
			}
			else
			{
				t1 = PlayFrame (bRace, In.bScreenshot, &t0, &bOddFrame);
			}
		}
		if (m_Play.bMenu && !PlayMenu (Pad))
		{
			return;					// GAME LIST
		}
		PlayDrawBands ();

		unsigned t2 = CTimer::GetClockTicks ();
		const boolean bRaced = bRace && !m_Play.bMenu;	// already on screen: no flip, no vsync
		if (!bRaced)
		{
			m_Video.Flip ();
		}
		unsigned t3 = CTimer::GetClockTicks ();

		if (bRaced)
		{
			nNextDeadline = t3 + 16667;	// the next frame start paces the loop
		}
		else if (m_bVSync)
		{
			m_Video.WaitForVSync ();
		}
		else
		{
			while ((int) (CTimer::GetClockTicks () - nNextDeadline) < 0)
			{
			}
			nNextDeadline += 16667;
		}
		unsigned t4 = CTimer::GetClockTicks ();
		unsigned nPeriod = t4 - nLastVSync;
		nLastVSync = t4;
		nTotalFrames++;

		if (g_Stats.bSkipFrame)
		{
			g_Stats.bSkipFrame = FALSE;	// spans the pause: not a real frame time
		}
		else if (!m_Play.bMenu)
		{
			g_Stats.Emu.Add (t1 - t0);
			g_Stats.Work.Add (t2 - t0);
			g_Stats.Period.Add (nPeriod);
			if (bRaced)
			{
				// pad report -> the HVS starts composing the top of the picture
				if (bChange)
				{
					g_Stats.Lag.Add (nGoUs - nChangeTicks
							 + (unsigned) g_Race.pRaster->TicksToUs ((s64) g_Race.pRacer->GetPadToPicture ()));
				}
			}
			else
			{
				g_Stats.Slack.Add (t4 - t3);
				if (!bOddFrame)
				{
					DelayUpdate (t3 - t0, nPeriod > 25000);
				}
				if (bChange)
				{
					g_Stats.Lag.Add (t4 - nChangeTicks);
				}
			}
			if (t3 - t0 > 16000) g_Stats.nOverBudget++;
			if (nPeriod > 25000) g_Stats.nMissedVSync++;
			g_Stats.nFrames++;
			if (g_Stats.nFrames >= REPORT_FRAMES)
			{
				ReportWindow (FALSE);
				m_pInput->FlushDiagnostics ();
			}
		}

		if (!m_Play.bMenu && t2 - t0 > m_Play.nWorkMax)
		{
			m_Play.nWorkMax = t2 - t0;
		}
		if (++m_Play.nFrames == 60)
		{
			PlayStatusLine (t4, &nNextDeadline);
		}
	}
}

void CKernel::PlayStart (void)
{
	TPlay &P = m_Play;
	memset (&P, 0, sizeof P);
	const int nGame = m_pGames[m_nLoaded].nGame;
	P.nPlayers = (unsigned) s16_game_players (nGame);
	P.nMenuPrev = ~0u;
	P.nMenuMsgColor = UI_OK;
	P.nFpsColor = UI_WHITE;
	P.nLoadColor = UI_OK;
	P.nTempColor = UI_WHITE;
	P.nWinStart = CTimer::GetClockTicks ();
	for (unsigned g = 0; g < 3; g++)
	{
		P.nAimX[g] = (S16_WIDTH / 2) * 16;
		P.nAimY[g] = (S16_HEIGHT / 2) * 16;
	}
	P.nGunAdjust = m_bGunAdjust ? 0 : -1;
	m_bGunAdjust = FALSE;
	PlayWaitRelease ();			// buttons still held from the list
}

// buttons pressed now reach the game only once released
void CKernel::PlayWaitRelease (void)
{
	for (unsigned p = 0; p < 4; p++)
	{
		m_Play.nPrev[p] = ~0u;
		m_Play.bSelectCombo[p] = FALSE;
	}
}

// Laser Ghost: the stick moves the crosshair (slowly at first, then faster),
// A fires, B is the bomb. While the battery RAM is blank the game asks for the
// gun adjustment, which s16_gun_adjust does for the players: the pads wait.
void CKernel::PlayGuns (const u32 Pad[4], u8 Player[4], boolean bPaused)
{
	const int nGuns = s16_guns ();
	if (nGuns == 0)
	{
		return;
	}
	TPlay &P = m_Play;
	if (P.nGunAdjust >= 0)
	{
		u8 Fire[3];
		if (bPaused)
		{
			memset (Player, 0, 3);
			return;
		}
		if (s16_gun_adjust (P.nGunAdjust, Fire))
		{
			for (int g = 0; g < nGuns; g++)
			{
				s16_set_crosshair (g, TRUE);
				Player[g] = Fire[g];
			}
			if (P.nGunAdjust++ == 0)
			{
				snprintf (P.Toast, sizeof P.Toast, "ADJUSTING THE GUNS");
				snprintf (P.ToastSide, sizeof P.ToastSide, "GUN\nADJUST");
				P.nToast = 600;
			}
			return;
		}
		P.nGunAdjust = -1;
		P.nToast = 0;
		Log ("  guns adjusted", COLOR_TEXT);
	}
	for (int g = 0; g < nGuns; g++)
	{
		const boolean bOn = s16_gun_in_play (g);	// not in attract mode, at the continue or after the game over
		const u32 n = bPaused ? 0 : Pad[g];
		const int dx = (n & BTN_RIGHT) ? 1 : (n & BTN_LEFT) ? -1 : 0;
		const int dy = (n & BTN_DOWN) ? 1 : (n & BTN_UP) ? -1 : 0;
		if (dx || dy)
		{
			// 1.5 pixels a frame, up to 5 after a quarter of a second
			const int v = P.nAimHeld[g] < 16 ? 24 + 3 * (int) P.nAimHeld[g] : 72;
			P.nAimHeld[g]++;
			P.nAimX[g] += dx * v;
			P.nAimY[g] += dy * v;
			P.nAimX[g] = P.nAimX[g] < 0 ? 0 : P.nAimX[g] > (S16_WIDTH - 1) * 16 ? (S16_WIDTH - 1) * 16 : P.nAimX[g];
			P.nAimY[g] = P.nAimY[g] < 0 ? 0 : P.nAimY[g] > (S16_HEIGHT - 1) * 16 ? (S16_HEIGHT - 1) * 16 : P.nAimY[g];
		}
		else
		{
			P.nAimHeld[g] = 0;
		}
		s16_set_aim (g, P.nAimX[g] >> 4, P.nAimY[g] >> 4);
		s16_set_crosshair (g, bOn);
		Player[g] &= 0x30;			// the trigger and the bomb only
	}
}

// The pads to the control panels and the system port, and the Select
// combinations: + A screenshot, + B lazy finger, + C status line, + D credits,
// alone for one second: menu (a short press is a coin)
void CKernel::PlayReadPanels (const u32 Pad[4], TPlayInput *pIn)
{
	TPlay &P = m_Play;
	u8 Player[4];				// panels, active high (the core inverts them)
	boolean bOpenMenu = FALSE, bOpenCredits = FALSE, bScreenshot = FALSE;
	u32 Eff[4] = {0, 0, 0, 0};
	for (unsigned p = 0; p < 4; p++)
	{
		u32 n = Pad[p];
		if (P.nPrev[p] == ~0u)
		{
			// buttons still held from the list or the menu: ignore until released
			if (n == 0)
			{
				P.nPrev[p] = 0;
			}
			Player[p] = 0;
			continue;
		}

		u32 nNew = n & ~P.nPrev[p];

		if (n & BTN_SELECT)
		{
			if (nNew & (BTN_A | BTN_B | BTN_C | BTN_D))
			{
				P.bSelectCombo[p] = TRUE;
			}
			if (nNew & BTN_A)
			{
				bScreenshot = TRUE;
			}
			if (nNew & BTN_B)
			{
				P.bLazy[p] = !P.bLazy[p];
				const char *pOn = P.bLazy[p] ? "ON" : "OFF";
				if (p == 0)
				{
					snprintf (P.Toast, sizeof P.Toast, "LAZY FINGER %s", pOn);
					snprintf (P.ToastSide, sizeof P.ToastSide, "LAZY\nFINGER\n%s", pOn);
				}
				else
				{
					snprintf (P.Toast, sizeof P.Toast, "P%u LAZY FINGER %s", p + 1, pOn);
					snprintf (P.ToastSide, sizeof P.ToastSide, "P%u\nLAZY\nFINGER\n%s", p + 1, pOn);
				}
				P.nToast = 60;
				Log (P.bLazy[p] ? "  lazy finger on" : "  lazy finger off", COLOR_TEXT);
			}
			if (nNew & BTN_C)
			{
				m_bStatusOn = !m_bStatusOn;
				m_nClearStatus = 2;
			}
			if (nNew & BTN_D)
			{
				bOpenCredits = TRUE;
			}
			if (!P.bSelectCombo[p] && ++P.nSelectHeld[p] >= 60)
			{
				bOpenMenu = TRUE;
				P.bSelectCombo[p] = TRUE;
			}
		}
		else
		{
			if ((P.nPrev[p] & BTN_SELECT) && !P.bSelectCombo[p])
			{
				P.nCoin[p] = 8;
			}
			P.bSelectCombo[p] = FALSE;
			P.nSelectHeld[p] = 0;
		}
		if (nNew & BTN_MENU)
		{
			bOpenMenu = TRUE;
		}
		P.nPrev[p] = n;
		Eff[p] = n;

		// Lazy finger: a held A-D button repeats, 2 frames pressed and
		// 2 released (15 per second), starting pressed at once
		u32 nGame = n;
		static const u32 LazyBtn[4] = {BTN_A, BTN_B, BTN_C, BTN_D};
		for (unsigned b = 0; b < 4; b++)
		{
			if (!(n & LazyBtn[b]))
			{
				P.nLazyCount[p][b] = 0;
			}
			else if (P.bLazy[p] && (P.nLazyCount[p][b]++ & 3) >= 2)
			{
				nGame &= ~LazyBtn[b];
			}
		}

		// panel: bit 0 right, 1 left, 2 down, 3 up, 4 button 1 (A), 5 button 2
		// (B), 6 button 3 (C); while Select is held only the joystick goes through
		u32 nPanel = (n & BTN_SELECT) ? (n & 0x0F) : nGame;
		u8 nBits = 0;
		if (nPanel & BTN_RIGHT)	nBits |= 0x01;
		if (nPanel & BTN_LEFT)	nBits |= 0x02;
		if (nPanel & BTN_DOWN)	nBits |= 0x04;
		if (nPanel & BTN_UP)	nBits |= 0x08;
		if (nPanel & BTN_A)	nBits |= 0x10;
		if (nPanel & BTN_B)	nBits |= 0x20;
		if (nPanel & BTN_C)	nBits |= 0x40;
		Player[p] = nBits;
	}

	// starts in bits 0-3 of the system port, coins in bits 4-7; nothing
	// reaches the game while the menu or the credits open
	u8 nSystem = 0;
	for (unsigned p = 0; p < 4; p++)
	{
		if (p >= P.nPlayers || bOpenMenu || bOpenCredits)
		{
			Player[p] = 0;
			P.nCoin[p] = 0;
			continue;
		}
		if (P.nCoin[p]) { nSystem |= (u8) (0x10 << p); P.nCoin[p]--; }
		if ((Eff[p] & BTN_START) && !(Eff[p] & BTN_SELECT)) nSystem |= (u8) (0x01 << p);
	}
	PlayGuns (Eff, Player, bOpenMenu || bOpenCredits);
	for (unsigned p = 0; p < 4; p++) s16_set_player ((int) p, Player[p]);
	s16_set_system (nSystem);

	pIn->bOpenMenu = bOpenMenu;
	pIn->bOpenCredits = bOpenCredits;
	pIn->bScreenshot = bScreenshot;
}

void CKernel::PlayOpenMenu (void)
{
	TPlay &P = m_Play;
	P.bMenu = TRUE;
	P.nMenuSel = 0;
	P.MenuMsg[0] = '\0';
	P.nMenuPrev = ~0u;
	P.nToast = 0;
	char Path[160];
	StatePath (Path, sizeof Path);
	FILINFO Info;
	P.bSaved = f_stat (Path, &Info) == FR_OK;
	m_pUI->SnapshotGame (g_VideoBuf);
	m_Video.SetRacing (FALSE);
	SaveGameData ();
	m_pInput->FlushDiagnostics ();
	ReportWindow (TRUE);
	SaveLog ();
}

// One emulated frame, its sound and its picture. Returns when the emulation
// ended; *pT0 moves on by the time spent waiting for the beam.
unsigned CKernel::PlayFrame (boolean bRace, boolean bScreenshot, unsigned *pT0, boolean *pOddFrame)
{
	TPlay &P = m_Play;
	if (bScreenshot)
	{
		*pOddFrame = TRUE;
		// the frame on screen, before emulating the next one
		const char *pFile = SaveScreenshot ();
		snprintf (P.Toast, sizeof P.Toast, pFile ? "SCREENSHOT SAVED  %s" : "SCREENSHOT FAILED", pFile);
		snprintf (P.ToastSide, sizeof P.ToastSide, "SCREEN\nSHOT\n%s", pFile ? "SAVED" : "FAILED");
		P.nToast = 60;
	}
	if (bRace)
	{
		FrameSetRaced (TRUE);
		g_Race.nWaitTicks = 0;
		s16_set_line_hook (RaceLineHook, 0);
	}
	const unsigned nSlow = s16_slow_work ();
	s16_run_frame ();
	if (s16_slow_work () != nSlow)
	{
		*pOddFrame = TRUE;		// pages decrypted: the beam racing timings start again
	}
	FrameSync ();
	HiTick ();
	if (bRace)
	{
		s16_set_line_hook (0, 0);
		FrameSetRaced (FALSE);
		if (*pOddFrame)
		{
			g_Race.pRacer->Restart ();	// the screenshot took the time
		}
		else
		{
			g_Race.pRacer->FrameEnd ();
		}
		g_Race.pRaster->EndFrame ();
		// the waits for the beam are not work: as if the frame had started later
		*pT0 += (unsigned) g_Race.pRaster->TicksToUs ((s64) g_Race.nWaitTicks);
		if (g_Race.pRacer->Failed ())
		{
			g_Race.bGame = FALSE;
			Log ("Beam racing: rows late in too many frames, double buffer for this game", COLOR_ERR);
		}
		if (!McEnabled ())
		{
			// core 1 stopped: the lines are no longer drawn as they come
			g_Race.bGame = FALSE;
			m_Video.SetRacing (FALSE);
			m_Video.DrawGame (g_VideoBuf);
			m_Video.Flip ();
			Log ("Beam racing: off, single core from now on", COLOR_ERR);
		}
	}
	unsigned t1 = CTimer::GetClockTicks ();
	FrameTakeAudio ();
	m_Audio.WriteFrame (g_AudioBuf, g_nAudioFrames);
	FrameDraw ();
	return t1;
}

// One frame of the in-game menu over the paused game. FALSE: back to the list.
boolean CKernel::PlayMenu (const u32 Pad[4])
{
	TPlay &P = m_Play;
	static const unsigned Ids[GmCount] = {GmSave, GmLoad, GmDips, GmOptions, GmReset, GmList, GmPower};
	const unsigned nIds = GmCount;		// RESUME is the B button
	if (P.nMenuSel >= nIds) P.nMenuSel = 0;

	u32 n = Pad[0] | Pad[1] | Pad[2] | Pad[3];	// whoever opened it can use it
	u32 nNew = P.nMenuPrev == ~0u ? 0 : (n & ~P.nMenuPrev);
	P.nMenuPrev = n;
	unsigned nId = Ids[P.nMenuSel];

	if (nNew & BTN_UP)	{ P.nMenuSel = (P.nMenuSel + nIds - 1) % nIds; P.MenuMsg[0] = '\0'; }
	if (nNew & BTN_DOWN)	{ P.nMenuSel = (P.nMenuSel + 1) % nIds; P.MenuMsg[0] = '\0'; }

	if (nNew & BTN_B)
	{
		P.bMenu = FALSE;		// resume
	}
	else if (nNew & BTN_A)
	{
		char Path[160];
		StatePath (Path, sizeof Path);

		switch (nId)
		{
		case GmSave:
		{
			boolean bOK = SaveStateFile (Path);
			if (bOK)
			{
				P.bSaved = TRUE;
				snprintf (P.MenuMsg, sizeof P.MenuMsg, "SAVED");
				P.nMenuMsgColor = UI_OK;
			}
			else
			{
				snprintf (P.MenuMsg, sizeof P.MenuMsg, "SAVE FAILED");
				P.nMenuMsgColor = UI_ERR;
			}
			Log (bOK ? "  save state: saved" : "  save state: FAILED", COLOR_TEXT);
			break;
		}

		case GmLoad:
		{
			if (!P.bSaved)
			{
				break;			// disabled until something is saved
			}
			boolean bOK = LoadStateFile (Path);
			if (bOK)
			{
				Log ("  save state: loaded", COLOR_TEXT);
				P.bMenu = FALSE;
			}
			else
			{
				snprintf (P.MenuMsg, sizeof P.MenuMsg, "LOAD FAILED");
				P.nMenuMsgColor = UI_ERR;
			}
			break;
		}

		case GmReset:
			HiSave ();		// the table as it is, then wait for the game to set it up again
			McSync ();
			s16_reset ();
			HiStart ();
			if (P.nGunAdjust > 0) P.nGunAdjust = 0;	// the adjustment starts again
			Log ("  reset", COLOR_TEXT);
			P.bMenu = FALSE;
			break;

		case GmDips:			// over the paused game; B comes back here
			RunDipMenu ();
			P.nMenuPrev = ~0u;
			break;

		case GmOptions:
			RunSystemMenu ();
			P.nMenuPrev = ~0u;
			break;

		case GmList:
			SaveGameData ();
			SaveLog ();
			m_Video.SetRacing (FALSE);
			return FALSE;

		case GmPower:
			if (PowerOffCountdown ())
			{
				PowerOff ();
			}
			P.nMenuPrev = ~0u;
			break;
		}
	}

	if (!P.bMenu)
	{
		PlayWaitRelease ();
		ResetGameStats ();
		m_Video.DrawGame (g_VideoBuf);
		return TRUE;
	}

	static const char *Labels[GmCount] =
	{
		"SAVE STATE", "LOAD STATE", "DIP SWITCHES",
		"OPTIONS", "RESET", "GAME LIST", "POWER OFF"
	};
	TMenuItem Items[GmCount];
	for (unsigned i = 0; i < nIds; i++)
	{
		Items[i].pLabel = Labels[Ids[i]];
		Items[i].pValue = 0;
		Items[i].bDisabled = Ids[i] == GmLoad && !P.bSaved;
	}
	static const TButtonHint Hints[2] = {{'A', "SELECT"}, {'B', "RESUME"}};
	m_pUI->DrawPanelMenu (TRUE, m_pGames[m_nLoaded].Name, Items, nIds, P.nMenuSel,
			      P.MenuMsg, P.nMenuMsgColor, Hints, 2, 0);
	FrameDrawPicture (m_pUI->GetBuffer ());
	return TRUE;
}

// the black bands (or side columns at 720p): status line and messages, only while playing
void CKernel::PlayDrawBands (void)
{
	TPlay &P = m_Play;
	const boolean bSide = m_Video.HasSideColumns ();	// fixed for the game
	if (m_bStatusOn && !P.bMenu && P.bStatusReady && bSide)
	{
		// left column, one item per line; "3X CPU+HVS" on two lines
		const char *pScale = P.StScale[0] == ' ' ? P.StScale + 1 : P.StScale;
		char Scale[16];
		strcpy (Scale, pScale);
		if (strlen (Scale) > 9)
		{
			char *pSp = strchr (Scale, ' ');
			if (pSp) *pSp = '\n';
		}
		const CScreen::TTextSpan Spans[] =
		{
			{P.StFps, P.nFpsColor}, {"\nLOAD ", UI_GREY}, {P.StLoad, P.nLoadColor},
			{"\n", UI_GREY}, {P.StTemp, P.nTempColor},
			{g_Race.bGame ? "\nRACE " : "\nDLY ", UI_GREY}, {P.StDelay, UI_WHITE},
			{"\n", UI_GREY}, {P.StRes, UI_WHITE}, {"\n", UI_GREY}, {Scale, UI_GREY}
		};
		m_Video.DrawSideText (FALSE, Spans, sizeof Spans / sizeof Spans[0]);
		m_nClearStatus = 2;
	}
	else if (m_bStatusOn && !P.bMenu && P.bStatusReady)
	{
		const CScreen::TTextSpan Spans[] =
		{
			{P.StFps, P.nFpsColor}, {"   LOAD ", UI_GREY}, {P.StLoad, P.nLoadColor},
			{"   ", UI_GREY}, {P.StTemp, P.nTempColor},
			{g_Race.bGame ? "   RACE " : "   DLY ", UI_GREY}, {P.StDelay, UI_WHITE},
			{"   ", UI_GREY}, {P.StRes, UI_WHITE}, {P.StScale, UI_GREY}
		};
		m_Video.DrawBandText (FALSE, Spans, sizeof Spans / sizeof Spans[0]);
		m_nClearStatus = 2;
	}
	else if (m_nClearStatus)
	{
		if (bSide)
		{
			m_Video.DrawSideText (FALSE, 0, 0);
		}
		else
		{
			m_Video.DrawBandText (FALSE, 0, 0);
		}
		m_nClearStatus--;
	}
	if (P.nToast && !P.bMenu)
	{
		// side columns: the message on the right, one word per line
		const CScreen::TTextSpan Span = {bSide ? P.ToastSide : P.Toast, UI_WHITE};
		if (bSide)
		{
			m_Video.DrawSideText (TRUE, &Span, 1);
		}
		else
		{
			m_Video.DrawBandText (TRUE, &Span, 1);
		}
		P.nToast--;
		P.nClearToast = 2;
	}
	else if (P.nClearToast)
	{
		if (bSide)
		{
			m_Video.DrawSideText (TRUE, 0, 0);
		}
		else
		{
			m_Video.DrawBandText (TRUE, 0, 0);
		}
		P.nClearToast--;
	}
}

// once per second: the status line's values (60 FPS / 59.8 FPS, load = worst
// frame of the second vs 16.7 ms)
void CKernel::PlayStatusLine (unsigned nNow, unsigned *pNextDeadline)
{
	TPlay &P = m_Play;
	unsigned nElapsed = nNow - P.nWinStart;
	unsigned nFPS100 = (unsigned) (6000ULL * 1000000 / nElapsed);

	if (m_bVSync && nFPS100 > 7500)
	{
		m_bVSync = FALSE;
		*pNextDeadline = CTimer::GetClockTicks () + 16667;
		Log ("firmware vsync does not block: switching to timer pacing", COLOR_ERR);
	}

	if (nFPS100 >= 5995)
		snprintf (P.StFps, sizeof P.StFps, "%u FPS", (nFPS100 + 50) / 100);
	else
		snprintf (P.StFps, sizeof P.StFps, "%u.%u FPS", nFPS100 / 100, (nFPS100 % 100) / 10);
	P.nFpsColor = nFPS100 < 5990 ? UI_WARN : UI_WHITE;
	unsigned nLoad = P.nWorkMax * 100 / 16667;
	snprintf (P.StLoad, sizeof P.StLoad, "%u%%", nLoad);
	P.nLoadColor = nLoad > 95 ? UI_ERR : (nLoad >= 75 ? UI_WARN : UI_OK);
	if (P.nSeconds++ % 4 == 0)
	{
		P.nTemp = m_CPUThrottle.GetTemperature ();	// a firmware call: every 4 s
	}
	snprintf (P.StTemp, sizeof P.StTemp, "%u" FONT_DEGREE "C", P.nTemp);
	P.nTempColor = P.nTemp > 75 ? UI_ERR : (P.nTemp > 65 ? UI_WARN : UI_WHITE);
	snprintf (P.StRes, sizeof P.StRes, "%uP", m_Video.GetHeight ());
	unsigned us = g_Race.bGame ? (unsigned) g_Race.pRaster->TicksToUs ((s64) g_Race.pRacer->GetPadToPicture ())
				   : g_Delay.nUs;
	snprintf (P.StDelay, sizeof P.StDelay, "%u.%u", us / 1000, (us % 1000) / 100);
	unsigned k = m_Video.GetHVSFactor (), S = m_Video.GetScale ();
	snprintf (P.StScale, sizeof P.StScale, " %uX %s", S,
		  k == S ? "HVS" : (k == 1 ? "CPU" : "CPU+HVS"));
	P.bStatusReady = TRUE;

	m_CPUThrottle.Update ();	// does its firmware calls every 4 s
	P.nFrames = 0;
	P.nWorkMax = 0;
	P.nWinStart = CTimer::GetClockTicks ();
}

// Select+A: the original picture (320x224, no filter) as
// s16pi/shots/<game>_NNN.png; returns the file name for the message, or 0
const char *CKernel::SaveScreenshot (void)
{
	static char Name[80];
	if (m_nLoaded < 0)
	{
		return 0;
	}
	char Base[64], Path[160];
	strcpy (Base, m_pGames[m_nLoaded].File);
	size_t n = strlen (Base);
	if (n > 4) Base[n - 4] = '\0';

	// next free number (the search starts after the last one used)
	FILINFO Info;
	for (;;)
	{
		if (++m_nShots > 999)
		{
			return 0;
		}
		snprintf (Name, sizeof Name, "%s_%03u.png", Base, m_nShots);
		snprintf (Path, sizeof Path, "%s/%s", SHOT_DIR, Name);
		if (f_stat (Path, &Info) != FR_OK)
		{
			break;
		}
	}

	static u8 RGB[GAME_W * GAME_H * 3];
	u8 *d = RGB;
	for (unsigned y = 0; y < GAME_H; y++)
	{
		const u32 *s = g_VideoBuf + (GAME_SRC_Y + y) * GAME_SRC_PITCH + GAME_SRC_X;
		for (unsigned x = 0; x < GAME_W; x++)
		{
			*d++ = (u8) (s[x] >> 16);
			*d++ = (u8) (s[x] >> 8);
			*d++ = (u8) s[x];
		}
	}
	size_t nSize = 0;
	void *pPng = s16pi_png_encode (RGB, GAME_W, GAME_H, &nSize);
	if (pPng == 0)
	{
		return 0;
	}
	FIL File;
	boolean bOK = FALSE;
	if (f_open (&File, Path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		UINT nWritten = 0;
		bOK = f_write (&File, pPng, nSize, &nWritten) == FR_OK && nWritten == nSize;
		f_close (&File);
	}
	s16pi_png_free (pPng);

	char Line[200];
	snprintf (Line, sizeof Line, "  screenshot %s: %s", Name, bOK ? "saved" : "FAILED");
	Log (Line, bOK ? COLOR_TEXT : COLOR_ERR);
	return bOK ? Name : 0;
}
