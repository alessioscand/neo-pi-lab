/*
 * s16-pi - Intel 8751 (see i8751.h)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "i8751.h"
#include <string.h>

// SFRs (index = address - 0x80)
enum { P0 = 0x00, SP = 0x01, DPL = 0x02, DPH = 0x03, PCON = 0x07, TCON = 0x08, TMOD = 0x09,
       TL0 = 0x0a, TL1 = 0x0b, TH0 = 0x0c, TH1 = 0x0d, P1 = 0x10, SCON = 0x18, SBUF = 0x19,
       P2 = 0x20, IE = 0x28, P3 = 0x30, IP = 0x38, PSW = 0x50, ACC = 0x60, B = 0x70 };

#define A	m_sfr[ACC]
#define PSWR	m_sfr[PSW]
#define CY	(PSWR >> 7)
#define DPTR	((uint16_t) (m_sfr[DPH] << 8 | m_sfr[DPL]))

void i8751_cpu::init (const uint8_t *pRom, size_t nRomSize, const TBus &rBus)
{
	m_rom = pRom;
	m_rommask = nRomSize - 1;
	m_bus = rBus;
	reset ();
}

void i8751_cpu::reset (void)
{
	memset (&m_s, 0, sizeof m_s);
	m_sfr[SP] = 0x07;
	m_sfr[P0] = m_sfr[P1] = m_sfr[P2] = m_sfr[P3] = 0xff;
}

uint8_t i8751_cpu::sfr_r (uint8_t a)
{
	a -= 0x80;
	switch (a)
	{
	case P0: case P1: case P2: case P3:
		return m_sfr[a] & m_bus.pPortRead (m_bus.pCtx, a >> 4);
	default:
		return m_sfr[a];
	}
}

uint8_t i8751_cpu::dir_r_latch (uint8_t a)
{
	return a < 0x80 ? m_ram[a] : m_sfr[a - 0x80];
}

void i8751_cpu::sfr_w (uint8_t a, uint8_t v)
{
	a -= 0x80;
	m_sfr[a] = v;
	if (a == P0 || a == P1 || a == P2 || a == P3)
	{
		m_bus.pPortWrite (m_bus.pCtx, a >> 4, v);
	}
}

// bits 0x00-0x7f are in RAM 0x20-0x2f, 0x80-0xff in the SFRs at multiples of 8
bool i8751_cpu::bit_r (uint8_t b)
{
	if (b < 0x80) return (m_ram[0x20 + (b >> 3)] >> (b & 7)) & 1;
	return (sfr_r (b & 0xf8) >> (b & 7)) & 1;
}

void i8751_cpu::bit_w (uint8_t b, bool v)
{
	const uint8_t m = (uint8_t) (1 << (b & 7));
	if (b < 0x80)
	{
		uint8_t &r = m_ram[0x20 + (b >> 3)];
		r = v ? (r | m) : (r & ~m);
	}
	else
	{
		const uint8_t a = b & 0xf8;
		const uint8_t r = m_sfr[a - 0x80];		// the latch
		sfr_w (a, v ? (r | m) : (r & ~m));
	}
}

void i8751_cpu::add (uint8_t v, int c)
{
	const unsigned a = A, r = a + v + c;
	uint8_t f = PSWR & ~0xc4;
	if (r > 0xff) f |= 0x80;
	if (((a & 0x0f) + (v & 0x0f) + c) > 0x0f) f |= 0x40;
	if (~(a ^ v) & (a ^ r) & 0x80) f |= 0x04;
	PSWR = f;
	A = (uint8_t) r;
}

void i8751_cpu::sub (uint8_t v, int c)
{
	const unsigned a = A, r = a - v - c;
	uint8_t f = PSWR & ~0xc4;
	if (r > 0xff) f |= 0x80;
	if (((a & 0x0f) - (v & 0x0f) - c) & 0x10) f |= 0x40;
	if ((a ^ v) & (a ^ r) & 0x80) f |= 0x04;
	PSWR = f;
	A = (uint8_t) r;
}

// one instruction; returns its machine cycles
int i8751_cpu::exec (void)
{
	const uint8_t op = fetch ();
	const int lo = op & 0x0f;

	// the regular columns: @R0 @R1 (6-7) and R0-R7 (8-15), and AJMP / ACALL
	if ((op & 0x1f) == 0x01)					// AJMP addr11
	{
		const uint8_t l = fetch ();
		m_pc = (uint16_t) ((m_pc & 0xf800) | ((op & 0xe0) << 3) | l);
		return 2;
	}
	if ((op & 0x1f) == 0x11)					// ACALL addr11
	{
		const uint8_t l = fetch ();
		push ((uint8_t) m_pc); push ((uint8_t) (m_pc >> 8));
		m_pc = (uint16_t) ((m_pc & 0xf800) | ((op & 0xe0) << 3) | l);
		return 2;
	}
	if (lo >= 6)
	{
		const bool ind = lo < 8;
		const uint8_t ri = ind ? reg (lo & 1) : 0;
		#define SRC	(ind ? m_ram[ri & 0x7f] : reg (lo & 7))
		#define DST(v)	do { if (ind) m_ram[ri & 0x7f] = (v); else reg (lo & 7) = (v); } while (0)
		switch (op >> 4)
		{
		case 0x0: DST ((uint8_t) (SRC + 1)); return 1;				// INC
		case 0x1: DST ((uint8_t) (SRC - 1)); return 1;				// DEC
		case 0x2: add (SRC, 0); return 1;					// ADD
		case 0x3: add (SRC, CY); return 1;					// ADDC
		case 0x4: A |= SRC; return 1;						// ORL
		case 0x5: A &= SRC; return 1;						// ANL
		case 0x6: A ^= SRC; return 1;						// XRL
		case 0x7: { uint8_t v = fetch (); DST (v); return 1; }			// MOV x,#imm
		case 0x8: { uint8_t d = fetch (); dir_w (d, SRC); return 2; }		// MOV dir,x
		case 0x9: sub (SRC, CY); return 1;					// SUBB
		case 0xa: { uint8_t d = fetch (); DST (dir_r (d)); return 2; }		// MOV x,dir
		case 0xb: {								// CJNE x,#imm,rel
			uint8_t v = fetch (); int8_t r = (int8_t) fetch (); uint8_t s = SRC;
			PSWR = (uint8_t) ((PSWR & 0x7f) | (s < v ? 0x80 : 0));
			if (s != v) m_pc = (uint16_t) (m_pc + r);
			return 2; }
		case 0xc: { uint8_t t = A; A = SRC; DST (t); return 1; }		// XCH
		case 0xd:
			if (ind)							// XCHD A,@Ri
			{
				uint8_t t = m_ram[ri & 0x7f];
				m_ram[ri & 0x7f] = (uint8_t) ((t & 0xf0) | (A & 0x0f));
				A = (uint8_t) ((A & 0xf0) | (t & 0x0f));
				return 1;
			}
			else								// DJNZ Rn,rel
			{
				int8_t r = (int8_t) fetch ();
				if (--reg (lo & 7) != 0) m_pc = (uint16_t) (m_pc + r);
				return 2;
			}
		case 0xe: A = SRC; return 1;						// MOV A,x
		case 0xf: DST (A); return 1;						// MOV x,A
		}
		#undef SRC
		#undef DST
	}

	switch (op)
	{
	case 0x00: return 1;								// NOP
	case 0x02: { uint8_t h = fetch (), l = fetch (); m_pc = (uint16_t) (h << 8 | l); return 2; }	// LJMP
	case 0x12: { uint8_t h = fetch (), l = fetch (); push ((uint8_t) m_pc); push ((uint8_t) (m_pc >> 8));
		     m_pc = (uint16_t) (h << 8 | l); return 2; }			// LCALL
	case 0x22: { uint8_t h = pop (), l = pop (); m_pc = (uint16_t) (h << 8 | l); return 2; }	// RET
	case 0x32: { uint8_t h = pop (), l = pop (); m_pc = (uint16_t) (h << 8 | l);	// RETI
		     if (m_s.in_irq & 2) m_s.in_irq &= ~2; else m_s.in_irq &= ~1;
		     return 2; }
	case 0x03: A = (uint8_t) (A >> 1 | A << 7); return 1;				// RR
	case 0x13: { uint8_t c = CY; PSWR = (uint8_t) ((PSWR & 0x7f) | (A & 1) << 7); A = (uint8_t) (A >> 1 | c << 7); return 1; }	// RRC
	case 0x23: A = (uint8_t) (A << 1 | A >> 7); return 1;				// RL
	case 0x33: { uint8_t c = CY; PSWR = (uint8_t) ((PSWR & 0x7f) | (A & 0x80)); A = (uint8_t) (A << 1 | c); return 1; }		// RLC
	case 0x04: A++; return 1;							// INC A
	case 0x14: A--; return 1;							// DEC A
	case 0x05: { uint8_t d = fetch (); dir_w (d, (uint8_t) (dir_r_latch (d) + 1)); return 1; }	// INC dir
	case 0x15: { uint8_t d = fetch (); dir_w (d, (uint8_t) (dir_r_latch (d) - 1)); return 1; }	// DEC dir
	case 0x24: add (fetch (), 0); return 1;
	case 0x25: add (dir_r (fetch ()), 0); return 1;
	case 0x34: add (fetch (), CY); return 1;
	case 0x35: add (dir_r (fetch ()), CY); return 1;
	case 0x94: sub (fetch (), CY); return 1;
	case 0x95: sub (dir_r (fetch ()), CY); return 1;
	case 0x42: { uint8_t d = fetch (); dir_w (d, dir_r_latch (d) | A); return 1; }		// ORL dir,A
	case 0x43: { uint8_t d = fetch (), v = fetch (); dir_w (d, dir_r_latch (d) | v); return 2; }
	case 0x44: A |= fetch (); return 1;
	case 0x45: A |= dir_r (fetch ()); return 1;
	case 0x52: { uint8_t d = fetch (); dir_w (d, dir_r_latch (d) & A); return 1; }		// ANL dir,A
	case 0x53: { uint8_t d = fetch (), v = fetch (); dir_w (d, dir_r_latch (d) & v); return 2; }
	case 0x54: A &= fetch (); return 1;
	case 0x55: A &= dir_r (fetch ()); return 1;
	case 0x62: { uint8_t d = fetch (); dir_w (d, dir_r_latch (d) ^ A); return 1; }		// XRL dir,A
	case 0x63: { uint8_t d = fetch (), v = fetch (); dir_w (d, dir_r_latch (d) ^ v); return 2; }
	case 0x64: A ^= fetch (); return 1;
	case 0x65: A ^= dir_r (fetch ()); return 1;
	case 0x74: A = fetch (); return 1;						// MOV A,#imm
	case 0x75: { uint8_t d = fetch (), v = fetch (); dir_w (d, v); return 2; }	// MOV dir,#imm
	case 0x85: { uint8_t s = fetch (), d = fetch (); dir_w (d, dir_r (s)); return 2; }	// MOV dir,dir
	case 0xe5: A = dir_r (fetch ()); return 1;					// MOV A,dir
	case 0xf5: dir_w (fetch (), A); return 1;					// MOV dir,A
	case 0x90: m_sfr[DPH] = fetch (); m_sfr[DPL] = fetch (); return 2;		// MOV DPTR,#imm16
	case 0xa3: if (++m_sfr[DPL] == 0) m_sfr[DPH]++; return 2;			// INC DPTR
	case 0x73: m_pc = (uint16_t) (DPTR + A); return 2;				// JMP @A+DPTR
	case 0x83: A = m_rom[(uint16_t) (m_pc + A) & m_rommask]; return 2;		// MOVC A,@A+PC
	case 0x93: A = m_rom[(uint16_t) (DPTR + A) & m_rommask]; return 2;		// MOVC A,@A+DPTR
	case 0xe0: A = m_bus.pXRead (m_bus.pCtx, DPTR); return 2;			// MOVX A,@DPTR
	case 0xe2: case 0xe3: A = m_bus.pXRead (m_bus.pCtx, (uint16_t) (m_sfr[P2] << 8 | reg (op & 1))); return 2;
	case 0xf0: m_bus.pXWrite (m_bus.pCtx, DPTR, A); return 2;			// MOVX @DPTR,A
	case 0xf2: case 0xf3: m_bus.pXWrite (m_bus.pCtx, (uint16_t) (m_sfr[P2] << 8 | reg (op & 1)), A); return 2;
	case 0x84: {									// DIV AB
		uint8_t b = m_sfr[B];
		PSWR &= ~0x84;
		if (b == 0) PSWR |= 0x04;
		else { uint8_t q = A / b, r = A % b; A = q; m_sfr[B] = r; }
		return 4; }
	case 0xa4: {									// MUL AB
		unsigned r = A * m_sfr[B];
		A = (uint8_t) r; m_sfr[B] = (uint8_t) (r >> 8);
		PSWR = (uint8_t) ((PSWR & ~0x84) | (r > 0xff ? 0x04 : 0));
		return 4; }
	case 0xc4: A = (uint8_t) (A << 4 | A >> 4); return 1;				// SWAP
	case 0xd4: {									// DA A
		unsigned a = A;
		if ((a & 0x0f) > 9 || (PSWR & 0x40)) a += 6;
		if (a > 0xff) PSWR |= 0x80;
		if ((a & 0x1f0) > 0x90 || (PSWR & 0x80)) a += 0x60;
		if (a > 0xff) PSWR |= 0x80;
		A = (uint8_t) a;
		return 1; }
	case 0xe4: A = 0; return 1;							// CLR A
	case 0xf4: A = (uint8_t) ~A; return 1;						// CPL A
	case 0xc5: { uint8_t d = fetch (), t = dir_r (d); dir_w (d, A); A = t; return 1; }	// XCH A,dir
	case 0xc0: push (dir_r (fetch ())); return 2;					// PUSH
	case 0xd0: { uint8_t d = fetch (); dir_w (d, pop ()); return 2; }		// POP
	case 0xb4: case 0xb5: {								// CJNE A,#imm / dir
		uint8_t v = op == 0xb4 ? fetch () : dir_r (fetch ());
		int8_t r = (int8_t) fetch ();
		PSWR = (uint8_t) ((PSWR & 0x7f) | (A < v ? 0x80 : 0));
		if (A != v) m_pc = (uint16_t) (m_pc + r);
		return 2; }
	case 0xd5: {									// DJNZ dir,rel
		uint8_t d = fetch (); int8_t r = (int8_t) fetch ();
		uint8_t v = (uint8_t) (dir_r_latch (d) - 1);
		dir_w (d, v);
		if (v) m_pc = (uint16_t) (m_pc + r);
		return 2; }
	// jumps
	case 0x80: { int8_t r = (int8_t) fetch (); m_pc = (uint16_t) (m_pc + r); return 2; }	// SJMP
	case 0x40: { int8_t r = (int8_t) fetch (); if (CY) m_pc = (uint16_t) (m_pc + r); return 2; }	// JC
	case 0x50: { int8_t r = (int8_t) fetch (); if (!CY) m_pc = (uint16_t) (m_pc + r); return 2; }	// JNC
	case 0x60: { int8_t r = (int8_t) fetch (); if (A == 0) m_pc = (uint16_t) (m_pc + r); return 2; }	// JZ
	case 0x70: { int8_t r = (int8_t) fetch (); if (A != 0) m_pc = (uint16_t) (m_pc + r); return 2; }	// JNZ
	case 0x10: { uint8_t b = fetch (); int8_t r = (int8_t) fetch ();			// JBC
		     bool v = b < 0x80 ? bit_r (b) : ((m_sfr[(b & 0xf8) - 0x80] >> (b & 7)) & 1);
		     if (v) { bit_w (b, false); m_pc = (uint16_t) (m_pc + r); } return 2; }
	case 0x20: { uint8_t b = fetch (); int8_t r = (int8_t) fetch (); if (bit_r (b)) m_pc = (uint16_t) (m_pc + r); return 2; }	// JB
	case 0x30: { uint8_t b = fetch (); int8_t r = (int8_t) fetch (); if (!bit_r (b)) m_pc = (uint16_t) (m_pc + r); return 2; }	// JNB
	// bits
	case 0x72: { uint8_t b = fetch (); if (bit_r (b)) PSWR |= 0x80; return 2; }		// ORL C,bit
	case 0xa0: { uint8_t b = fetch (); if (!bit_r (b)) PSWR |= 0x80; return 2; }	// ORL C,/bit
	case 0x82: { uint8_t b = fetch (); if (!bit_r (b)) PSWR &= 0x7f; return 2; }	// ANL C,bit
	case 0xb0: { uint8_t b = fetch (); if (bit_r (b)) PSWR &= 0x7f; return 2; }		// ANL C,/bit
	case 0x92: bit_w (fetch (), CY); return 2;					// MOV bit,C
	case 0xa2: { uint8_t b = fetch (); PSWR = (uint8_t) ((PSWR & 0x7f) | (bit_r (b) ? 0x80 : 0)); return 1; }	// MOV C,bit
	case 0xb2: { uint8_t b = fetch ();						// CPL bit
		     bool v = b < 0x80 ? bit_r (b) : ((m_sfr[(b & 0xf8) - 0x80] >> (b & 7)) & 1);
		     bit_w (b, !v); return 1; }
	case 0xb3: PSWR ^= 0x80; return 1;						// CPL C
	case 0xc2: bit_w (fetch (), false); return 1;					// CLR bit
	case 0xc3: PSWR &= 0x7f; return 1;						// CLR C
	case 0xd2: bit_w (fetch (), true); return 1;					// SETB bit
	case 0xd3: PSWR |= 0x80; return 1;						// SETB C
	default:   return 1;								// 0xa5: undefined
	}
}

// timers 0 and 1 counting machine cycles (modes 0-2; mode 3 as two 8-bit halves of timer 0)
void i8751_cpu::timers (int n)
{
	for (int t = 0; t < 2; t++)
	{
		if (!(m_sfr[TCON] & (t ? 0x40 : 0x10)))		// TRx
		{
			continue;
		}
		const uint8_t mode = (m_sfr[TMOD] >> (t * 4)) & 0x0f;
		if (mode & 0x0c)				// counter input or gated: not connected
		{
			continue;
		}
		uint8_t &tl = m_sfr[TL0 + t], &th = m_sfr[TH0 + t];
		const uint8_t tf = t ? 0x80 : 0x20;
		switch (mode & 3)
		{
		case 0: {					// 13 bits
			unsigned v = (unsigned) (th << 5 | (tl & 0x1f)) + (unsigned) n;
			if (v > 0x1fff) m_sfr[TCON] |= tf;
			tl = (uint8_t) ((tl & 0xe0) | (v & 0x1f)); th = (uint8_t) (v >> 5);
			break; }
		case 1: {					// 16 bits
			unsigned v = (unsigned) (th << 8 | tl) + (unsigned) n;
			if (v > 0xffff) m_sfr[TCON] |= tf;
			tl = (uint8_t) v; th = (uint8_t) (v >> 8);
			break; }
		case 2:						// 8 bits, reloaded from TH
			for (int i = 0; i < n; i++)
			{
				if (++tl == 0) { tl = th; m_sfr[TCON] |= tf; }
			}
			break;
		case 3:						// timer 0 only: TL0 as an 8-bit timer
			if (t == 0)
			{
				unsigned v = (unsigned) tl + (unsigned) n;
				if (v > 0xff) m_sfr[TCON] |= tf;
				tl = (uint8_t) v;
			}
			break;
		}
	}
}

// INT0, timer 0, INT1, timer 1, in this order within a priority level
bool i8751_cpu::interrupts (void)
{
	const uint8_t ie = m_sfr[IE];
	if (!(ie & 0x80))
	{
		return false;
	}
	static const struct { uint8_t ie, tcon, vector, clear; } Src[4] =
	{
		{0x01, 0x02, 0x03, 0x01}, {0x02, 0x20, 0x0b, 0xff}, {0x04, 0x08, 0x13, 0x04}, {0x08, 0x80, 0x1b, 0xff}
	};
	for (int pri = 1; pri >= 0; pri--)
	{
		if (m_s.in_irq & (pri ? 2 : 3))			// a level of this priority or higher is running
		{
			continue;
		}
		for (int i = 0; i < 4; i++)
		{
			if (!(ie & Src[i].ie) || !(m_sfr[TCON] & Src[i].tcon) || ((m_sfr[IP] >> i) & 1) != pri)
			{
				continue;
			}
			// the flag is cleared when vectoring: always for the timers, for
			// the external lines only when edge triggered
			if (Src[i].clear == 0xff || (m_sfr[TCON] & Src[i].clear))
			{
				m_sfr[TCON] &= ~Src[i].tcon;
			}
			push ((uint8_t) m_pc); push ((uint8_t) (m_pc >> 8));
			m_pc = Src[i].vector;
			m_s.in_irq |= pri ? 2 : 1;
			return true;
		}
	}
	return false;
}

void i8751_cpu::set_int (int nLine, bool bState)
{
	const uint8_t ie = nLine ? 0x08 : 0x02, it = nLine ? 0x04 : 0x01;
	if (bState)
	{
		if (!(m_sfr[TCON] & it) || !m_s.int_state[nLine])	// level, or the edge
		{
			m_sfr[TCON] |= ie;
		}
	}
	else if (!(m_sfr[TCON] & it))
	{
		m_sfr[TCON] &= ~ie;
	}
	m_s.int_state[nLine] = bState;
}

int i8751_cpu::run (int nCycles)
{
	int nUsed = 0;
	while (nUsed < nCycles)
	{
		int n = interrupts () ? 2 : 0;
		n += exec ();
		timers (n);
		nUsed += n;
	}
	return nUsed;
}
