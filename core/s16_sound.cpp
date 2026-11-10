/*
 * s16-pi - System 16B sound: YM2151 (ymfm, Aaron Giles) and the uPD7759 ADPCM
 * speech chip, fed by the Z80 one byte at a time (slave mode).
 *
 * uPD7759: state machine and tables from MAME's upd7759.cpp (Juergen
 * Buchmueller, Mike Balfour, Howie Cohen, Olivier Galibert, Aaron Giles),
 * re-implemented here.
 *
 * The board's analogue section follows the components of MAME's netlist
 * (nl_segas16b.cpp, Couriersud), as digital filters: the speech goes through
 * two Sallen-Key low-pass stages, then the mixer (one more low-pass, 3.4 kHz)
 * takes it together with the two YM2151 channels, into one speaker.
 *
 * Time is counted in Z80 cycles (5 MHz): the machine calls s16_sound_advance
 * after each slice of the sound CPU.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "s16.h"
#include "ymfm_opm.h"
#include <string.h>
#include <math.h>

// rates, per Z80 cycle (5 MHz): YM2151 clock 4 MHz (a sample every 64 clocks),
// uPD7759 clock 640 kHz, output 48 kHz
#define OPM_CLOCKS(c)	((c) * 4 / 5)
#define UPD_CLOCKS(c)	((c) * 16 / 125)
#define OUT_SAMPLES(c)	((c) * 6 / 625)

static void (*s_pNmi) (void);		// uPD7759 data request -> Z80 NMI

// ---- uPD7759 ---------------------------------------------------------------

enum { ST_IDLE, ST_DROP_DRQ, ST_START, ST_FIRST_REQ, ST_LAST_SAMPLE, ST_DUMMY1, ST_ADDR_MSB, ST_ADDR_LSB,
       ST_DUMMY2, ST_BLOCK_HEADER, ST_NIBBLE_COUNT, ST_NIBBLE_MSN, ST_NIBBLE_LSN };

static struct TUpd
{
	u8 nFifoIn, bReset, bMd, bDrq, bSlave;
	u8 nState, nPostDrqState;
	s32 nClocksLeft, nPostDrqClocks;
	u16 nNibblesLeft;
	u8 nRepeatCount, nReqSample, nLastSample, nBlockHeader, nSampleRate, bFirstValidHeader;
	u8 nAdpcmData;
	s8 nAdpcmState;
	s16 nSample;
	bool bTimer;			// the next step is due at nNext
	u64 nNext;			// in uPD7759 clocks
} U;

static const s16 s_UpdStep[16][16] =
{
	{ 0,  0,  1,  2,  3,   5,   7,  10,  0,   0,  -1,  -2,  -3,   -5,   -7,  -10 },
	{ 0,  1,  2,  3,  4,   6,   8,  13,  0,  -1,  -2,  -3,  -4,   -6,   -8,  -13 },
	{ 0,  1,  2,  4,  5,   7,  10,  15,  0,  -1,  -2,  -4,  -5,   -7,  -10,  -15 },
	{ 0,  1,  3,  4,  6,   9,  13,  19,  0,  -1,  -3,  -4,  -6,   -9,  -13,  -19 },
	{ 0,  2,  3,  5,  8,  11,  15,  23,  0,  -2,  -3,  -5,  -8,  -11,  -15,  -23 },
	{ 0,  2,  4,  7, 10,  14,  19,  29,  0,  -2,  -4,  -7, -10,  -14,  -19,  -29 },
	{ 0,  3,  5,  8, 12,  16,  22,  33,  0,  -3,  -5,  -8, -12,  -16,  -22,  -33 },
	{ 1,  4,  7, 10, 15,  20,  29,  43, -1,  -4,  -7, -10, -15,  -20,  -29,  -43 },
	{ 1,  4,  8, 13, 18,  25,  35,  53, -1,  -4,  -8, -13, -18,  -25,  -35,  -53 },
	{ 1,  6, 10, 16, 22,  31,  43,  64, -1,  -6, -10, -16, -22,  -31,  -43,  -64 },
	{ 2,  7, 12, 19, 27,  37,  51,  76, -2,  -7, -12, -19, -27,  -37,  -51,  -76 },
	{ 2,  9, 16, 24, 34,  46,  64,  96, -2,  -9, -16, -24, -34,  -46,  -64,  -96 },
	{ 3, 11, 19, 29, 41,  57,  79, 117, -3, -11, -19, -29, -41,  -57,  -79, -117 },
	{ 4, 13, 24, 36, 50,  69,  96, 143, -4, -13, -24, -36, -50,  -69,  -96, -143 },
	{ 4, 16, 29, 44, 62,  85, 118, 175, -4, -16, -29, -44, -62,  -85, -118, -175 },
	{ 6, 20, 36, 54, 76, 104, 144, 214, -6, -20, -36, -54, -76, -104, -144, -214 },
};
static const s8 s_UpdStateStep[16] = {-1, -1, 0, 0, 1, 2, 2, 3, -1, -1, 0, 0, 1, 2, 2, 3};

static void UpdAdpcm (int nData)
{
	U.nSample = (s16) (U.nSample + s_UpdStep[U.nAdpcmState][nData]);
	int n = U.nAdpcmState + s_UpdStateStep[nData];
	U.nAdpcmState = (s8) (n < 0 ? 0 : (n > 15 ? 15 : n));
}

// one step of the chip: what it does with the byte it was given, and what it
// asks for next (every byte comes from the Z80, on request)
static void UpdAdvance (void)
{
	switch (U.nState)
	{
	case ST_IDLE:
		U.nClocksLeft = 4;
		break;
	case ST_DROP_DRQ:
		U.bDrq = 0;
		U.nClocksLeft = U.nPostDrqClocks;
		U.nState = U.nPostDrqState;
		break;
	case ST_START:
		U.nReqSample = U.bSlave ? 0x10 : U.nFifoIn;
		U.nClocksLeft = 70;
		U.nState = ST_FIRST_REQ;
		break;
	case ST_FIRST_REQ:			// asks for the number of the last sample
		U.bDrq = 1;
		U.nClocksLeft = 44;
		U.nState = ST_LAST_SAMPLE;
		break;
	case ST_LAST_SAMPLE:
		U.nLastSample = U.nFifoIn;
		U.bDrq = 1;
		U.nClocksLeft = 28;
		U.nState = U.nReqSample > U.nLastSample ? ST_IDLE : ST_DUMMY1;
		break;
	case ST_DUMMY1:
		U.bDrq = 1;
		U.nClocksLeft = 32;
		U.nState = ST_ADDR_MSB;
		break;
	case ST_ADDR_MSB:			// (the address is of no use in slave mode)
		U.bDrq = 1;
		U.nClocksLeft = 44;
		U.nState = ST_ADDR_LSB;
		break;
	case ST_ADDR_LSB:
		U.bDrq = 1;
		U.nClocksLeft = 36;
		U.nState = ST_DUMMY2;
		break;
	case ST_DUMMY2:
		U.bFirstValidHeader = 0;
		U.bDrq = 1;
		U.nClocksLeft = 36;
		U.nState = ST_BLOCK_HEADER;
		break;
	case ST_BLOCK_HEADER:
		if (U.nRepeatCount)
		{
			U.nRepeatCount--;
		}
		U.nBlockHeader = U.nFifoIn;
		U.bDrq = 1;
		switch (U.nBlockHeader & 0xc0)
		{
		case 0x00:			// silence
			U.nClocksLeft = 1024 * ((U.nBlockHeader & 0x3f) + 1);
			U.nState = (U.nBlockHeader == 0 && U.bFirstValidHeader) ? ST_IDLE : ST_BLOCK_HEADER;
			U.nSample = 0;
			U.nAdpcmState = 0;
			break;
		case 0x40:			// 256 nibbles
			U.nSampleRate = (u8) ((U.nBlockHeader & 0x3f) + 1);
			U.nNibblesLeft = 256;
			U.nClocksLeft = 36;
			U.nState = ST_NIBBLE_MSN;
			break;
		case 0x80:			// n nibbles
			U.nSampleRate = (u8) ((U.nBlockHeader & 0x3f) + 1);
			U.nClocksLeft = 36;
			U.nState = ST_NIBBLE_COUNT;
			break;
		case 0xc0:			// repeat
			U.nRepeatCount = (u8) ((U.nBlockHeader & 7) + 1);
			U.nClocksLeft = 36;
			U.nState = ST_BLOCK_HEADER;
			break;
		}
		if (U.nBlockHeader != 0)
		{
			U.bFirstValidHeader = 1;
		}
		break;
	case ST_NIBBLE_COUNT:
		U.nNibblesLeft = (u16) (U.nFifoIn + 1);
		U.bDrq = 1;
		U.nClocksLeft = 36;
		U.nState = ST_NIBBLE_MSN;
		break;
	case ST_NIBBLE_MSN:
		U.nAdpcmData = U.nFifoIn;
		UpdAdpcm (U.nAdpcmData >> 4);
		U.bDrq = 1;
		U.nClocksLeft = U.nSampleRate * 4;
		U.nState = --U.nNibblesLeft == 0 ? ST_BLOCK_HEADER : ST_NIBBLE_LSN;
		break;
	case ST_NIBBLE_LSN:
		UpdAdpcm (U.nAdpcmData & 15);
		U.nClocksLeft = U.nSampleRate * 4;
		U.nState = --U.nNibblesLeft == 0 ? ST_BLOCK_HEADER : ST_NIBBLE_MSN;
		break;
	}
	if (U.bDrq)				// the request lasts 21 clocks
	{
		U.nPostDrqState = U.nState;
		U.nPostDrqClocks = U.nClocksLeft - 21;
		U.nState = ST_DROP_DRQ;
		U.nClocksLeft = 21;
	}
}

static void UpdReset (void)
{
	const u8 bReset = U.bReset, bMd = U.bMd, nFifo = U.nFifoIn;
	memset (&U, 0, sizeof U);
	U.bReset = bReset;
	U.bMd = bMd;
	U.nFifoIn = nFifo;
}

// ---- YM2151 and the mix ----------------------------------------------------------

static u64 s_nCycles;			// Z80 cycles since reset
static u64 s_nOutDone, s_nOpmDone;	// output and YM2151 samples made
static s32 s_nOpmPrev, s_nOpmCur;
static u64 s_nOpmTimer[2];		// when each YM2151 timer expires (Z80 cycles), 0 = stopped
static u64 s_nOpmBusyEnd;

enum { FIFO = 8192 };
static s16 s_Fifo[FIFO];		// mono
static unsigned s_nFifoWr, s_nFifoRd;

class TOpm : public ymfm::ymfm_interface
{
public:
	TOpm (void) : m_Chip (*this) {}
	ymfm::ym2151 m_Chip;

	void ymfm_set_timer (uint32_t nTimer, int32_t nClocks) override
	{
		s_nOpmTimer[nTimer] = nClocks >= 0 ? s_nCycles + ((u64) nClocks * 5 + 3) / 4 + 1 : 0;
	}
	void ymfm_set_busy_end (uint32_t nClocks) override	{ s_nOpmBusyEnd = s_nCycles + ((u64) nClocks * 5 + 3) / 4; }
	bool ymfm_is_busy (void) override			{ return s_nCycles < s_nOpmBusyEnd; }
	void ymfm_update_irq (bool) override			{}	// not connected on this board
	void Expired (unsigned nTimer)				{ m_engine->engine_timer_expired (nTimer); }
};
static TOpm *s_pOpm;

static s32 OpmNext (void)
{
	ymfm::ym2151::output_data Out;
	s_pOpm->m_Chip.generate (&Out);
	return Out.data[0] + Out.data[1];
}

// ---- the analogue section, at 48 kHz --------------------------------------------
//
// Speech: the uPD7759 sinks a current (0-1020 uA) through R11 (1K) from 5 V;
// R14 (5.6K) and R16 (6.8K) divide it into the first Sallen-Key stage (R15
// 12K, C26 22n, C25 2.2n: 3.6 kHz, Q 1.31), then the second (R12 1.3K, R13
// 6.2K, C23 22n, C24 10n: 3.8 kHz, Q 0.56), then C62 (2.2u) into the mixer
// through R6 (10K).
// YM2151: each channel (2.5 V +- 0.5 V) through a buffer and 47K into the mixer.
// Mixer: feedback R7 47K with C20 1n (a low-pass at 3.4 kHz); gain 4.7 for the
// speech, 1 for each YM2151 channel. Then C63, R3 (10K) and the volume
// trimmer (2K): what is left is taken as 1/0.2 of full scale, as MAME does.
//
// With MAME's input levels (YM2151 0.43, uPD7759 0.48), for 16-bit samples:
//   YM2151 (left + right)	0.43 * 0.5 V * 0.164 * 5	= 0.176
//   uPD7759 (sample * 128)	0.48 * 0.51 mA * 507 ohm * 4.7 * 0.164 * 5 = 0.478
#define GAIN_OPM	0.176
#define GAIN_UPD	0.478

struct TBiquad { double b0, b1, b2, a1, a2, z1, z2; };
static TBiquad s_Sk1, s_Sk2;
static double s_fLp, s_fLpK;			// the mixer's low-pass
static double s_fHpIn, s_fHpOut, s_fHpK;	// the coupling capacitors (about 6 Hz)
static double s_fSpHpIn, s_fSpHpOut;

static void LowPass (TBiquad *pF, double fHz, double fQ)
{
	const double w = 2 * M_PI * fHz / 48000, a = sin (w) / (2 * fQ), c = cos (w), n = 1 / (1 + a);
	pF->b0 = pF->b2 = (1 - c) / 2 * n;
	pF->b1 = (1 - c) * n;
	pF->a1 = -2 * c * n;
	pF->a2 = (1 - a) * n;
	pF->z1 = pF->z2 = 0;
}

static inline double Run (TBiquad *pF, double x)
{
	const double y = pF->b0 * x + pF->z1;
	pF->z1 = pF->b1 * x - pF->a1 * y + pF->z2;
	pF->z2 = pF->b2 * x - pF->a2 * y;
	return y;
}

static void AnalogReset (void)
{
	LowPass (&s_Sk1, 3609, 1.306);
	LowPass (&s_Sk2, 3780, 0.5615);
	s_fLpK = 1 - exp (-2 * M_PI * 3386 / 48000);
	s_fHpK = exp (-2 * M_PI * 6.5 / 48000);
	s_fLp = s_fHpIn = s_fHpOut = s_fSpHpIn = s_fSpHpOut = 0;
}

// one output sample from the YM2151's (left + right) and the uPD7759's sample
static inline s32 Analog (s32 nOpm, s32 nUpd)
{
	double fSp = Run (&s_Sk2, Run (&s_Sk1, nUpd * 128.0));
	s_fSpHpOut = s_fHpK * (s_fSpHpOut + fSp - s_fSpHpIn);		// C62
	s_fSpHpIn = fSp;
	const double fMix = GAIN_OPM * nOpm + GAIN_UPD * s_fSpHpOut;
	s_fLp += s_fLpK * (fMix - s_fLp);				// C20
	s_fHpOut = s_fHpK * (s_fHpOut + s_fLp - s_fHpIn);		// C63
	s_fHpIn = s_fLp;
	return (s32) lrint (s_fHpOut);
}

// brings timers, the uPD7759 and the output up to the current time
static void Update (void)
{
	for (unsigned t = 0; t < 2; t++)
	{
		if (s_nOpmTimer[t] && s_nOpmTimer[t] <= s_nCycles)
		{
			s_nOpmTimer[t] = 0;
			s_pOpm->Expired (t);		// (a periodic timer sets itself again)
		}
	}

	const u64 nUpdNow = UPD_CLOCKS (s_nCycles);
	while (U.bTimer && U.nNext <= nUpdNow)
	{
		const u8 bOldDrq = U.bDrq, nOldState = U.nState;
		UpdAdvance ();
		if (!bOldDrq && U.bDrq && s_pNmi)
		{
			s_pNmi ();
		}
		if (U.nState != ST_IDLE || nOldState != ST_IDLE)
		{
			U.nNext += (u64) (U.nClocksLeft > 0 ? U.nClocksLeft : 1);
		}
		else
		{
			U.bTimer = false;
		}
	}

	const u64 nTarget = OUT_SAMPLES (s_nCycles);
	while (s_nOutDone < nTarget)
	{
		// YM2151: linear interpolation between its samples (62.5 kHz)
		const u64 nPos = s_nOutDone * 62500;		// in 1/48000 of a YM2151 sample
		const u64 i = nPos / 48000;
		while (s_nOpmDone < i + 2)
		{
			s_nOpmPrev = s_nOpmCur;
			s_nOpmCur = OpmNext ();
			s_nOpmDone++;
		}
		const s32 nFrac = (s32) (nPos % 48000);
		s32 v = s_nOpmPrev + (s32) ((s64) (s_nOpmCur - s_nOpmPrev) * nFrac / 48000);
		v = Analog (v, U.nState != ST_IDLE ? U.nSample : 0);
		if (v > 32767) v = 32767;
		if (v < -32768) v = -32768;
		if (s_nFifoWr - s_nFifoRd >= FIFO)
		{
			s_nFifoRd++;			// nobody is reading: drop the oldest
		}
		s_Fifo[s_nFifoWr++ % FIFO] = (s16) v;
		s_nOutDone++;
	}
}

// ---- interface for the machine -----------------------------------------------------

void s16_sound_init (void (*pNmi) (void))
{
	s_pNmi = pNmi;
	if (!s_pOpm)
	{
		s_pOpm = new TOpm;
	}
}

void s16_sound_reset (void)
{
	s_nCycles = s_nOutDone = s_nOpmDone = 0;
	s_nOpmPrev = s_nOpmCur = 0;
	s_nOpmTimer[0] = s_nOpmTimer[1] = 0;
	s_nOpmBusyEnd = 0;
	s_nFifoWr = s_nFifoRd = 0;
	s_pOpm->m_Chip.reset ();
	AnalogReset ();
	memset (&U, 0, sizeof U);
	U.bReset = 1;
	U.bMd = 1;
}

void s16_sound_advance (unsigned nCycles)
{
	s_nCycles += nCycles;
	Update ();
}

u8 s16_sound_ym_r (unsigned nOffset)
{
	Update ();
	return s_pOpm->m_Chip.read (nOffset & 1);
}

void s16_sound_ym_w (unsigned nOffset, u8 nData)
{
	Update ();
	s_pOpm->m_Chip.write (nOffset & 1, nData);
}

// /MD and /RESET: MD going low starts slave mode, RESET going low resets
void s16_sound_upd_control (bool bMd, bool bReset)
{
	Update ();
	const u8 bOldMd = U.bMd;
	U.bMd = bMd;
	if (U.nState == ST_IDLE && U.bReset)
	{
		if (bOldMd && !bMd)
		{
			U.bSlave = 1;
			U.nState = ST_START;
			U.bTimer = true;
			U.nNext = UPD_CLOCKS (s_nCycles);
		}
		else if (!bOldMd && bMd)
		{
			U.bSlave = 0;
		}
	}
	const u8 bOldReset = U.bReset;
	U.bReset = bReset;
	if (bOldReset && !bReset)
	{
		UpdReset ();
	}
}

void s16_sound_upd_w (u8 nData)		{ Update (); U.nFifoIn = nData; }
bool s16_sound_upd_idle (void)		{ Update (); return U.nState == ST_IDLE; }

int s16_sound_read (int16_t *pDst, int nMaxFrames)
{
	int n = 0;
	while (n < nMaxFrames && s_nFifoRd != s_nFifoWr)
	{
		pDst[n * 2] = pDst[n * 2 + 1] = s_Fifo[s_nFifoRd++ % FIFO];
		n++;
	}
	return n;
}
