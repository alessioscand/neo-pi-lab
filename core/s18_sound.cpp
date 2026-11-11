/*
 * s16-pi - System 18 sound: two YM3438 (ymfm, Aaron Giles) and the RF5C68
 * PCM chip with its 64 KB of wave RAM, into one speaker, as MAME mixes them
 * (YM3438 0.40 each, RF5C68 1.0).
 *
 * RF5C68: as MAME's rf5c68.cpp (Olivier Galibert, Aaron Giles),
 * re-implemented here.
 *
 * Time is counted in Z80 cycles (8 MHz, the YM3438 clock too). The output
 * is 45784 samples per second of the game: 800 for each of its frames (57.23
 * a second), so that the 60 frames a second the Pi shows make 48 kHz.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "s16.h"
#include "ymfm_opn.h"
#include <string.h>
#include <vector>

// rates: YM3438 8 MHz / 144, RF5C68 10 MHz / 384, output 45784 Hz; per Z80 cycle (8 MHz)
#define OUT_RATE	45784
#define OUT_SAMPLES(c)	((c) * 5723 / 1000000)
#define YM_POS(n)	((n) * 500000)		// in 1/412056 of a YM3438 sample
#define YM_DIV		412056
#define RF_POS(n)	((n) * 78125)		// in 1/137352 of an RF5C68 sample
#define RF_DIV		137352

static void (*s_pIrq) (bool);			// first YM3438 -> Z80 IRQ

static u64 s_nCycles;
static u64 s_nOutDone, s_nYmDone, s_nRfDone;
static s32 s_nYmPrev, s_nYmCur, s_nRfPrev, s_nRfCur;

enum { FIFO = 8192 };
static s16 s_Fifo[FIFO];
static unsigned s_nFifoWr, s_nFifoRd;

// ---- YM3438 --------------------------------------------------------------------

struct TYmTimes { u64 Timer[2], nBusyEnd; };
static TYmTimes s_YmTimes[2];

class TOpn : public ymfm::ymfm_interface
{
public:
	TOpn (int n) : m_Chip (*this), m_n (n) {}
	ymfm::ym3438 m_Chip;
	int m_n;

	void ymfm_set_timer (uint32_t nTimer, int32_t nClocks) override
	{
		s_YmTimes[m_n].Timer[nTimer] = nClocks >= 0 ? s_nCycles + (u64) nClocks + 1 : 0;
	}
	void ymfm_set_busy_end (uint32_t nClocks) override	{ s_YmTimes[m_n].nBusyEnd = s_nCycles + nClocks; }
	bool ymfm_is_busy (void) override			{ return s_nCycles < s_YmTimes[m_n].nBusyEnd; }
	void ymfm_update_irq (bool bAsserted) override		{ if (m_n == 0 && s_pIrq) s_pIrq (bAsserted); }
	void Expired (unsigned nTimer)				{ m_engine->engine_timer_expired (nTimer); }
};
static TOpn *s_pYm[2];

// ---- RF5C68 ------------------------------------------------------------------------

static struct TRf
{
	struct { u8 bEnable, nEnv, nPan, nStart; u32 nAddr; u16 nStep, nLoop; } Chan[8];
	u8 nCBank;
	u16 nWBank;
	u8 bEnable;
	u8 Ram[0x10000];
} R;

static s32 RfNext (void)
{
	if (!R.bEnable)
	{
		return 0;
	}
	s32 l = 0, r = 0;
	for (auto &C : R.Chan)
	{
		if (!C.bEnable)
		{
			continue;
		}
		const int lv = (C.nPan & 0x0f) * C.nEnv, rv = ((C.nPan >> 4) & 0x0f) * C.nEnv;
		int s = R.Ram[(C.nAddr >> 11) & 0xffff];
		if (s == 0xff)
		{
			C.nAddr = (u32) C.nLoop << 11;
			s = R.Ram[(C.nAddr >> 11) & 0xffff];
			if (s == 0xff)
			{
				continue;		// looping to a loop point: silent
			}
		}
		C.nAddr += C.nStep;
		if (s & 0x80)
		{
			s &= 0x7f;
			l += (s * lv) >> 5;
			r += (s * rv) >> 5;
		}
		else
		{
			l -= (s * lv) >> 5;
			r -= (s * rv) >> 5;
		}
	}
	// 10 bits out
	auto Clamp = [] (s32 v) { v &= ~63; return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); };
	return Clamp (l) + Clamp (r);
}

// ---- time --------------------------------------------------------------------------

static s32 YmNext (void)
{
	s32 n = 0;
	for (int i = 0; i < 2; i++)
	{
		ymfm::ym3438::output_data Out;
		s_pYm[i]->m_Chip.generate (&Out);
		n += Out.data[0] + Out.data[1];
	}
	return n;
}

static void Update (void)
{
	for (int c = 0; c < 2; c++)
	{
		for (unsigned t = 0; t < 2; t++)
		{
			if (s_YmTimes[c].Timer[t] && s_YmTimes[c].Timer[t] <= s_nCycles)
			{
				s_YmTimes[c].Timer[t] = 0;
				s_pYm[c]->Expired (t);
			}
		}
	}
	const u64 nTarget = OUT_SAMPLES (s_nCycles);
	while (s_nOutDone < nTarget)
	{
		const u64 nYmPos = YM_POS (s_nOutDone), nRfPos = RF_POS (s_nOutDone);
		const u64 iy = nYmPos / YM_DIV, ir = nRfPos / RF_DIV;
		while (s_nYmDone < iy + 2)
		{
			s_nYmPrev = s_nYmCur;
			s_nYmCur = YmNext ();
			s_nYmDone++;
		}
		while (s_nRfDone < ir + 2)
		{
			s_nRfPrev = s_nRfCur;
			s_nRfCur = RfNext ();
			s_nRfDone++;
		}
		const s64 fy = (s64) (nYmPos % YM_DIV), fr = (s64) (nRfPos % RF_DIV);
		const s32 ym = s_nYmPrev + (s32) ((s_nYmCur - s_nYmPrev) * fy / YM_DIV);
		const s32 rf = s_nRfPrev + (s32) ((s_nRfCur - s_nRfPrev) * fr / RF_DIV);
		s32 v = (ym * 2 + 2) / 5 + rf;
		if (v > 32767) v = 32767;
		if (v < -32768) v = -32768;
		if (s_nFifoWr - s_nFifoRd >= FIFO)
		{
			s_nFifoRd++;
		}
		s_Fifo[s_nFifoWr++ % FIFO] = (s16) v;
		s_nOutDone++;
	}
}

// ---- interface for the machine -------------------------------------------------------

void s18_sound_init (void (*pIrq) (bool))
{
	s_pIrq = pIrq;
	for (int i = 0; i < 2; i++)
	{
		if (!s_pYm[i]) s_pYm[i] = new TOpn (i);
	}
}

void s18_sound_reset (void)
{
	s_nCycles = s_nOutDone = s_nYmDone = s_nRfDone = 0;
	s_nYmPrev = s_nYmCur = s_nRfPrev = s_nRfCur = 0;
	memset (s_YmTimes, 0, sizeof s_YmTimes);
	s_nFifoWr = s_nFifoRd = 0;
	for (int i = 0; i < 2; i++) s_pYm[i]->m_Chip.reset ();
	memset (&R, 0, sizeof R);
}

void s18_sound_advance (unsigned nCycles)
{
	s_nCycles += nCycles;
	Update ();
}

u8 s18_sound_ym_r (int nChip, unsigned nOffset)
{
	Update ();
	return s_pYm[nChip & 1]->m_Chip.read (nOffset & 3);
}

void s18_sound_ym_w (int nChip, unsigned nOffset, u8 nData)
{
	Update ();
	s_pYm[nChip & 1]->m_Chip.write (nOffset & 3, nData);
}

void s18_sound_rf_w (unsigned nOffset, u8 nData)
{
	Update ();
	auto &C = R.Chan[R.nCBank];
	switch (nOffset & 0x0f)
	{
	case 0x0: C.nEnv = nData; break;
	case 0x1: C.nPan = nData; break;
	case 0x2: C.nStep = (u16) ((C.nStep & 0xff00) | nData); break;
	case 0x3: C.nStep = (u16) ((C.nStep & 0x00ff) | nData << 8); break;
	case 0x4: C.nLoop = (u16) ((C.nLoop & 0xff00) | nData); break;
	case 0x5: C.nLoop = (u16) ((C.nLoop & 0x00ff) | nData << 8); break;
	case 0x6:
		C.nStart = nData;
		if (!C.bEnable) C.nAddr = (u32) C.nStart << (8 + 11);
		break;
	case 0x7:
		R.bEnable = (nData >> 7) & 1;
		if (nData & 0x40)	R.nCBank = nData & 7;
		else			R.nWBank = (u16) ((nData & 0x0f) << 12);
		break;
	case 0x8:
		for (int i = 0; i < 8; i++)
		{
			R.Chan[i].bEnable = (~nData >> i) & 1;
			if (!R.Chan[i].bEnable) R.Chan[i].nAddr = (u32) R.Chan[i].nStart << (8 + 11);
		}
		break;
	}
}

u8 s18_sound_rf_mem_r (unsigned nOffset)		{ return R.Ram[(R.nWBank | (nOffset & 0xfff)) & 0xffff]; }
void s18_sound_rf_mem_w (unsigned nOffset, u8 nData)	{ Update (); R.Ram[(R.nWBank | (nOffset & 0xfff)) & 0xffff] = nData; }

int s18_sound_read (int16_t *pDst, int nMaxFrames)
{
	int n = 0;
	while (n < nMaxFrames && s_nFifoRd != s_nFifoWr)
	{
		pDst[n * 2] = pDst[n * 2 + 1] = s_Fifo[s_nFifoRd++ % FIFO];
		n++;
	}
	return n;
}

// save states: everything but the output FIFO (bSave false = load)
size_t s18_sound_state (u8 *pBuf, size_t nMax, bool bSave)
{
	struct THead
	{
		u32 nYmSize[2];
		u64 nCycles, nOutDone, nYmDone, nRfDone;
		s32 nYmPrev, nYmCur, nRfPrev, nRfCur;
		TYmTimes Times[2];
	} H;
	std::vector<uint8_t> Ym[2];
	if (bSave)
	{
		memset (&H, 0, sizeof H);
		for (int i = 0; i < 2; i++)
		{
			ymfm::ymfm_saved_state State (Ym[i], true);
			s_pYm[i]->m_Chip.save_restore (State);
			H.nYmSize[i] = (u32) Ym[i].size ();
		}
		H.nCycles = s_nCycles; H.nOutDone = s_nOutDone; H.nYmDone = s_nYmDone; H.nRfDone = s_nRfDone;
		H.nYmPrev = s_nYmPrev; H.nYmCur = s_nYmCur; H.nRfPrev = s_nRfPrev; H.nRfCur = s_nRfCur;
		memcpy (H.Times, s_YmTimes, sizeof H.Times);
		const size_t nSize = sizeof H + sizeof R + Ym[0].size () + Ym[1].size ();
		if (nSize > nMax) return 0;
		u8 *p = pBuf;
		memcpy (p, &H, sizeof H);		p += sizeof H;
		memcpy (p, &R, sizeof R);		p += sizeof R;
		memcpy (p, Ym[0].data (), Ym[0].size ());	p += Ym[0].size ();
		memcpy (p, Ym[1].data (), Ym[1].size ());
		return nSize;
	}
	if (nMax < sizeof H + sizeof R) return 0;
	memcpy (&H, pBuf, sizeof H);
	const size_t nSize = sizeof H + sizeof R + H.nYmSize[0] + H.nYmSize[1];
	if (nSize > nMax) return 0;
	const u8 *p = pBuf + sizeof H;
	memcpy (&R, p, sizeof R);			p += sizeof R;
	for (int i = 0; i < 2; i++)
	{
		Ym[i].assign (p, p + H.nYmSize[i]);	p += H.nYmSize[i];
		ymfm::ymfm_saved_state State (Ym[i], false);
		s_pYm[i]->m_Chip.save_restore (State);
	}
	s_nCycles = H.nCycles; s_nOutDone = H.nOutDone; s_nYmDone = H.nYmDone; s_nRfDone = H.nRfDone;
	s_nYmPrev = H.nYmPrev; s_nYmCur = H.nYmCur; s_nRfPrev = H.nRfPrev; s_nRfCur = H.nRfCur;
	memcpy (s_YmTimes, H.Times, sizeof s_YmTimes);
	s_nFifoWr = s_nFifoRd = 0;
	return nSize;
}
