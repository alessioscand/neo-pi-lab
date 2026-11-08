/*
 * nes-pi - kernel: game list, play with USB pads and HDMI audio, in-game menu
 * with save states, automatic diagnostics in LOG.TXT
 *
 * The class is implemented by area: kernel.cpp (start-up, power off),
 * console.cpp, games.cpp, settings.cpp, menus.cpp, gameloop.cpp, bench.cpp,
 * report.cpp, racing.cpp.
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

#define NESPI_DIR	"SD:/nespi"
#define LOG_PATH	NESPI_DIR "/LOG.TXT"
#define BENCH_FLAG	NESPI_DIR "/BENCH"
#define BENCH_PATH	NESPI_DIR "/BENCH.TXT"
#define PROFILE_PATH	NESPI_DIR "/PROFILE.TXT"
#define LAST_PATH	NESPI_DIR "/LAST.TXT"
#define CFG_PATH	NESPI_DIR "/nespi.cfg"
#define PADS_PATH	NESPI_DIR "/pads.cfg"
#define PALETTE_PATH	NESPI_DIR "/palette.pal"
#define SAVE_DIR	NESPI_DIR "/saves"
#define SHOT_DIR	NESPI_DIR "/shots"

#ifndef BENCH_WARMUP
#define BENCH_WARMUP	1800		// 30 s of play before measuring
#endif
#ifndef BENCH_FRAMES
#define BENCH_FRAMES	3600		// then 60 s measured
#endif

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
	void GamePath (char *pOut, size_t nSize, const char *pExt) const;
	void StateExt (char *pExt, unsigned nSlot) const;
	boolean SaveState (const char *pPath);
	boolean LoadState (const char *pPath);
	static size_t BatteryLoad (void *pBuffer, size_t nMax);	// the core's battery RAM
	static void BatterySave (const void *pData, size_t nSize);

	// settings.cpp
	static const char *TvName (int nMode);
	static const char *PaletteName (int nPalette);
	static const char *FilterName (unsigned nFilter);
	static const char *FilterShort (unsigned nFilter);
	void ApplyPalette (void);
	void SetTvMode (void);
	void VideoModeTag (char *pOut, size_t nSize);
	void ApplyVideoMode (void);
	void LoadConfig (void);
	void SaveConfig (void);
	void LoadPads (void);
	void SavePads (void);

	// menus.cpp
	void SnapshotForUI (void);
	void ShowUIFrame (boolean bWait = TRUE, const char *pTopMessage = 0);
	unsigned RunList (unsigned nSel);
	void RunSystemMenu (void);
	int ChoosePad (void);
	boolean RunMapping (unsigned nSlot, boolean bOverGame);
	boolean PowerOffCountdown (void);
	void RunCredits (void);

	// gameloop.cpp
	void SetupCoreOnce (void);
	void RunGame (void);
	const char *SaveScreenshot (void);

	// bench.cpp
	void Benchmark (void);

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
	CVideoOut		m_Video;
	CAudioOut		m_Audio;
	CInput			*m_pInput;
	CUI			*m_pUI;

	enum { MaxLines = 400, MaxLineLen = 140, MaxGames = 256 };

	boolean			m_bVideoOK;
	boolean			m_bSDOK;
	boolean			m_bQuiet;	// UI/emulation running: console not drawn
	char			m_LastRaw[MaxLineLen];
	unsigned		m_nRepeat;

	// console lines
	char			m_Lines[MaxLines][MaxLineLen];
	u32			m_LineColor[MaxLines];
	unsigned		m_nLines;

	// emulator data
	u8			*m_pGame;
	size_t			 m_nGameSize;
	TGameEntry		*m_pGames;
	unsigned		 m_nGames;
	int			 m_nLoaded;	// index in m_pGames, -1 = none

	// frame pacing / statistics
	boolean			 m_bVSync;
	boolean			 m_bStatusOn;
	unsigned		 m_nClearStatus;
	unsigned		 m_nShots;

	// configuration (nespi.cfg)
	unsigned		 m_nFilter;	// FILTER_*
	boolean			 m_bHVS;		// FALSE: scaler=cpu in nespi.cfg
	boolean			 m_bFilterAllowed;	// Pi 4 only
	boolean			 m_bForceFilters;	// filters=on in nespi.cfg

	static CKernel		*s_pThis;
};

#endif
