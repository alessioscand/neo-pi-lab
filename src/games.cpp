/*
 * s16-pi - games: the list on the card, loading a set (with its parent),
 * its save data and DIP switches
 */
#include "kernel.h"
#include "mcline.h"
#include "s16_api.h"
#include <circle/new.h>

extern "C" int s16pi_zip_fetch (const void *pZip, size_t nZipSize, const char *pName, uint32_t nCRC,
				uint32_t nSize, uint8_t *pDst);

u8 *CKernel::LoadFile (const char *pPath, size_t *pSize, int nHeap, const char *pShowName)
{
	FIL File;
	if (f_open (&File, pPath, FA_READ) != FR_OK)
	{
		return 0;
	}

	size_t nSize = f_size (&File);
	u8 *pBuffer = new (nHeap) u8[nSize + 16];
	if (pBuffer == 0)
	{
		f_close (&File);
		char Msg[160];
		snprintf (Msg, sizeof Msg, "not enough memory for %s (%u MB)",
			  pPath, (unsigned) (nSize >> 20));
		Log (Msg, COLOR_ERR);
		return 0;
	}

	unsigned nStart = CTimer::GetClockTicks ();
	size_t nDone = 0;
	const size_t nChunk = 1 << 20;
	while (nDone < nSize)
	{
		size_t nWant = nSize - nDone < nChunk ? nSize - nDone : nChunk;
		UINT nRead = 0;
		if (   f_read (&File, pBuffer + nDone, nWant, &nRead) != FR_OK
		    || nRead == 0)
		{
			break;
		}
		nDone += nRead;
		if (pShowName != 0 && m_pUI != 0)
		{
			m_pUI->DrawLoading (pShowName, (unsigned) ((u64) nDone * 100 / nSize));
			ShowUIFrame (FALSE);
		}
	}
	f_close (&File);

	if (nDone != nSize)
	{
		delete [] pBuffer;
		return 0;
	}

	unsigned nMs = (CTimer::GetClockTicks () - nStart) / 1000;
	char Msg[180];
	unsigned nSpeed10 = nMs ? (unsigned) ((u64) nSize * 10 * 1000 / nMs / (1024 * 1024)) : 0;
	snprintf (Msg, sizeof Msg, "  read %s: %u KB in %u ms (%u.%u MB/s)", pPath + 4,
		  (unsigned) (nSize >> 10), nMs, nSpeed10 / 10, nSpeed10 % 10);
	Log (Msg, COLOR_OK);

	*pSize = nSize;
	return pBuffer;
}

static boolean HasZipExtension (const char *pName)
{
	size_t n = strlen (pName);
	if (n < 5)
	{
		return FALSE;
	}
	const char *e = pName + n - 4;
	return    e[0] == '.'
	       && (e[1] == 'z' || e[1] == 'Z')
	       && (e[2] == 'i' || e[2] == 'I')
	       && (e[3] == 'p' || e[3] == 'P');
}

// "goldnaxe.zip" -> "goldnaxe" (MAME set names are lower case)
static void ZipBase (const char *pFile, char *pOut, size_t nSize)
{
	size_t n = strlen (pFile);
	if (n > 4) n -= 4;
	if (n >= nSize) n = nSize - 1;
	for (size_t i = 0; i < n; i++)
	{
		char c = pFile[i];
		pOut[i] = c >= 'A' && c <= 'Z' ? (char) (c + 32) : c;
	}
	pOut[n] = '\0';
}


// s16pi/list.txt: "set=Title" or "set=Title|Region" per line. Fills pTitle
// and pRegion (both may stay empty) for one set; FALSE if the set is not listed
static boolean ListLookup (const char *pList, const char *pSet, char *pTitle, size_t nTitle,
			   char *pRegion, size_t nRegion)
{
	size_t nSet = strlen (pSet);
	for (const char *p = pList; p && *p; )
	{
		const char *pEnd = strchr (p, '\n');
		size_t nLine = pEnd ? (size_t) (pEnd - p) : strlen (p);
		if (nLine > nSet && strncmp (p, pSet, nSet) == 0 && p[nSet] == '=')
		{
			const char *t = p + nSet + 1, *e = p + nLine;
			while (e > t && (e[-1] == '\r' || e[-1] == ' ')) e--;
			const char *bar = t;
			while (bar < e && *bar != '|') bar++;
			size_t n = (size_t) (bar - t) < nTitle - 1 ? (size_t) (bar - t) : nTitle - 1;
			memcpy (pTitle, t, n);
			pTitle[n] = '\0';
			pRegion[0] = '\0';
			if (bar < e)
			{
				n = (size_t) (e - bar - 1) < nRegion - 1 ? (size_t) (e - bar - 1) : nRegion - 1;
				memcpy (pRegion, bar + 1, n);
				pRegion[n] = '\0';
			}
			return TRUE;
		}
		p = pEnd ? pEnd + 1 : 0;
	}
	return FALSE;
}

// MAME's name split in title and region: "Hook (Japan)" -> "Hook", "JAPAN";
// the region loses the commas
static void SplitMameName (const char *pDesc, char *pTitle, size_t nTitle, char *pRegion, size_t nRegion)
{
	const char *pPar = 0;				// the last '('
	for (const char *q = pDesc; *q; q++)
	{
		if (*q == '(') pPar = q;
	}
	size_t n = pPar ? (size_t) (pPar - pDesc) : strlen (pDesc);
	while (n > 0 && pDesc[n - 1] == ' ') n--;
	if (n > nTitle - 1) n = nTitle - 1;
	memcpy (pTitle, pDesc, n);
	pTitle[n] = '\0';
	pRegion[0] = '\0';
	if (pPar)
	{
		char Tmp[64];
		strncpy (Tmp, pPar + 1, sizeof Tmp - 1);
		Tmp[sizeof Tmp - 1] = '\0';
		char *pClose = strchr (Tmp, ')');
		if (pClose) *pClose = '\0';
		static const char *Drop[] = {","};
		for (const char *d : Drop)
		{
			char *q;
			while ((q = strstr (Tmp, d)) != 0)
			{
				memmove (q, q + strlen (d), strlen (q + strlen (d)) + 1);
			}
		}
		strncpy (pRegion, Tmp, nRegion - 1);
		pRegion[nRegion - 1] = '\0';
	}
}

void CKernel::ScanGames (void)
{
	// names for the list (optional)
	static char ListTxt[8192];
	ListTxt[0] = '\0';
	{
		FIL File;
		UINT nRead = 0;
		if (f_open (&File, S16PI_DIR "/list.txt", FA_READ) == FR_OK)
		{
			f_read (&File, ListTxt, sizeof ListTxt - 1, &nRead);
			f_close (&File);
			ListTxt[nRead] = '\0';
		}
	}

	m_pGames = new TGameEntry[MaxGames];
	m_nGames = 0;

	DIR Dir;
	FILINFO Info;
	if (f_opendir (&Dir, S16PI_DIR) != FR_OK)
	{
		return;
	}
	while (   m_nGames < MaxGames
	       && f_readdir (&Dir, &Info) == FR_OK
	       && Info.fname[0] != '\0')
	{
		if ((Info.fattrib & AM_DIR) || !HasZipExtension (Info.fname)
		    || strlen (Info.fname) >= sizeof m_pGames[0].File)
		{
			continue;
		}
		char Base[64];
		ZipBase (Info.fname, Base, sizeof Base);
		int nGame = s16_find (Base);
		if (nGame < 0)
		{
			continue;			// not a set the core knows
		}
		// a clone needs its parent's zip
		const char *pParent = s16_game_parent (nGame);
		if (pParent[0])
		{
			char Path[128];
			snprintf (Path, sizeof Path, "%s/%s.zip", S16PI_DIR, pParent);
			FILINFO PInfo;
			if (f_stat (Path, &PInfo) != FR_OK && s16_find (pParent) >= 0)
			{
				char Msg[160];
				snprintf (Msg, sizeof Msg, "  %s: needs %s.zip too, skipped", Info.fname, pParent);
				Log (Msg, COLOR_ERR);
				continue;
			}
		}

		TGameEntry &G = m_pGames[m_nGames];
		strcpy (G.File, Info.fname);
		char Title[64], Region[32], MameTitle[64], MameRegion[32];
		SplitMameName (s16_game_desc (nGame), MameTitle, sizeof MameTitle, MameRegion, sizeof MameRegion);
		if (ListLookup (ListTxt, Base, Title, sizeof Title, Region, sizeof Region))
		{
			if (!Region[0]) strcpy (Region, MameRegion);
		}
		else
		{
			strncpy (Title, s16_game_desc (nGame), sizeof Title - 1);	// MAME's full name
			Title[sizeof Title - 1] = '\0';
			strcpy (Region, MameRegion);
		}
		strncpy (G.Name, Title, sizeof G.Name - 1);
		G.Name[sizeof G.Name - 1] = '\0';
		for (char *q = Region; *q; q++)
		{
			if (*q >= 'a' && *q <= 'z') *q = (char) (*q - 32);
		}
		snprintf (G.Info, sizeof G.Info, "%s  %s", Base, Region);	// "goldnaxe  WORLD"
		G.nGame = nGame;
		m_nGames++;
	}
	f_closedir (&Dir);

	// sort by name
	for (unsigned i = 1; i < m_nGames; i++)
	{
		TGameEntry t = m_pGames[i];
		int j = (int) i - 1;
		while (j >= 0 && strcmp (m_pGames[j].Name, t.Name) > 0)
		{
			m_pGames[j+1] = m_pGames[j];
			j--;
		}
		m_pGames[j+1] = t;
	}
}

void CKernel::GamePath (char *pOut, size_t nSize, const char *pExt) const
{
	snprintf (pOut, nSize, "%s/%s.%s", SAVE_DIR, s16_game_name (m_pGames[m_nLoaded].nGame), pExt);
}

void CKernel::StatePath (char *pOut, size_t nSize) const
{
	GamePath (pOut, nSize, "st1");
}

// save data of the loaded game: the hiscores, the battery RAM
void CKernel::SaveGameData (void)
{
	if (m_nLoaded < 0)
	{
		return;
	}
	HiSave ();
	NvSave ();
}

void CKernel::UnloadGame (void)
{
	if (m_nLoaded < 0)
	{
		return;
	}
	SaveGameData ();
	HiStop ();
	McSync ();
	s16_unload ();
	m_nLoaded = -1;
}

// ROMs are looked up as MAME does: by CRC, then by name, in the game's zip
// and then in its parent's
int CKernel::FetchROM (void *pCtx, const char *pName, uint32_t nCRC, uint32_t nSize, uint8_t *pDst)
{
	CKernel *pThis = (CKernel *) pCtx;
	if (s16pi_zip_fetch (pThis->m_pZip, pThis->m_nZipSize, pName, nCRC, nSize, pDst))
	{
		return 1;
	}
	return pThis->m_pParentZip
	       && s16pi_zip_fetch (pThis->m_pParentZip, pThis->m_nParentZipSize, pName, nCRC, nSize, pDst);
}

// switches left out of the DIP SWITCHES menu; Flip Screen and Service Mode are
// put back to MAME's default when a .dip is loaded, so a file saved before they
// were hidden cannot keep a game in service mode
boolean CKernel::IsHiddenDip (const char *pName)
{
	return    strcmp (pName, "Unknown") == 0
	       || strcmp (pName, "Flip Screen") == 0		// not emulated
	       || strcmp (pName, "Service Mode") == 0;	// the test menu, for operators
}

// DIP switches: saves/<game>.dip ("dsw=FFBE": SW2 SW1), otherwise MAME's defaults
void CKernel::LoadDips (void)
{
	int nGame = m_pGames[m_nLoaded].nGame;
	m_nDsw = s16_dsw_default (nGame);
	char Path[160], Text[64];
	GamePath (Path, sizeof Path, "dip");
	FIL File;
	UINT nRead = 0;
	if (f_open (&File, Path, FA_READ) == FR_OK)
	{
		f_read (&File, Text, sizeof Text - 1, &nRead);
		f_close (&File);
		Text[nRead] = '\0';
		const char *p = strstr (Text, "dsw=");
		if (p)
		{
			m_nDsw = (u16) strtoul (p + 4, 0, 16);
		}
	}
	const int nCount = s16_dip_count (nGame);
	for (int d = 0; d < nCount; d++)
	{
		TS16Dip D;
		s16_dip_get (nGame, d, &D);
		if (!IsHiddenDip (D.pName) || strcmp (D.pName, "Unknown") == 0)
		{
			continue;			// the unknown ones keep what the file says
		}
		m_nDsw = (u16) ((m_nDsw & ~D.nMask) | (D.nDefault & D.nMask));
	}
	s16_set_dsw (m_nDsw);
}

void CKernel::SaveDips (void)
{
	if (m_nLoaded < 0)
	{
		return;
	}
	char Path[160], Text[64];
	GamePath (Path, sizeof Path, "dip");
	snprintf (Text, sizeof Text, "dsw=%04X\r\n", m_nDsw);
	WriteTextFile (Path, Text);
}

boolean CKernel::LoadGame (unsigned nIndex)
{
	UnloadGame ();

	const TGameEntry &G = m_pGames[nIndex];
	const int nGame = G.nGame;
	char Path[128], Line[180];
	snprintf (Path, sizeof Path, "%s/%s", S16PI_DIR, G.File);
	snprintf (Line, sizeof Line, "Game: %s (%s)", G.Name, G.File);
	Log (Line, COLOR_TITLE);

	m_pZip = LoadFile (Path, &m_nZipSize, HEAP_ANY, m_bQuiet ? G.Name : 0);
	if (m_pZip == 0)
	{
		Log ("  read failed", COLOR_ERR);
		return FALSE;
	}
	const char *pParent = s16_game_parent (nGame);
	if (pParent[0])
	{
		snprintf (Path, sizeof Path, "%s/%s.zip", S16PI_DIR, pParent);
		m_pParentZip = LoadFile (Path, &m_nParentZipSize, HEAP_ANY, m_bQuiet ? G.Name : 0);
	}

	McSync ();
	char Missing[64] = "";
	unsigned t0 = CTimer::GetClockTicks ();
	boolean bOK = s16_load (nGame, FetchROM, this, Missing, sizeof Missing) != 0;
	delete [] m_pZip;
	m_pZip = 0;
	delete [] m_pParentZip;
	m_pParentZip = 0;
	if (!bOK)
	{
		if (Missing[0])
		{
			snprintf (Line, sizeof Line, "  ROM %s missing or wrong size (in %s%s%s)", Missing,
				  G.File, pParent[0] ? " or " : "", pParent[0] ? pParent : "");
		}
		else
		{
			snprintf (Line, sizeof Line, "  not enough memory");
		}
		Log (Line, COLOR_ERR);
		return FALSE;
	}
	snprintf (Line, sizeof Line, "  %s %s, %u players, %u buttons; ROMs ready in %u ms",
		  s16_game_maker (nGame), s16_game_year (nGame), s16_game_players (nGame),
		  s16_game_buttons (nGame), (CTimer::GetClockTicks () - t0) / 1000);
	Log (Line, COLOR_DIM);
	m_nLoaded = (int) nIndex;
	m_nShots = 0;					// screenshot numbers: next free one

	// the core is new: its line hooks and DIP switches
	McEnable (m_bMultiCore);
	LoadDips ();
	s16_reset ();
	HiStart ();
	NvStart ();

	WriteTextFile (LAST_PATH, G.File);
	return TRUE;
}
