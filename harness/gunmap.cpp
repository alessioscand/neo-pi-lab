/*
 * s16-pi - Laser Ghost: where the shots land, for a grid of aims (core/s16_guns.inc)
 *
 *   make gunmap; ./gunmap <rom folder>/lghost <gun 0-2> > map.txt
 *
 * Plays the gun adjustment, puts 3 players in, saves a state in the first
 * level; then for each aim loads it, fires and finds the shot's white ring 8
 * frames later. Lines: aim x, aim y, ring x, ring y, ring pixels (a ring is
 * 8-20; other counts mean something else was white). Grid: X0 X1 XS Y0 Y1 YS
 * in the environment. Built with -DGUN_RAW (make gunmap) it measures straight
 * aims, as core/s16_guns.inc wants; without, what is left after the correction.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "s16_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
static std::string s_Dir;
static uint32_t s_Frame[S16_WIDTH * S16_HEIGHT];
static int Fetch (void *, const char *pName, uint32_t, uint32_t nSize, uint8_t *pDst)
{
	FILE *f = fopen ((s_Dir + "/" + pName).c_str (), "rb"); if (!f) return 0;
	size_t n = fread (pDst, 1, nSize, f); fclose (f); return n == nSize;
}
static void Line (void *, int y, const uint32_t *p) { memcpy (s_Frame + y * S16_WIDTH, p, S16_WIDTH * 4); }
static int16_t Audio[8192];
static void Run (int n) { while (n--) { s16_run_frame (); s16_audio_read (Audio, 4096); } }
int main (int argc, char **argv)
{
	s_Dir = argv[1];
	int nGun = atoi (argv[2]);
	s16_set_line_callback (Line, 0);
	char Miss[200]; if (!s16_load (s16_find ("lghost"), Fetch, 0, Miss, sizeof Miss)) { printf ("load %s\n", Miss); return 1; }
	int Aim[3][2] = {{160,112},{160,112},{160,112}};
	int nEnd = argc > 3 ? atoi (argv[3]) : 2600;
	for (int f = 0; f < nEnd; f++)
	{
		uint8_t Fire[3] = {0,0,0};
		const bool bAdjust = s16_gun_adjust (f, Fire) != 0;
		if (!bAdjust)
		{
			for (int g = 0; g < 3; g++) { Aim[g][0] = 160; Aim[g][1] = 112; }
			if (f >= 1300 && f < 1306) Fire[0] = 0x10;
			if (f >= 1320 && f < 1326) Fire[1] = 0x10;
			if (f >= 1340 && f < 1346) Fire[2] = 0x10;
		}
		uint8_t sys = 0;
		for (int c = 0; c < 3; c++) if (f >= 1100 + 30 * c && f < 1108 + 30 * c) sys |= (uint8_t) (0x10 << c);
		s16_set_system (sys);
		for (int g = 0; g < 3; g++) { if (!bAdjust) s16_set_aim (g, Aim[g][0], Aim[g][1]); s16_set_player (g, Fire[g]); s16_set_crosshair (g, 0); }
		Run (1);
	}
	{ FILE *f = fopen ("/tmp/gm/at.ppm", "wb"); fprintf (f, "P6\n320 224\n255\n"); for (int i = 0; i < 320*224; i++) { uint8_t c[3] = {(uint8_t)(s_Frame[i]>>16),(uint8_t)(s_Frame[i]>>8),(uint8_t)s_Frame[i]}; fwrite (c,1,3,f);} fclose (f); }
	if (argc > 3) return 0;
	size_t nSize = s16_state_size (); void *pS = malloc (nSize);
	s16_state_save (pS, nSize);
	const int K = 8, LEAD = 12;
	std::vector<uint32_t> B (S16_WIDTH * S16_HEIGHT);
	s16_state_load (pS, nSize); Run (LEAD + K); memcpy (B.data (), s_Frame, sizeof s_Frame);
	auto Env = [] (const char *n, int d) { const char *e = getenv (n); return e ? atoi (e) : d; };
	const int X0 = Env ("X0", 10), X1 = Env ("X1", 310), XS = Env ("XS", 20);
	const int Y0 = Env ("Y0", 10), Y1 = Env ("Y1", 214), YS = Env ("YS", 17);
	for (int ay = Y0; ay <= Y1; ay += YS)
	for (int ax = X0; ax <= X1; ax += XS)
	{
		s16_state_load (pS, nSize);
		for (int k = 0; k < LEAD + K; k++)	// aim LEAD frames before the trigger
		{
			for (int g = 0; g < 3; g++) { s16_set_aim (g, g == nGun ? ax : Env ("OX", 160), g == nGun ? ay : Env ("OY", 112)); s16_set_player (g, g == nGun && k >= LEAD && k < LEAD + 4 ? 0x10 : 0); }
			Run (1);
		}
		auto White = [&] (int i) { uint32_t c = s_Frame[i]; return ((c >> 16) & 255) > 200 && ((c >> 8) & 255) > 200 && (c & 255) > 200 && c != B[i]; };
		double cx = ax, cy = ay; int n = 0;
		for (int pass = 0; pass < 3; pass++)
		{
			const int R = pass == 0 ? 50 : 12;
			double sx = 0, sy = 0; n = 0;
			for (int y = (int) cy - R; y <= (int) cy + R; y++)
				for (int x = (int) cx - R; x <= (int) cx + R; x++)
					if (x >= 0 && x < 320 && y >= 0 && y < 224 && White (y * 320 + x)) { sx += x; sy += y; n++; }
			if (!n) break;
			cx = sx / n; cy = sy / n;
		}
		if (n) printf ("%d %d %.1f %.1f %d\n", ax, ay, cx, cy, n); else printf ("%d %d - - 0\n", ax, ay);
		fflush (stdout);
	}
	return 0;
}
