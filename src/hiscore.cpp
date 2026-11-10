/*
 * s16-pi - hiscores, as MAME's hiscore plugin does them (hiscore.dat,
 * core/s16_hiscore.inc): once the game has set its table up in RAM, the table
 * of saves/<game>.hi is put over it; the table is written back when it changed
 * (in-game menu, game list, another game, RESET, power off). The .hi files are
 * MAME's own format.
 */
#include "kernel.h"
#include "mcline.h"
#include "s16_api.h"

enum { HI_NONE, HI_WAIT, HI_ACTIVE };
static unsigned s_nHiState = HI_NONE;
static size_t s_nHiSize = 0;
static u8 *s_pHiDefault = 0;		// the game's own table (power-on)
static u8 *s_pHiCard = 0;		// what saves/<game>.hi holds
static boolean s_bHiCard = FALSE;	// ... if there is one
static u8 *s_pHiNow = 0;

void CKernel::HiStart (void)
{
	HiStop ();
	s_nHiSize = s16_hi_size ();
	if (s_nHiSize == 0)
	{
		return;
	}
	s_pHiDefault = new u8[s_nHiSize];
	s_pHiCard = new u8[s_nHiSize];
	s_pHiNow = new u8[s_nHiSize];
	if (!s_pHiDefault || !s_pHiCard || !s_pHiNow)
	{
		HiStop ();
		return;
	}
	char Path[160];
	GamePath (Path, sizeof Path, "hi");
	FIL File;
	UINT nRead = 0;
	if (f_open (&File, Path, FA_READ) == FR_OK)
	{
		f_read (&File, s_pHiCard, s_nHiSize, &nRead);
		f_close (&File);
	}
	s_bHiCard = nRead == s_nHiSize;
	s_nHiState = HI_WAIT;
}

void CKernel::HiStop (void)
{
	delete [] s_pHiDefault;
	delete [] s_pHiCard;
	delete [] s_pHiNow;
	s_pHiDefault = s_pHiCard = s_pHiNow = 0;
	s_nHiSize = 0;
	s_bHiCard = FALSE;
	s_nHiState = HI_NONE;
}

// after each frame: put the saved table in as soon as the game has set its own up
void CKernel::HiTick (void)
{
	if (s_nHiState != HI_WAIT || !s16_hi_ready ())
	{
		return;
	}
	s16_hi_get (s_pHiDefault);
	if (s_bHiCard)
	{
		s16_hi_put (s_pHiCard);
	}
	else
	{
		memcpy (s_pHiCard, s_pHiDefault, s_nHiSize);
	}
	s_nHiState = HI_ACTIVE;
	Log (s_bHiCard ? "  hiscores: loaded from the card" : "  hiscores: none saved yet", COLOR_DIM);
}

// writes the table if it differs from the card's and from the game's own
void CKernel::HiSave (void)
{
	if (s_nHiState != HI_ACTIVE)
	{
		return;
	}
	McSync ();
	s16_hi_get (s_pHiNow);
	if (   memcmp (s_pHiNow, s_pHiCard, s_nHiSize) == 0
	    || memcmp (s_pHiNow, s_pHiDefault, s_nHiSize) == 0)
	{
		return;
	}
	char Path[160];
	GamePath (Path, sizeof Path, "hi");
	FIL File;
	UINT nWritten = 0;
	if (f_open (&File, Path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		f_write (&File, s_pHiNow, s_nHiSize, &nWritten);
		f_close (&File);
	}
	if (nWritten == s_nHiSize)
	{
		memcpy (s_pHiCard, s_pHiNow, s_nHiSize);
		s_bHiCard = TRUE;
		Log ("  hiscores: saved", COLOR_DIM);
	}
	else
	{
		Log ("  hiscores: NOT saved", COLOR_ERR);
	}
}
