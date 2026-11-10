/*
 * s16-pi - PLACEHOLDER for the System 16 core: the interface of s16_api.h
 * with no machine behind it yet. A "game" is a test card, drawn one line at
 * a time with the same hooks the real core will use, so the front-end (list,
 * menus, pads, multicore lines, beam racing) can be built and tried first.
 * No ROM is read. To be replaced by the System 16B machine.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "s16_api.h"
#include <string.h>

// sets the list recognises for now: MAME's names of some System 16B parents,
// titles only (the real table will be generated from MAME's segas16b.cpp)
static const struct { const char *pName, *pDesc; } s_Games[] =
{
	{"aliensyn", "Alien Syndrome"},
	{"altbeast", "Altered Beast"},
	{"aurail",   "Aurail"},
	{"bayroute", "Bay Route"},
	{"cotton",   "Cotton"},
	{"ddux",     "Dynamite Dux"},
	{"eswat",    "Cyber Police ESWAT"},
	{"fpoint",   "Flash Point"},
	{"goldnaxe", "Golden Axe"},
	{"hwchamp",  "Heavyweight Champ"},
	{"passsht",  "Passing Shot"},
	{"riotcity", "Riot City"},
	{"shinobi",  "Shinobi"},
	{"tetris",   "Tetris"},
	{"tturf",    "Tough Turf"},
	{"wb3",      "Wonder Boy III - Monster Lair"},
	{"wrestwar", "Wrestle War"},
};
#define NUM_GAMES	((int) (sizeof s_Games / sizeof s_Games[0]))

static bool s_bLoaded = false;
static TS16LineRegs s_Regs;
static TS16LineCB *s_pLineCB;		static void *s_pLineCtx;
static TS16LineHook *s_pHook;		static void *s_pHookCtx;
static TS16LineSink *s_pSink;		static void *s_pSinkCtx;
static void (*s_pSync) (void *);	static void *s_pSyncCtx;
static int s_nAudioFrames;

int s16_num_games (void)			{ return NUM_GAMES; }
int s16_find (const char *pName)
{
	for (int i = 0; i < NUM_GAMES; i++)
	{
		if (strcmp (s_Games[i].pName, pName) == 0) return i;
	}
	return -1;
}
static bool Valid (int nGame)			{ return nGame >= 0 && nGame < NUM_GAMES; }
const char *s16_game_name (int nGame)		{ return Valid (nGame) ? s_Games[nGame].pName : ""; }
const char *s16_game_parent (int)		{ return ""; }
const char *s16_game_desc (int nGame)		{ return Valid (nGame) ? s_Games[nGame].pDesc : ""; }
const char *s16_game_year (int)			{ return "19??"; }
const char *s16_game_maker (int)		{ return "Sega"; }
int s16_game_players (int)			{ return 2; }
int s16_game_buttons (int)			{ return 3; }

int s16_dip_count (int)				{ return 0; }
int s16_dip_get (int, int, TS16Dip *)		{ return 0; }
const char *s16_dip_setting_name (int, int, int, uint16_t *)	{ return ""; }
uint16_t s16_dsw_default (int)			{ return 0xFFFF; }

int s16_load (int nGame, TS16Fetch *, void *, char *pMissing, size_t nMissing)
{
	if (nMissing) pMissing[0] = '\0';
	s_bLoaded = Valid (nGame);
	s16_reset ();
	return s_bLoaded;
}
void s16_unload (void)				{ s_bLoaded = false; }
void s16_reset (void)				{ memset (&s_Regs, 0, sizeof s_Regs); s_nAudioFrames = 0; }

void s16_set_player (int nPlayer, uint8_t nBits)	{ if (nPlayer >= 0 && nPlayer < 4) s_Regs.Player[nPlayer] = nBits; }
void s16_set_system (uint8_t nBits)		{ s_Regs.nSystem = nBits; }
void s16_set_dsw (uint16_t)			{}

void s16_set_line_callback (TS16LineCB *pCB, void *pCtx)	{ s_pLineCB = pCB; s_pLineCtx = pCtx; }
void s16_set_line_hook (TS16LineHook *pHook, void *pCtx)	{ s_pHook = pHook; s_pHookCtx = pCtx; }
void s16_set_line_sink (TS16LineSink *pSink, void *pCtx)	{ s_pSink = pSink; s_pSinkCtx = pCtx; }
void s16_set_video_sync (void (*pSync) (void *), void *pCtx)	{ s_pSync = pSync; s_pSyncCtx = pCtx; }

// the test card: a frame, eight colour bars, a line that goes down one row
// per frame, and a square for each control held (four rows, one per player)
void s16_render_line (const TS16LineRegs *pRegs, int y, uint32_t *pOut)
{
	static const uint32_t Bars[8] = {0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000};
	const int nSweep = (int) (pRegs->nFrame % S16_HEIGHT);
	for (int x = 0; x < S16_WIDTH; x++)
	{
		uint32_t c = y < 112 ? Bars[x / 40] : 0x202020;
		if (x == 0 || y == 0 || x == S16_WIDTH - 1 || y == S16_HEIGHT - 1) c = 0xFFFFFF;
		if (y == nSweep) c = 0xFF8000;
		pOut[x] = c;
	}
	const int nRow = (y - 124) / 24;
	if (y >= 124 && nRow < 4 && (y - 124) % 24 < 16)
	{
		const unsigned nBits = pRegs->Player[nRow] | ((pRegs->nSystem >> nRow & 1) << 7)
				       | ((pRegs->nSystem >> (4 + nRow) & 1) << 8);
		for (int b = 0; b < 9; b++)
		{
			const uint32_t c = (nBits >> b & 1) ? 0x00E000 : 0x505050;
			for (int x = 20 + b * 32; x < 36 + b * 32; x++) pOut[x] = c;
		}
	}
}

void s16_run_frame (void)
{
	if (!s_bLoaded) return;
	for (int i = 0; i < S16_LINES; i++)
	{
		const int nLine = (S16_HEIGHT + i) % S16_LINES;	// 224..261, then 0..223
		if (s_pHook) s_pHook (s_pHookCtx, nLine);
		if (nLine >= S16_HEIGHT) continue;
		if (s_pSink)
		{
			s_pSink (s_pSinkCtx, nLine, &s_Regs);
		}
		else if (s_pLineCB)
		{
			uint32_t Line[S16_WIDTH];
			s16_render_line (&s_Regs, nLine, Line);
			s_pLineCB (s_pLineCtx, nLine, Line);
		}
	}
	s_Regs.nFrame++;
	s_nAudioFrames += 800;				// silence, 48 kHz at 60 Hz
}

int s16_audio_read (int16_t *pDst, int nMaxFrames)
{
	int n = s_nAudioFrames < nMaxFrames ? s_nAudioFrames : nMaxFrames;
	memset (pDst, 0, (size_t) n * 2 * sizeof (int16_t));
	s_nAudioFrames = 0;
	return n;
}

size_t s16_state_size (void)			{ return sizeof s_Regs.nFrame; }
int s16_state_save (void *pBuffer, size_t nSize)
{
	if (!s_bLoaded || nSize < sizeof s_Regs.nFrame) return 0;
	memcpy (pBuffer, &s_Regs.nFrame, sizeof s_Regs.nFrame);
	return (int) sizeof s_Regs.nFrame;
}
int s16_state_load (const void *pBuffer, size_t nSize)
{
	if (!s_bLoaded || nSize != sizeof s_Regs.nFrame) return 0;
	if (s_pSync) s_pSync (s_pSyncCtx);
	memcpy (&s_Regs.nFrame, pBuffer, sizeof s_Regs.nFrame);
	return 1;
}

size_t s16_hi_size (void)			{ return 0; }
int s16_hi_ready (void)				{ return 0; }
void s16_hi_get (uint8_t *)			{}
void s16_hi_put (const uint8_t *)		{}
