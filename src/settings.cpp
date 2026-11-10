/*
 * s16-pi - settings: s16pi.cfg, the video mode they ask for, pads.cfg
 */
#include "kernel.h"
#include "frame.h"
#include "pacing.h"
#include "racing.h"
#include "tvcore.h"
#include <circle/usb/usbhcidevice.h>

const char *CKernel::FilterShort (unsigned nFilter)
{
	switch (nFilter)
	{
	case FILTER_SCAN_LIGHT:	 return "SCAN-L";
	case FILTER_SCAN_MEDIUM: return "SCAN-M";
	case FILTER_SCAN_STRONG: return "SCAN-S";
	case FILTER_COMPOSITE:	 return "COMP";
	default:		 return "NOFLT";
	}
}

const char *CKernel::FilterName (unsigned nFilter)
{
	switch (nFilter)
	{
	case FILTER_SCAN_LIGHT:	 return "SCANLINES_LIGHT";
	case FILTER_SCAN_MEDIUM: return "SCANLINES";
	case FILTER_SCAN_STRONG: return "SCANLINES_STRONG";
	case FILTER_COMPOSITE:	 return "COMPOSITE";
	default:		 return "NONE";
	}
}

// "HVS4", "HVS2" or "CPU": how the picture is being enlarged right now
void CKernel::VideoModeTag (char *pOut, size_t nSize)
{
	if (m_Video.GetHVSFactor () > 1)
	{
		snprintf (pOut, nSize, "HVS%u", m_Video.GetHVSFactor ());
	}
	else
	{
		snprintf (pOut, nSize, "CPU");
	}
}

void CKernel::ApplyVideoMode (void)
{
	if (m_nFilter == FILTER_COMPOSITE && !TvReady ())
	{
		// the filter's tables, built the first time COMPOSITE is used
		unsigned t0 = CTimer::GetClockTicks ();
		if (FrameTvInit ())
		{
			char Msg[100];
			unsigned nMs = (CTimer::GetClockTicks () - t0) / 1000;
			snprintf (Msg, sizeof Msg, "COMPOSITE: filter tables ready in %u ms, %s", nMs,
				  TvWorkerAlive () ? "lines filtered on core 2" : "no core 2: lines filtered by the drawing core");
			Log (Msg, TvWorkerAlive () ? COLOR_TEXT : COLOR_ERR);
		}
		else
		{
			Log ("COMPOSITE: not enough memory for the filter, scanlines instead", COLOR_ERR);
			m_nFilter = FILTER_SCAN_MEDIUM;
		}
	}
	FrameSetComposite (m_nFilter == FILTER_COMPOSITE);
	if (!m_Video.SetGameMode (m_nFilter, m_bHVS))
	{
		Fatal (PROJECT_ERR_FB, "cannot set up the game framebuffer");
	}
	char Tag[8];
	VideoModeTag (Tag, sizeof Tag);
	char Line[MaxLineLen];
	snprintf (Line, sizeof Line, "Video: mode %s, HDMI %ux%u, framebuffer %ux%u, scale %ux (HVS %ux, CPU %ux), filter %s%s",
		  Tag, m_Video.GetWidth (), m_Video.GetHeight (),
		  m_Video.GetFBWidth (), m_Video.GetFBHeight (), m_Video.GetScale (),
		  m_Video.GetHVSFactor (), m_Video.GetScale () / m_Video.GetHVSFactor (),
		  FilterName (m_Video.GetFilter ()), m_bHVS ? "" : " (scaler=cpu)");
	Log (Line, COLOR_TEXT);
	snprintf (Line, sizeof Line, "Video: pixel order %s (%u), red in the %s byte%s",
		  m_Video.GetPixelOrder () ? "RGB" : "BGR", m_Video.GetPixelOrder (),
		  m_Video.IsRedLow () ? "low" : "high", "");
	Log (Line, COLOR_TEXT);
	snprintf (Line, sizeof Line, "Video: framebuffer write-combining %s (%u pages of 64 KB)",
		  m_Video.GetWCPages () ? "on" : "FAILED",
		  m_Video.GetWCPages ());
	Log (Line, COLOR_TEXT);
}

// usbpoll= value for the log and s16pi.cfg
static const char *UsbPollName (void)
{
	switch (g_nUSBInterruptPollMs)
	{
	case 0:		return "native";
	case 2:		return "500";
	case 4:		return "250";
	default:	return "1000";
	}
}

// lines starting with '#' are comments: blanked, so their options do not count
static void BlankComments (char *pText)
{
	boolean bLineStart = TRUE, bComment = FALSE;
	for (char *p = pText; *p; p++)
	{
		if (*p == '\n')
		{
			bLineStart = TRUE;
			bComment = FALSE;
			continue;
		}
		if (bLineStart && *p != ' ' && *p != '\t')
		{
			bComment = *p == '#';
			bLineStart = FALSE;
		}
		if (bComment)
		{
			*p = ' ';
		}
	}
}

void CKernel::LoadConfig (void)
{
	FIL File;
	char Text[1024];
	UINT nRead = 0;
	if (f_open (&File, CFG_PATH, FA_READ) != FR_OK)
	{
		return;
	}
	f_read (&File, Text, sizeof Text - 1, &nRead);
	f_close (&File);
	Text[nRead] = '\0';
	BlankComments (Text);

	if (strstr (Text, "filter=COMPOSITE"))			m_nFilter = FILTER_COMPOSITE;
	else if (strstr (Text, "filter=SCANLINES_LIGHT"))	m_nFilter = FILTER_SCAN_LIGHT;
	else if (strstr (Text, "filter=SCANLINES_STRONG"))	m_nFilter = FILTER_SCAN_STRONG;
	else if (strstr (Text, "filter=SCANLINES"))		m_nFilter = FILTER_SCAN_MEDIUM;
	if (strstr (Text, "scaler=cpu"))	m_bHVS = FALSE;
	// (multicore and the scanlines on a Pi 3: decided after LoadConfig, also
	// when s16pi.cfg does not exist yet)
	// USB pads are polled every 1 ms (like usbhid.jspoll=1 on Linux);
	// usbpoll=500 / 250 = every 2 / 4 ms, for pads that misbehave at 1000 Hz;
	// usbpoll=native keeps the interval requested by the device
	if (strstr (Text, "usbpoll=native"))	g_nUSBInterruptPollMs = 0;
	else if (strstr (Text, "usbpoll=500"))	g_nUSBInterruptPollMs = 2;
	else if (strstr (Text, "usbpoll=250"))	g_nUSBInterruptPollMs = 4;
	if (strstr (Text, "multicore=off"))	m_bMultiCore = FALSE;
	// input lag (see pacing.h)
	if (strstr (Text, "beamracing=off"))		g_Race.nCfg = 0;
	if (strstr (Text, "beamracing=on"))		g_Race.nCfg = 1;
	const char *pDelay = strstr (Text, "framedelay=");
	if (pDelay)
	{
		pDelay += 11;
		if (strncmp (pDelay, "off", 3) == 0)
		{
			g_Delay.nCfg = 0;
		}
		else if (*pDelay >= '0' && *pDelay <= '9')
		{
			int n = 0;
			while (*pDelay >= '0' && *pDelay <= '9' && n < 100)
			{
				n = n * 10 + (*pDelay++ - '0');
			}
			g_Delay.nCfg = n > 12 ? 12 : n;
		}
	}

	char Line[140];
	snprintf (Line, sizeof Line, "Settings: filter %s, USB polling %s%s",
		  FilterName (m_nFilter),
		  UsbPollName (), g_nUSBInterruptPollMs ? " Hz" : "");
	Log (Line, COLOR_TEXT);
	char Delay[16];
	if (g_Delay.nCfg < 0)		snprintf (Delay, sizeof Delay, "auto");
	else if (g_Delay.nCfg == 0)	snprintf (Delay, sizeof Delay, "off");
	else				snprintf (Delay, sizeof Delay, "%d ms", g_Delay.nCfg);
	snprintf (Line, sizeof Line, "Settings: frame starts at line %u, frame delay %s, beam racing %s",
		  PHASE_LINE, Delay,
		  g_Race.nCfg ? "on" : "off");
	Log (Line, COLOR_TEXT);
}

void CKernel::SaveConfig (void)
{
	char Text[200];
	snprintf (Text, sizeof Text, "filter=%s\r\n%s",
		  FilterName (m_nFilter), m_bHVS ? "" : "scaler=cpu\r\n");
	if (g_nUSBInterruptPollMs != 1)
	{
		strcat (Text, "usbpoll=");
		strcat (Text, UsbPollName ());
		strcat (Text, "\r\n");
	}
	if (!m_bMultiCore)
	{
		strcat (Text, "multicore=off\r\n");
	}
	if (g_Race.nCfg == 0)
	{
		strcat (Text, "beamracing=off\r\n");
	}
	else if (g_Race.nCfg == 1)
	{
		strcat (Text, "beamracing=on\r\n");
	}
	if (g_Delay.nCfg >= 0)
	{
		char Line[24];
		if (g_Delay.nCfg == 0)	snprintf (Line, sizeof Line, "framedelay=off\r\n");
		else			snprintf (Line, sizeof Line, "framedelay=%d\r\n", g_Delay.nCfg);
		strcat (Text, Line);
	}
	WriteTextFile (CFG_PATH, Text);
}

void CKernel::LoadPads (void)
{
	static char Text[4096];
	FIL File;
	UINT nRead = 0;
	if (f_open (&File, PADS_PATH, FA_READ) == FR_OK)
	{
		f_read (&File, Text, sizeof Text - 1, &nRead);
		f_close (&File);
	}
	Text[nRead] = '\0';
	m_pInput->LoadMaps (Text);
}

void CKernel::SavePads (void)
{
	static char Text[4096];
	m_pInput->FormatMaps (Text, sizeof Text);
	WriteTextFile (PADS_PATH, Text);
}
