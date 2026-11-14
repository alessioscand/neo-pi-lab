/*
 * s16-pi - kernel: start-up, the main flow (list, game, list...), power off
 */
#include "kernel.h"
#include "frame.h"
#include "mcline.h"
#include "fbmap.h"
#include "tvcore.h"
#include "s16_api.h"
#include <circle/machineinfo.h>
#include <circle/memory.h>
#include <circle/multicore.h>
#include <circle/bcmwatchdog.h>

CKernel *CKernel::s_pThis = 0;

// Core 1 draws the video lines while core 0 runs the two CPUs and the sound
// (mcline.cpp); core 2 runs the COMPOSITE filter (tvcore.cpp); core 3 idles
class CVideoCore : public CMultiCoreSupport
{
public:
	CVideoCore (void) : CMultiCoreSupport (CMemorySystem::Get ()) {}
	void Run (unsigned nCore)
	{
		if (nCore == 1)
		{
			FbSetMair ();			// the framebuffer's write-combining type
			McWorker ();
		}
		else if (nCore == 2)
		{
			FbSetMair ();
			TvWorkerRun ();
		}
	}
};

CKernel::CKernel (void)
:	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_CPUThrottle (CPUSpeedMaximum),
	m_USBHCI (&m_Interrupt, &m_Timer, TRUE),
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
	m_pInput (0),
	m_pUI (0),
	m_bVideoOK (FALSE),
	m_bSDOK (FALSE),
	m_bQuiet (TRUE),		// black screen while booting; text only on errors
	m_nLines (0),
	m_nRepeat (0),
	m_pZip (0),
	m_nZipSize (0),
	m_pParentZip (0),
	m_nParentZipSize (0),
	m_pGames (0),
	m_nGames (0),
	m_nLoaded (-1),
	m_nShots (0),
	m_bGunAdjust (FALSE),
	m_nDsw (0xFFFF),
	m_nFilter (0),
	m_bHVS (TRUE),
	m_bMultiCore (TRUE),
	m_bFilterAllowed (TRUE),
	m_bVSync (TRUE),
	m_bStatusOn (FALSE),
	m_nClearStatus (0)
{
	s_pThis = this;
	m_LastRaw[0] = '\0';
}

CKernel::~CKernel (void)
{
	s_pThis = 0;
}

boolean CKernel::Initialize (void)
{
	m_bVideoOK = m_Video.Initialize ();

	m_Serial.Initialize (115200);

	return    m_Logger.Initialize (&m_LogSink)
	       && m_Interrupt.Initialize ()
	       && m_Timer.Initialize ();
}

TShutdownMode CKernel::Run (void)
{
	if (!m_bVideoOK)
	{
		Fatal (PROJECT_ERR_FB, "cannot set up the HDMI output");
	}

	char Line[180];
	Log ("s16-pi - bare metal SEGA System 16 emulator", COLOR_TITLE);
	snprintf (Line, sizeof Line, "Version %s, built %s %s", PROJECT_VERSION, __DATE__, __TIME__);
	Log (Line, COLOR_DIM);

	CMachineInfo *pInfo = CMachineInfo::Get ();
	m_CPUThrottle.SetSpeed (CPUSpeedMaximum);
	snprintf (Line, sizeof Line, "%s, %u MB RAM, CPU %u MHz, temperature %u C",
		  pInfo->GetMachineName (), pInfo->GetRAMSize (),
		  m_CPUThrottle.GetClockRate () / 1000000, m_CPUThrottle.GetTemperature ());
	Log (Line, COLOR_TEXT);
	snprintf (Line, sizeof Line, "HDMI %ux%u, pixel order %s", m_Video.GetWidth (), m_Video.GetHeight (),
		  m_Video.GetPixelOrder () ? "RGB" : "BGR");
	Log (Line, COLOR_TEXT);

	// SD card
	if (!m_EMMC.Initialize ())
	{
		Fatal (PROJECT_ERR_SD, "SD card not initialised");
	}
	if (f_mount (&m_FileSystem, "SD:", 1) != FR_OK)
	{
		Fatal (PROJECT_ERR_SD, "cannot read the SD card file system");
	}
	m_bSDOK = TRUE;
	f_mkdir (SAVE_DIR);
	f_mkdir (SHOT_DIR);
	FrameSetup (&m_Video);
	LoadConfig ();
	// the scanlines need a Pi 4, or a Pi 3 with core 1 drawing the lines
	m_bFilterAllowed = pInfo->GetModelMajor () >= 4 || m_bMultiCore;
	if (!m_bFilterAllowed)
	{
		m_nFilter = FILTER_NONE;
	}
	StartCores ();

	m_pUI = new CUI;

	// games
	ScanGames ();
	snprintf (Line, sizeof Line, "Games found: %u (of the %d sets s16-pi knows)", m_nGames, s16_num_games ());
	Log (Line, m_nGames ? COLOR_OK : COLOR_ERR);
	if (m_nGames == 0)
	{
		Fatal (PROJECT_ERR_GAME, "no System 16 or 18 game (MAME .zip set) in the s16pi folder");
	}

	// USB and audio
	Log ("USB...", COLOR_TEXT);
	if (!m_USBHCI.Initialize ())
	{
		Log ("  USB controller not initialised", COLOR_ERR);
	}
	m_pInput = new CInput (&m_USBHCI, &m_DeviceNameService);
	LoadPads ();
	m_pInput->UpdateDevices ();

	if (m_Audio.Initialize (&m_Interrupt))
	{
		Log ("HDMI audio: OK (48 kHz)", COLOR_OK);
	}
	else
	{
		Log ("HDMI audio: not available, playing without sound", COLOR_ERR);
	}

	unsigned nSel = LastPlayed ();

	FILINFO Info;
	if (f_stat (MCTEST_FLAG, &Info) == FR_OK)
	{
		McTest (nSel);
		CTimer::SimpleMsDelay (8000);
	}
	if (f_stat (BENCH_FLAG, &Info) == FR_OK)
	{
		if (LoadGame (nSel))
		{
			Benchmark ();
		}
		CTimer::SimpleMsDelay (8000);		// time to read the results on screen
	}
	SaveLog ();

	// from here on the screen belongs to the UI
	ApplyVideoMode ();
	FrameLinesToScreen (TRUE);
	SetupBeamRacing ();
	SaveLog ();
	m_bQuiet = TRUE;

	boolean bDirect = m_nGames == 1;
	while (1)
	{
		if (!bDirect)
		{
			nSel = RunList (nSel);
		}
		bDirect = FALSE;

		if (m_nLoaded != (int) nSel && !LoadGame (nSel))
		{
			m_pUI->DrawMessage ("CANNOT LOAD GAME", 0, "SEE s16pi/LOG.TXT");
			SaveLog ();
			for (unsigned i = 0; i < 180; i++)
			{
				ShowUIFrame ();
			}
			continue;
		}
		SaveLog ();
		RunGame ();
		m_bQuiet = TRUE;
	}

	return ShutdownHalt;
}

// cores 1 and 2, unless multicore=off; single core if core 1 does not answer
void CKernel::StartCores (void)
{
	if (!m_bMultiCore)
	{
		Log ("Multicore: off (multicore=off), single core", COLOR_TEXT);
		return;
	}
	CVideoCore *pVideoCore = new CVideoCore;
	boolean bStarted = pVideoCore && pVideoCore->Initialize ();
	unsigned nWait = CTimer::GetClockTicks ();
	while (bStarted && !McAlive () && CTimer::GetClockTicks () - nWait < 200000)
	{
	}
	if (bStarted && McAlive ())
	{
		Log ("Multicore: video lines drawn on core 1", COLOR_TEXT);
		while (!TvWorkerAlive () && CTimer::GetClockTicks () - nWait < 200000)
		{
		}
		Log (TvWorkerAlive () ? "Multicore: core 2 ready for COMPOSITE"
				      : "Multicore: core 2 not running, COMPOSITE on core 1",
		     TvWorkerAlive () ? COLOR_TEXT : COLOR_ERR);
	}
	else
	{
		Log (bStarted ? "Multicore: core 1 started but is not running, single core"
			      : "Multicore: core 1 did not start, single core", COLOR_ERR);
		m_bMultiCore = FALSE;
	}
	SaveLog ();
}

// the game played last (LAST.TXT), selected in the list
unsigned CKernel::LastPlayed (void)
{
	unsigned nSel = 0;
	FIL File;
	char Last[64];
	UINT nRead = 0;
	if (f_open (&File, LAST_PATH, FA_READ) == FR_OK)
	{
		f_read (&File, Last, sizeof Last - 1, &nRead);
		f_close (&File);
		Last[nRead] = '\0';
		for (unsigned i = 0; i < m_nGames; i++)
		{
			if (strcmp (m_pGames[i].File, Last) == 0)
			{
				nSel = i;
			}
		}
	}
	return nSel;
}

void CKernel::PowerOff (void)
{
	Log ("Power off: saving data and halting", COLOR_TITLE);
	SaveGameData ();
	SaveLog ();
	f_mount (0, "SD:", 0);
	m_bSDOK = FALSE;

	// black picture, then halt (the firmware stops the HDMI output)
	m_Video.Clear (0, RGB (0, 0, 0));
	m_Video.Clear (1, RGB (0, 0, 0));
	m_Video.ShowBuffer (0);
	CTimer::SimpleMsDelay (300);

	CBcmWatchdog Watchdog;
	Watchdog.Restart (CBcmWatchdog::PartitionHalt);
	while (1)
	{
	}
}
