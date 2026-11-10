/*
 * s16-pi - menus and screens (see ui.h)
 */
#include "fontdata.h"
#include "ui.h"
#include <circle/font.h>
#include <circle/util.h>
#include <circle/timer.h>

// "..." in one character (UI_ELLIPSIS), for names cut in the menus
static const u8 s_Ellipsis[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x92, 0x00};

#define ARGB(c)	(0xFF000000u | (c))
#define BUF_PX	(GAME_SRC_PITCH * GAME_SRC_LINES)

CUI::CUI (void)
{
	memset (m_Buf, 0, sizeof m_Buf);
	memset (m_Snapshot, 0, sizeof m_Snapshot);
	m_nClipX0 = 0;
	m_nClipX1 = UI_W;
}

void CUI::Pixel (int x, int y, u32 c)
{
	if (x >= m_nClipX0 && x < m_nClipX1 && y >= 0 && y < UI_H)
	{
		m_Buf[y * GAME_SRC_PITCH + x] = ARGB (c);
	}
}

void CUI::Fill (int x, int y, int w, int h, u32 c)
{
	for (int j = y; j < y + h; j++)
	{
		for (int i = x; i < x + w; i++)
		{
			Pixel (i, j, c);
		}
	}
}

// Background: a grid on petrol blue, a line every 16 pixels, closed on all
// four sides (the last column and row are 1 pixel narrower)
#define BG_CELL		16

// Screens without a game behind them (bFreeze: kept for the callers; the grid
// does not move)
void CUI::Background (boolean bFreeze)
{
	(void) bFreeze;
	for (int y = 0; y < GAME_H; y++)
	{
		u32 *p = &m_Buf[y * GAME_SRC_PITCH];
		for (int x = 0; x < GAME_W; x++)
		{
			boolean bLine = x % BG_CELL == 0 || y % BG_CELL == 0 || x == GAME_W - 1 || y == GAME_H - 1;
			p[x] = ARGB (bLine ? UI_GRID : UI_BG);
		}
	}
}

// PLACEHOLDER: the logo is still to be drawn; the name marks its place
void CUI::Logo (void)
{
	CText (14, "S16-PI", UI_WHITE, 2, TEXT_SHADOW);
}

// a dark panel with a white frame
void CUI::Panel (int x0, int y0, int x1, int y1)
{
	Fill (x0, y0, x1 - x0 + 1, y1 - y0 + 1, UI_FRAME);
	Fill (x0 + 1, y0 + 1, x1 - x0 - 1, y1 - y0 - 1, UI_LINE);
	Fill (x0 + 2, y0 + 2, x1 - x0 - 3, y1 - y0 - 3, UI_PANEL);
}

// Menu font (Matinee), capitals only; TEXT_LIST: the game list's font
// (Cyberwire), with its lower case; TEXT_SHADOW: a black shadow under it
void CUI::Text (int x, int y, const char *s, u32 c, unsigned nTall, unsigned nStyle)
{
	const boolean bListFont = (nStyle & TEXT_LIST) != 0;
	for (unsigned i = 0; s[i] != '\0'; i++)
	{
		unsigned ch = (unsigned char) s[i];
		const boolean bEllipsis = ch == (unsigned char) UI_ELLIPSIS[0];
		if (!bEllipsis && (ch < 32 || ch >= 128))
		{
			ch = '?';
		}
		if (!bListFont && ch >= 'a' && ch <= 'z')
		{
			ch -= 32;
		}
		for (int pass = (nStyle & TEXT_SHADOW) ? 0 : 1; pass < 2; pass++)	// shadow, then glyph
		{
			int dx = pass == 0 ? 1 : 0;
			u32 col = pass == 0 ? 0x000000 : c;
			for (unsigned r = 0; r < 8; r++)
			{
				u8 nRow = bEllipsis ? s_Ellipsis[r] : (bListFont ? g_ListFont[ch - 32][r] : g_MenuFont[ch - 32][r]);
				for (unsigned k = 0; k < 8; k++)
				{
					if (nRow & (0x80 >> k))
					{
						for (unsigned t = 0; t < nTall; t++)
						{
							Pixel (x + i * 8 + k + dx, y + r * nTall + t + dx, col);
						}
					}
				}
			}
		}
	}
}

void CUI::CText (int y, const char *s, u32 c, unsigned nTall, unsigned nStyle)
{
	Text ((UI_W - (int) strlen (s) * 8) / 2, y, s, c, nTall, nStyle);
}

// Buttons: 13 px disc in the button colour with a 5x7 letter drawn for it
// (exactly centred): A red, B yellow, C green, D blue, and S (Select) grey
static const u32 ButtonColor[5] = {0xDC2828, 0xF0C81E, 0x28B446, 0x325AE6, 0x6E6E78};
static const u32 ButtonInk[5]   = {0xFFFFFF, 0x281E00, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF};
static const char *ButtonGlyph[5][7] =
{
	{".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"},
	{"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."},
	{".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."},
	{"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."},
	{".####", "#....", "#....", ".###.", "....#", "....#", "####."}
};

static int ButtonIndex (char cButton)
{
	return cButton == 'S' ? 4 : (cButton >= 'A' && cButton <= 'D' ? cButton - 'A' : -1);
}

void CUI::ButtonIcon (int x, int y, char cButton)
{
	const int b = ButtonIndex (cButton);
	if (b < 0)
	{
		return;
	}
	// a 13 x 13 disc drawn by hand, with an even 1-pixel black rim
	static const char *const Disc[13] =
	{
		"....#####....",
		"..##ooooo##..",
		".#ooooooooo#.",
		".#ooooooooo#.",
		"#ooooooooooo#",
		"#ooooooooooo#",
		"#ooooooooooo#",
		"#ooooooooooo#",
		"#ooooooooooo#",
		".#ooooooooo#.",
		".#ooooooooo#.",
		"..##ooooo##..",
		"....#####....",
	};
	for (int j = 0; j < 13; j++)
	{
		for (int i = 0; i < 13; i++)
		{
			char c = Disc[j][i];
			if (c != '.')
			{
				Pixel (x + i, y + j, c == '#' ? 0x000000 : ButtonColor[b]);
			}
		}
	}
	for (int r = 0; r < 7; r++)
	{
		for (int k = 0; k < 5; k++)
		{
			if (ButtonGlyph[b][r][k] == '#')
			{
				Pixel (x + 4 + k, y + 3 + r, ButtonInk[b]);
			}
		}
	}
}

u32 CUI::ButtonColorOf (char cButton)
{
	const int b = ButtonIndex (cButton);
	return b >= 0 ? ButtonColor[b] : UI_WHITE;
}

u32 CUI::Dim (u32 c, unsigned nPercent)
{
	return ((((c >> 16) & 0xFF) * nPercent / 100) << 16)
	     | ((((c >> 8) & 0xFF) * nPercent / 100) << 8)
	     | (((c & 0xFF) * nPercent / 100));
}

// Hint bar: button followed by its action
static int HintIconWidth (char c)	{ (void) c; return 13; }

void CUI::Buttons (int y, const TButtonHint *pHints, unsigned nHints)
{
	const int nGap = 16;
	int nW = 0;
	for (unsigned i = 0; i < nHints; i++)
	{
		nW += HintIconWidth (pHints[i].cButton) + 4 + (int) strlen (pHints[i].pText) * 8 + (i ? nGap : 0);
	}
	int x = (UI_W - nW) / 2;
	for (unsigned i = 0; i < nHints; i++)
	{
		char c = pHints[i].cButton;
		ButtonIcon (x, y, c);
		int w = HintIconWidth (c);
		Text (x + w + 4, y + 3, pHints[i].pText, UI_WHITE, 1, TEXT_SHADOW);
		x += w + 4 + (int) strlen (pHints[i].pText) * 8 + nGap;
	}
}

// The paused game (darkened snapshot) or, with no game loaded, the pattern
void CUI::Backdrop (boolean bOverGame)
{
	if (bOverGame)
	{
		memcpy (m_Buf, m_Snapshot, sizeof m_Buf);
	}
	else
	{
		Background ();
	}
}

// a name longer than nMax characters: cut after the last whole word that
// fits, then the one-character ellipsis
static void CutAtWord (char *pOut, const char *pName, unsigned nMax)
{
	size_t nLen = strlen (pName);
	if (nLen <= nMax)
	{
		memcpy (pOut, pName, nLen + 1);
		return;
	}
	unsigned nCut = nMax - 1;
	while (nCut > 0 && pName[nCut] != ' ')
	{
		nCut--;
	}
	if (nCut == 0)
	{
		nCut = nMax - 1;		// one long word: cut inside it
	}
	while (nCut > 0 && strchr (" -,:", pName[nCut - 1]))
	{
		nCut--;
	}
	memcpy (pOut, pName, nCut);
	pOut[nCut] = UI_ELLIPSIS[0];
	pOut[nCut + 1] = '\0';
}

// how far a title nExtra pixels too long has scrolled after nTicks frames:
// a pause at the start, forward 1 px every 2 frames, a pause, then back
static int ScrollOffset (int nExtra, unsigned nTicks)
{
	const unsigned nPause = 90;
	const unsigned nRun = (unsigned) nExtra * 2;
	unsigned t = nTicks % (2 * (nPause + nRun));
	if (t < nPause)
	{
		return 0;
	}
	t -= nPause;
	if (t < nRun)
	{
		return (int) (t / 2);
	}
	t -= nRun;
	if (t < nPause)
	{
		return nExtra;
	}
	return nExtra - (int) ((t - nPause) / 2);
}

void CUI::DrawList (const TGameEntry *pGames, unsigned nCount, unsigned nSel, const char *pMessage,
		    boolean bOverGame, unsigned nTicks)
{
	Backdrop (bOverGame);
	Logo ();
	// the list: 4 titles above and below the selected one, 34 characters wide
	const int C = 108;				// centre of the band
	const int nChars = 34, nListW = nChars * 8, nListX = (UI_W - nListW) / 2;
	Panel (16, 44, UI_W - 17, 190);
	Fill (21, C - 11, UI_W - 42, 23, UI_BAND);

	// the titles fade away from the selected one
	static const u32 Fade[4] = {0x83B5C8, 0x6794A5, 0x4A7382, 0x395E6D};
	for (int k = -4; k <= 4; k++)
	{
		int i = (int) nSel + k;
		if (i < 0 || i >= (int) nCount)
		{
			continue;
		}
		const char *pName = pGames[i].Name;
		if (k == 0)
		{
			// the selected title: a long one scrolls inside the band
			int nExtra = (int) strlen (pName) * 8 - nListW;
			if (nExtra <= 0)
			{
				CText (C - 8, pName, UI_WHITE, 2, TEXT_LIST | TEXT_SHADOW);
			}
			else
			{
				m_nClipX0 = nListX;
				m_nClipX1 = nListX + nListW;
				Text (nListX - ScrollOffset (nExtra, nTicks), C - 8, pName, UI_WHITE, 2,
				      TEXT_LIST | TEXT_SHADOW);
				m_nClipX0 = 0;
				m_nClipX1 = UI_W;
			}
		}
		else
		{
			char Name[sizeof pGames[0].Name + 1];
			CutAtWord (Name, pName, (unsigned) nChars);
			int y = C + k * 11 + (k > 0 ? 8 : -8) - 4;
			CText (y, Name, Fade[(k < 0 ? -k : k) - 1], 1, TEXT_LIST);
		}
	}

	// bottom row: set and region of the selected game on the left, counter on the right
	Fill (21, 172, UI_W - 42, 1, UI_LINE);
	char Count[16];
	{
		unsigned a = nSel + 1, b = nCount;
		char *p = Count;
		if (a >= 100) *p++ = '0' + (a / 100) % 10;
		if (a >= 10) *p++ = '0' + (a / 10) % 10;
		*p++ = '0' + a % 10;
		*p++ = '/';
		if (b >= 100) *p++ = '0' + (b / 100) % 10;
		if (b >= 10) *p++ = '0' + (b / 10) % 10;
		*p++ = '0' + b % 10;
		*p = '\0';
	}
	Text (UI_W - 23 - (int) strlen (Count) * 8, 177, Count, UI_VALUE, 1, TEXT_LIST);
	if (pMessage && pMessage[0])
	{
		Text (22, 177, pMessage, UI_ERR);
	}
	else if (nCount)
	{
		char Info[40];
		strncpy (Info, pGames[nSel].Info, sizeof Info - 1);
		Info[sizeof Info - 1] = '\0';
		size_t nRoom = (size_t) ((UI_W - 46 - (int) strlen (Count) * 8) / 8) - 1;
		if (strlen (Info) > nRoom) Info[nRoom] = '\0';
		Text (22, 177, Info, UI_VALUE, 1, TEXT_LIST);
	}
	static const TButtonHint Hints[2] = {{'A', "START GAME"}, {'S', "OPTIONS"}};
	Buttons (200, Hints, 2);
}

void CUI::SnapshotGame (const u32 *pGameBuf)
{
	for (unsigned i = 0; i < BUF_PX; i++)
	{
		u32 p = pGameBuf[i];
		u32 r = ((p >> 16) & 0xFF) * 30 / 100;
		u32 g = ((p >> 8) & 0xFF) * 30 / 100;
		u32 b = (p & 0xFF) * 30 / 100;
		m_Snapshot[i] = ARGB ((r << 16) | (g << 8) | b);
	}
}

void CUI::DrawPanelMenu (boolean bOverGame, const char *pTitle,
			 const TMenuItem *pItems, unsigned nItems, unsigned nSel,
			 const char *pMessage, u32 nMessageColor,
			 const TButtonHint *pHints, unsigned nHints, const char *pFooter,
			 boolean bWide)
{
	if (bOverGame)
	{
		memcpy (m_Buf, m_Snapshot, sizeof m_Buf);
	}
	else
	{
		Background ();
	}

	const int nStep = nItems > 8 ? 12 : 15;		// long lists (controls) a bit tighter
	int nInner = 22 + (int) nItems * nStep + 22;		// title + items + message line
	int y0 = (190 - nInner) / 2 + 4;
	if (y0 < 4) y0 = 4;
	// DIP SWITCHES uses the width of the game list: MAME's names fit in full
	int x0 = bWide ? 16 : 52, x1 = bWide ? UI_W - 17 : UI_W - 53;
	Panel (x0, y0, x1, y0 + nInner + 8);

	// 25 characters fit inside the frame (200 px); a longer name is cut at a
	// word, within 24 characters, and ends with the one-character ellipsis
	char Title[34];
	strncpy (Title, pTitle, 33);
	Title[33] = '\0';
	if (strlen (Title) > 25)
	{
		int nCut = 24;
		for (int i = 24; i > 8; i--)
		{
			if (Title[i] == ' ')
			{
				nCut = i;
				break;
			}
		}
		while (nCut > 0 && (Title[nCut - 1] == ' ' || Title[nCut - 1] == '-' || Title[nCut - 1] == ':'))
		{
			nCut--;
		}
		Title[nCut] = UI_ELLIPSIS[0];
		Title[nCut + 1] = '\0';
	}
	CText (y0 + 10, Title, UI_TITLE);
	Fill (x0 + 6, y0 + 22, x1 - x0 - 11, 1, UI_LINE);

	for (unsigned n = 0; n < nItems; n++)
	{
		int y = y0 + 32 + (int) n * nStep;
		boolean bSel = n == nSel;
		if (bSel)
		{
			Fill (x0 + 6, y - 3, x1 - x0 - 11, 13, UI_BAND);
			for (int t = 0; t < 4; t++)		// cursor triangle
			{
				Fill (x0 + 12 + t, y + t, 1, 8 - 2 * t, UI_WHITE);
			}
		}
		u32 nColor = pItems[n].bDisabled ? Dim (UI_DIM, 55) : (bSel ? UI_WHITE : UI_DIM);
		if (!pItems[n].bDisabled && !bSel && strcmp (pItems[n].pLabel, "POWER OFF") == 0)
		{
			nColor = UI_POWER;		// red, unless on the band
		}
		const unsigned nStyle = bSel ? TEXT_SHADOW : 0;
		Text (x0 + 22, y, pItems[n].pLabel, nColor, 1, nStyle);
		if (pItems[n].pValue)
		{
			int nLen = (int) strlen (pItems[n].pValue);
			u32 nValColor = pItems[n].bDisabled ? Dim (UI_DIM, 45) : (bSel ? UI_WHITE : UI_VALUE);
			Text (x1 - 10 - nLen * 8, y, pItems[n].pValue, nValColor, 1, nStyle);
		}
	}

	if (pMessage && pMessage[0])
	{
		Text (x0 + 22, y0 + 32 + (int) nItems * nStep + 4, pMessage, nMessageColor);
	}

	if (pFooter && pFooter[0])
	{
		char Footer[40];
		strncpy (Footer, pFooter, 37);
		Footer[37] = '\0';
		CText (y0 + nInner + 14, Footer, UI_DIM, 1, TEXT_SHADOW);	// below the panel
	}

	Buttons (209, pHints, nHints);
}

void CUI::DrawBox (const char *pLine1, const char *pLine2, const TButtonHint *pHint)
{
	// darken what is underneath, so the box stands out from the menu
	for (unsigned i = 0; i < BUF_PX; i++)
	{
		m_Buf[i] = ARGB (Dim (m_Buf[i] & 0xFFFFFF, 40));
	}
	Panel (64, 80, UI_W - 65, 150);
	if (pLine2 && pLine2[0])
	{
		if (pLine1) CText (92, pLine1, UI_WHITE);
		CText (110, pLine2, UI_DIM);
		if (pHint) Buttons (126, pHint, 1);
	}
	else
	{
		// one line and the button: centred in the box
		if (pLine1) CText (pHint ? 99 : 111, pLine1, UI_WHITE);
		if (pHint) Buttons (117, pHint, 1);
	}
}

void CUI::DrawBlack (void)
{
	for (unsigned i = 0; i < BUF_PX; i++)
	{
		m_Buf[i] = 0xFF000000u;
	}
}

void CUI::DrawLoading (const char *pName, unsigned nPercent)
{
	Background (TRUE);

	// long titles go on two lines of up to 26 characters, split at a space
	const unsigned nMax = 26;
	char Line1[34], Line2[34];
	size_t nLen = strlen (pName);
	Line2[0] = '\0';
	if (nLen <= nMax)
	{
		strncpy (Line1, pName, sizeof Line1 - 1);
		Line1[sizeof Line1 - 1] = '\0';
	}
	else
	{
		unsigned nCut = nMax;
		while (nCut > 0 && pName[nCut] != ' ')
		{
			nCut--;
		}
		if (nCut == 0)
		{
			nCut = nMax;			// no space: split inside the word
		}
		memcpy (Line1, pName, nCut);
		Line1[nCut] = '\0';
		const char *pRest = pName + nCut;
		while (*pRest == ' ')
		{
			pRest++;
		}
		strncpy (Line2, pRest, nMax);
		Line2[nMax] = '\0';
	}

	int dy = Line2[0] ? 6 : 0;			// the box grows by 6 lines up and down
	Panel (32, 84 - dy, UI_W - 33, 150 + dy);
	CText (98 - dy, "LOADING", UI_TITLE);
	CText (112 - dy, Line1, UI_WHITE);
	if (Line2[0])
	{
		CText (124 - dy, Line2, UI_WHITE);
	}
	int nBarW = UI_W - 96;
	int yBar = 130 + dy;
	Fill (48, yBar, nBarW, 8, UI_LINE);
	Fill (49, yBar + 1, nBarW - 2, 6, UI_PANEL);
	Fill (49, yBar + 1, (nBarW - 2) * (int) (nPercent > 100 ? 100 : nPercent) / 100, 6, UI_TITLE);
}

void CUI::DrawMessage (const char *pTitle, const char *pLine1, const char *pLine2)
{
	Background ();
	Panel (16, 70, UI_W - 17, 160);

	// the lines centred vertically in the panel (inside 74-156): 22 px from
	// the title to the first line below it, 16 px between the other two
	const int nY0 = 70, nY1 = 160, nGlyph = 8;
	int nH = nGlyph;
	if (pLine1) nH += 22;
	if (pLine2) nH += pLine1 ? 16 : 22;
	int y = (nY0 + nY1) / 2 - nH / 2;
	CText (y, pTitle, UI_ERR);
	y += 22;
	if (pLine1)
	{
		CText (y, pLine1, UI_WHITE);
		y += 16;
	}
	if (pLine2) CText (y, pLine2, UI_DIM);
}

// ---- controls set-up -------------------------------------------------------

void CUI::DrawMapping (boolean bOverGame, const char *pTitle, int nStep,
		       const char *const *pValues, const char *pFooter)
{
	Backdrop (bOverGame);

	const int x0 = 52, x1 = UI_W - 53;
	Panel (x0, 20, x1, 188);
	CText (30, pTitle, UI_TITLE);
	Fill (x0 + 6, 42, x1 - x0 - 11, 1, UI_LINE);

	// nStep is the slot being asked for, in the order of the logical bits
	// (A B C D START SELECT), 6 = done; rows and steps: A B C D SELECT START
	static const char *Names[6] = {"A", "B", "C", "D", "START", "SELECT"};
	static const char cButton[6] = {'A', 'B', 'C', 'D', 0, 0};
	static const int StepOfRow[6] = {0, 1, 2, 3, 5, 4};
	if (nStep >= 0 && nStep < 6)
	{
		const char *pName = Names[nStep];
		int nLen = 6 + (int) strlen (pName);
		int x = (UI_W - nLen * 8) / 2;
		Text (x, 52, "PRESS ", UI_WHITE, 2);
		Text (x + 48, 52, pName, cButton[nStep] ? ButtonColorOf (cButton[nStep]) : UI_VALUE, 2);
	}
	else
	{
		CText (52, "SAVED", UI_OK, 2);
	}

	for (int r = 0; r < 6; r++)
	{
		int y = 80 + r * 14;
		int i = StepOfRow[r];
		const boolean bSel = i == nStep;
		if (bSel)
		{
			Fill (x0 + 6, y - 1, x1 - x0 - 11, 15, UI_BAND);
		}
		if (cButton[i])
		{
			ButtonIcon (x0 + 20, y, cButton[i]);
		}
		else
		{
			Text (x0 + 20, y + 3, i == 4 ? "START" : "SELECT / COIN", bSel ? UI_WHITE : UI_DIM,
			      1, bSel ? TEXT_SHADOW : 0);
		}
		const char *pValue = bSel ? "?" : pValues[i];
		if (pValue && pValue[0])
		{
			Text (x1 - 10 - (int) strlen (pValue) * 8, y + 3, pValue, bSel ? UI_WHITE : UI_VALUE,
			      1, bSel ? TEXT_SHADOW : 0);
		}
	}

	if (pFooter && pFooter[0])
	{
		CText (172, pFooter, Dim (UI_DIM, 60));
	}
}

// ---- credits -----------------------------------------------------------------

void CUI::DrawCredits (const char *pVersion)
{
	memcpy (m_Buf, m_Snapshot, sizeof m_Buf);

	const int x0 = 26, x1 = UI_W - 27, y0 = 6;
	Panel (x0, y0, x1, y0 + 187);
	CText (y0 + 10, "CREDITS", UI_TITLE);
	Fill (x0 + 6, y0 + 22, x1 - x0 - 11, 1, UI_LINE);

	char Title[32] = "S16-PI ";
	strncat (Title, pVersion, sizeof Title - 8);
	CText (y0 + 28, Title, UI_WHITE, 2, TEXT_LIST);
	CText (y0 + 48, "BY A.SCANDEREBECH", UI_VALUE);
	Fill (x0 + 6, y0 + 62, x1 - x0 - 11, 1, UI_LINE);

	// PLACEHOLDER: the core's authors come with the core
	static const char *Rows[4][2] =
	{
		{"ZIP",    "MINIZ - R.GELDREICH"},
		{"SYSTEM", "CIRCLE - R.STANGE"},
		{"NTSC",   "BLARGG - S.GREEN"},
		{"FONT",   "ZX ORIGINS - D.GUARD"}
	};
	for (int i = 0; i < 4; i++)
	{
		int y = y0 + 68 + i * 10;
		Text (x0 + 12, y, Rows[i][0], Dim (UI_DIM, 80));
		Text (x0 + 12 + 7 * 8, y, Rows[i][1], 0xEBEBEB);
	}
	Fill (x0 + 6, y0 + 157, x1 - x0 - 11, 1, UI_LINE);
	CText (y0 + 162, "SEGA IS A TRADEMARK", Dim (UI_DIM, 70));
	CText (y0 + 173, "OF ITS OWNER.", Dim (UI_DIM, 70));

	static const TButtonHint Close = {'B', "CLOSE"};
	Buttons (201, &Close, 1);
}
