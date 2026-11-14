/*
 * s16-pi - kernel: game list, play with USB pads and HDMI audio, in-game menu
 * with save states and DIP switches, automatic diagnostics in LOG.TXT
 *
 * The class is implemented by area: kernel.cpp (start-up, power off),
 * console.cpp, games.cpp, hiscore.cpp, settings.cpp, menus.cpp, gameloop.cpp,
 * bench.cpp, report.cpp, racing.cpp.
 */
#ifndef KERNEL_H
#define KERNEL_H

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/cputhrottle.h>
#include <circle/device.h>
#include <circle/usb/usbhcidevice.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <circle/types.h>
#include <circle/util.h>
#include <stdint.h>
#include "video.h"
#include "input.h"
#include "audio.h"
#include "ui.h"
#include "project.h"

extern "C" {
int snprintf (char *pBuffer, size_t nSize, const char *pFormat, ...);
}

#define S16PI_DIR	"SD:/s16pi"
#define LOG_PATH	S16PI_DIR "/LOG.TXT"
#define BENCH_FLAG	S16PI_DIR "/BENCH"
#define BENCH_PATH	S16PI_DIR "/BENCH.TXT"
#define PROFILE_PATH	S16PI_DIR "/PROFILE.TXT"
#define MCTEST_FLAG	S16PI_DIR "/MCTEST"
#define MCTEST_PATH	S16PI_DIR "/MCTEST.TXT"
#define LAST_PATH	S16PI_DIR "/LAST.TXT"
#define CFG_PATH	S16PI_DIR "/s16pi.cfg"
#define PADS_PATH	S16PI_DIR "/pads.cfg"
#define SAVE_DIR	S16PI_DIR "/saves"
#define SHOT_DIR	S16PI_DIR "/shots"

#define SAMPLE_RATE	48000
#define BENCH_WARMUP	1800		// 30 s of play before measuring
#define BENCH_FRAMES	3600		// then 60 s measured
#define MCTEST_FRAMES	3600

#define COLOR_TEXT	RGB (220, 220, 220)
#define COLOR_TITLE	RGB (255, 200, 0)
#define COLOR_OK	RGB (80, 230, 80)
#define COLOR_ERR	RGB (255, 70, 70)
#define COLOR_DIM	RGB (140, 140, 140)

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

// Receives Circle's own log messages and forwards them to the console
class CLogSink : public CDevice
{
public:
	int Write (const void *pBuffer, size_t nCount);
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);
	TShutdownMode Run (void);

	// console (screen during boot + serial + LOG.TXT)
	void Log (const char *pLine, u32 nColor);
	void Fatal (int nCode, const char *pMessage);
	void SaveLog (void);

	static CKernel *Get (void) { return s_pThis; }

private:
	// kernel.cpp
	void StartCores (void);
	unsigned LastPlayed (void);
	void PowerOff (void);

	// console.cpp
	void RedrawConsole (void);
	void WriteTextFile (const char *pPath, const char *pText);

	// games.cpp
	u8 *LoadFile (const char *pPath, size_t *pSize, int nHeap, const char *pShowName);
	void ScanGames (void);
	boolean LoadGame (unsigned nIndex);
	void UnloadGame (void);
	void SaveGameData (void);
	void GamePath (char *pOut, size_t nSize, const char *pExt) const;
	void StatePath (char *pOut, size_t nSize) const;
	static int FetchROM (void *pCtx, const char *pName, uint32_t nCRC, uint32_t nSize, uint8_t *pDst);
	static boolean IsHiddenDip (const char *pName);
	void LoadDips (void);
	void SaveDips (void);

	// hiscore.cpp
	void HiStart (void);
	void HiStop (void);
	void HiTick (void);
	void HiSave (void);
	void NvStart (void);
	void NvSave (void);

	// settings.cpp
	static const char *FilterName (unsigned nFilter);
	static const char *FilterShort (unsigned nFilter);
	void VideoModeTag (char *pOut, size_t nSize);
	void ApplyVideoMode (void);
	void LoadConfig (void);
	void SaveConfig (void);
	void LoadPads (void);
	void SavePads (void);

	// menus.cpp
	void ShowUIFrame (boolean bWait = TRUE, const char *pTopMessage = 0);
	unsigned RunList (unsigned nSel);
	void RunSystemMenu (void);
	void RunDipMenu (void);
	int ChoosePad (void);
	boolean RunMapping (unsigned nSlot, boolean bOverGame);
	boolean PowerOffCountdown (void);
	void RunCredits (void);

	// gameloop.cpp
	struct TPlayInput
	{
		boolean bOpenMenu, bOpenCredits, bScreenshot;
	};
	void RunGame (void);
	void PlayStart (void);
	void PlayWaitRelease (void);
	void PlayReadPanels (const u32 Pad[4], TPlayInput *pIn);
	void PlayGuns (const u32 Pad[4], u8 Player[4], boolean bPaused);
	void PlayOpenMenu (void);
	unsigned PlayFrame (boolean bRace, boolean bScreenshot, unsigned *pT0, boolean *pOddFrame);
	boolean PlayMenu (const u32 Pad[4]);
	void PlayDrawBands (void);
	void PlayStatusLine (unsigned nNow, unsigned *pNextDeadline);
	const char *SaveScreenshot (void);

	// bench.cpp
	void Benchmark (void);
	void McTest (unsigned nSel);

	// report.cpp, racing.cpp
	void ResetGameStats (void);
	void ReportWindow (boolean bFinal);
	void SetupBeamRacing (void);

private:
	// do not change this order
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CSerialDevice		m_Serial;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogSink		m_LogSink;
	CLogger			m_Logger;
	CCPUThrottle		m_CPUThrottle;
	CUSBHCIDevice		m_USBHCI;
	CEMMCDevice		m_EMMC;
	FATFS			m_FileSystem;
	CVideoOut		m_Video;	// the screen (HDMI): used through CScreen only
	CAudioOut		m_Audio;
	CInput			*m_pInput;
	CUI			*m_pUI;

	enum { MaxLines = 400, MaxLineLen = 140, MaxGames = 256 };

	boolean			m_bVideoOK;
	boolean			m_bSDOK;
	boolean			m_bQuiet;	// UI/emulation running: console not drawn

	// console lines
	char			m_Lines[MaxLines][MaxLineLen];
	u32			m_LineColor[MaxLines];
	unsigned		m_nLines;
	char			m_LastRaw[MaxLineLen];
	unsigned		m_nRepeat;

	// games
	u8			*m_pZip;	// while loading: the game's zip
	size_t			 m_nZipSize;
	u8			*m_pParentZip;	// and its parent's (clones)
	size_t			 m_nParentZipSize;
	TGameEntry		*m_pGames;
	unsigned		 m_nGames;
	int			 m_nLoaded;	// index in m_pGames, -1 = none
	unsigned		 m_nShots;	// last screenshot number used
	boolean			 m_bGunAdjust;	// Laser Ghost: no battery RAM saved, adjust the guns
	u16			 m_nDsw;	// DIP switches of the loaded game (saves/<game>.dip)

	// configuration (s16pi.cfg)
	unsigned		 m_nFilter;	// FILTER_*
	boolean			 m_bHVS;	// FALSE: scaler=cpu
	boolean			 m_bMultiCore;	// FALSE: multicore=off, or core 1 did not start
	boolean			 m_bFilterAllowed;

	// frame pacing and the status line
	boolean			 m_bVSync;	// FALSE: the firmware's vsync does not block
	boolean			 m_bStatusOn;
	unsigned		 m_nClearStatus;

	// RunGame
	struct TPlay
	{
		unsigned nPlayers;

		boolean	 bMenu;			// in-game menu open
		unsigned nMenuSel;
		u32	 nMenuPrev;
		boolean	 bSaved;		// a save state exists (one per game)
		char	 MenuMsg[40];
		u32	 nMenuMsgColor;

		// per player
		u32	 nPrev[4];		// ~0: wait until released
		boolean	 bSelectCombo[4];	// Select used with another button
		unsigned nCoin[4];		// frames left of a coin press
		unsigned nSelectHeld[4];	// frames Select has been held alone
		boolean	 bLazy[4];		// lazy finger (turbo on A-F), Select+B
		unsigned nLazyCount[4][6];	// frames each of A-F has been held

		// Laser Ghost: where each gun aims (pixels x 16), how long the stick
		// has been held (it speeds up), the gun adjustment (frames, -1: none)
		int	 nAimX[3], nAimY[3];
		unsigned nAimHeld[3];
		int	 nGunAdjust;

		// message in the top band, in frames
		char	 Toast[64];
		char	 ToastSide[32];		// the same, one word per line (side columns)
		unsigned nToast, nClearToast;

		// status line (Select+C), refreshed once per second
		char	 StFps[16], StLoad[8], StTemp[8], StRes[8], StScale[16], StDelay[8];
		u32	 nFpsColor, nLoadColor, nTempColor;
		boolean	 bStatusReady;
		unsigned nFrames, nSeconds, nTemp, nWinStart, nWorkMax;
	};
	TPlay			 m_Play;

	static CKernel		*s_pThis;
};

#endif
