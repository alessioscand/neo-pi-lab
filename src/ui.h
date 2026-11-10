/*
 * s16-pi - menus and screens, drawn in a buffer of the game's size and format
 * (320x224, XRGB8888 with R high), so they go through the same scaling and
 * filters as the game.
 */
#ifndef S16PI_UI_H
#define S16PI_UI_H

#include <circle/types.h>
#include "video.h"

#define UI_W	GAME_W		// 320
#define UI_H	GAME_H		// 224
#define UI_ELLIPSIS	"\x1F"	// "..." in one character (Text, CText)

// Palette (0xRRGGBB): petrol blue grid, dark panels with a white frame, a
// crimson band on the selected line
#define UI_BG		0x05566Fu
#define UI_GRID		0x0E6882u
#define UI_FRAME	0xFFFFFFu
#define UI_LINE		0x033C50u	// inside the frame, and the separators
#define UI_PANEL	0x021E2Au
#define UI_BAND		0xC81C37u
#define UI_TITLE	0xFFFFFFu
#define UI_VALUE	0x6EAAC3u
#define UI_DIM		0x83B5C8u
#define UI_WHITE	0xFFFFFFu
#define UI_POWER	0xE8213Fu	// POWER OFF
#define UI_OK		0x6EBE6Eu
#define UI_ERR		0xFF6E6Eu
#define UI_WARN		0xF0C828u
#define UI_GREY		0xA0A0AAu

// Text styles
#define TEXT_LIST	1u		// the game list's font, with lower case
#define TEXT_SHADOW	2u		// black shadow: over the grid, the game or the band

struct TMenuItem
{
	const char	*pLabel;
	const char	*pValue;	// 0 = none
	boolean		 bDisabled;
};

struct TButtonHint
{
	char		 cButton;	// 'A' 'B' 'C' 'D'
	const char	*pText;
};

struct TGameEntry
{
	char	Name[64];	// title shown in the list (a long one scrolls)
	char	File[64];	// zip file name in the s16pi folder
	int	nGame;		// index in the core's game table
	char	Info[40];	// "goldnaxe  WORLD": set and region, under the list
};

class CUI
{
public:
	CUI (void);

	u32 *GetBuffer (void)	{ return m_Buf; }

	// bOverGame: a game is loaded, show it paused behind the list (SnapshotGame);
	// nTicks = frames since the selection changed (scrolls a long title)
	void DrawList (const TGameEntry *pGames, unsigned nCount, unsigned nSel, const char *pMessage,
		       boolean bOverGame, unsigned nTicks);
	// Menu over a darkened copy of the game frame (call SnapshotGame when it opens)
	void SnapshotGame (const u32 *pGameBuf);
	// Generic panel menu: bOverGame = draw over the snapshot, else over the pattern
	void DrawPanelMenu (boolean bOverGame, const char *pTitle,
			    const TMenuItem *pItems, unsigned nItems, unsigned nSel,
			    const char *pMessage, u32 nMessageColor,
			    const TButtonHint *pHints, unsigned nHints, const char *pFooter,
			    boolean bWide = FALSE);
	// Small box drawn over the current buffer contents
	void DrawBox (const char *pLine1, const char *pLine2, const TButtonHint *pHint);
	// Controls set-up: nStep 0-5 = A B C D START SELECT being asked, 6 = saved
	void DrawMapping (boolean bOverGame, const char *pTitle, int nStep,
			  const char *const *pValues, const char *pFooter);
	void DrawLoading (const char *pName, unsigned nPercent);
	// Credits over the darkened game (call SnapshotGame first)
	void DrawCredits (const char *pVersion);
	void DrawMessage (const char *pTitle, const char *pLine1, const char *pLine2);
	void DrawBlack (void);

	static u32 Dim (u32 c, unsigned nPercent);

private:
	void Pixel (int x, int y, u32 c);
	void Fill (int x, int y, int w, int h, u32 c);
	void Background (boolean bFreeze = FALSE);
	void Backdrop (boolean bOverGame);
	void Logo (void);
	void Panel (int x0, int y0, int x1, int y1);
	void Text (int x, int y, const char *s, u32 c, unsigned nTall = 1, unsigned nStyle = 0);
	void CText (int y, const char *s, u32 c, unsigned nTall = 1, unsigned nStyle = 0);
	void Buttons (int y, const TButtonHint *pHints, unsigned nHints);
	void ButtonIcon (int x, int y, char cButton);
	static u32 ButtonColorOf (char cButton);

private:
	u32	m_Buf[GAME_SRC_PITCH * GAME_SRC_LINES] __attribute__ ((aligned (64)));
	u32	m_Snapshot[GAME_SRC_PITCH * GAME_SRC_LINES];	// the whole paused picture
	int	m_nClipX0, m_nClipX1;	// Pixel draws only inside (a scrolling title)
};

#endif
