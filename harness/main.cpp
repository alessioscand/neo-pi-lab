/*
 * s16-pi - Linux test harness: runs the core without the Pi
 *
 *   s16test <rom folder> <set> [-f frames] [-s every] [-o dir] [-coin frame]
 *
 * <rom folder>/<set>/ holds the set's files, unzipped. -s N: a PPM screenshot
 * every N frames (and always <set>_last.ppm); -coin F: coin at frame F, start
 * one second later.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "s16_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>


static std::string s_Dir;
static uint32_t s_Frame[S16_WIDTH * S16_HEIGHT];

static int Fetch (void *, const char *pName, uint32_t, uint32_t nSize, uint8_t *pDst)
{
	const std::string Path = s_Dir + "/" + pName;
	FILE *f = fopen (Path.c_str (), "rb");
	if (!f) return 0;
	const size_t n = fread (pDst, 1, nSize, f);
	fclose (f);
	return n == nSize;
}

static void Line (void *, int y, const uint32_t *pRGB)
{
	memcpy (s_Frame + y * S16_WIDTH, pRGB, S16_WIDTH * 4);
}

static void SavePPM (const std::string &Path)
{
	FILE *f = fopen (Path.c_str (), "wb");
	if (!f) return;
	fprintf (f, "P6\n%d %d\n255\n", S16_WIDTH, S16_HEIGHT);
	for (int i = 0; i < S16_WIDTH * S16_HEIGHT; i++)
	{
		const uint8_t c[3] = {(uint8_t) (s_Frame[i] >> 16), (uint8_t) (s_Frame[i] >> 8), (uint8_t) s_Frame[i]};
		fwrite (c, 1, 3, f);
	}
	fclose (f);
}

int main (int argc, char **argv)
{
	if (argc < 3)
	{
		fprintf (stderr, "usage: s16test <rom folder> <set> [-f frames] [-s every] [-o dir] [-coin frame]\n");
		return 1;
	}
	const char *pSet = argv[2];
	int nFrames = 600, nEvery = 0, nCoin = -1;
	std::string Out = ".";
	for (int i = 3; i < argc; i++)
	{
		if (!strcmp (argv[i], "-f") && i + 1 < argc)		nFrames = atoi (argv[++i]);
		else if (!strcmp (argv[i], "-s") && i + 1 < argc)	nEvery = atoi (argv[++i]);
		else if (!strcmp (argv[i], "-o") && i + 1 < argc)	Out = argv[++i];
		else if (!strcmp (argv[i], "-coin") && i + 1 < argc)	nCoin = atoi (argv[++i]);
	}
	const int nGame = s16_find (pSet);
	if (nGame < 0)
	{
		fprintf (stderr, "%s: not a set the core knows\n", pSet);
		return 1;
	}
	s_Dir = std::string (argv[1]) + "/" + pSet;
	char Missing[64];
	if (!s16_load (nGame, Fetch, 0, Missing, sizeof Missing))
	{
		fprintf (stderr, "cannot load %s (missing: %s)\n", pSet, Missing);
		return 1;
	}
	printf ("%s: %s\n", pSet, s16_game_desc (nGame));
	s16_set_line_callback (Line, 0);
	static int16_t Audio[8192];
	for (int f = 0; f < nFrames; f++)
	{
		uint8_t nSystem = 0;
		if (nCoin >= 0 && f >= nCoin && f < nCoin + 8)			nSystem |= 0x10;
		if (nCoin >= 0 && f >= nCoin + 60 && f < nCoin + 68)		nSystem |= 0x01;
		s16_set_system (nSystem);
		s16_run_frame ();
		s16_audio_read (Audio, 4096);
		if (nEvery && (f + 1) % nEvery == 0)
		{
			char Name[64];
			snprintf (Name, sizeof Name, "/%s_%05d.ppm", pSet, f + 1);
			SavePPM (Out + Name);
		}
		if (getenv ("TRACE") && f % 30 == 0)
		{
			printf ("frame %d: 68000 pc %06x, mcu pc %04x\n", f, s16_main_pc (), s16_mcu_pc ());
		}
	}
	SavePPM (Out + "/" + pSet + "_last.ppm");
	return 0;
}
