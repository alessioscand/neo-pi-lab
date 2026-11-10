/*
 * s16-pi - Intel 8751 (MCS-51) microcontroller, as the protection MCU of
 * some System 16B boards: 4 KB of program ROM, 128 bytes of RAM, two timers,
 * INT0 / INT1. The serial port is not emulated (the boards do not use it).
 *
 * Written for s16-pi from Intel's MCS-51 instruction set; MAME's mcs51.cpp
 * (Steve Ellenoff, Manuel Abadia, Couriersud) was the reference for the
 * interrupt lines.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef I8751_H
#define I8751_H

#include <stdint.h>
#include <stddef.h>

class i8751_cpu
{
public:
	// the board: external data memory (MOVX) and the four ports
	struct TBus
	{
		void *pCtx;
		uint8_t (*pXRead) (void *pCtx, uint16_t nAddr);
		void (*pXWrite) (void *pCtx, uint16_t nAddr, uint8_t nData);
		uint8_t (*pPortRead) (void *pCtx, int nPort);
		void (*pPortWrite) (void *pCtx, int nPort, uint8_t nData);
	};

	void init (const uint8_t *pRom, size_t nRomSize, const TBus &rBus);
	void reset (void);
	int run (int nCycles);			// machine cycles (clock / 12); returns those used
	void set_int (int nLine, bool bState);	// 0 = INT0, 1 = INT1 (active = asserted)
	uint16_t pc (void) const		{ return m_pc; }

	// save states: everything below m_rom
	size_t state_size (void) const		{ return sizeof m_s; }
	void *state_ptr (void)			{ return &m_s; }

private:
	uint8_t fetch (void)			{ return m_rom[m_pc++ & m_rommask]; }
	uint8_t sfr_r (uint8_t a);
	void sfr_w (uint8_t a, uint8_t v);
	uint8_t dir_r (uint8_t a)		{ return a < 0x80 ? m_ram[a] : sfr_r (a); }
	uint8_t dir_r_latch (uint8_t a);	// read-modify-write: port latches, not pins
	void dir_w (uint8_t a, uint8_t v)	{ if (a < 0x80) m_ram[a] = v; else sfr_w (a, v); }
	bool bit_r (uint8_t b);
	void bit_w (uint8_t b, bool v);
	uint8_t &reg (int n)			{ return m_ram[(m_sfr[0x50] & 0x18) + n]; }
	void push (uint8_t v)			{ m_sfr[0x01]++; m_ram[m_sfr[0x01] & 0x7f] = v; }
	uint8_t pop (void)			{ uint8_t v = m_ram[m_sfr[0x01] & 0x7f]; m_sfr[0x01]--; return v; }
	void add (uint8_t v, int c);
	void sub (uint8_t v, int c);
	int exec (void);
	void timers (int nCycles);
	bool interrupts (void);

	const uint8_t *m_rom;
	size_t m_rommask;
	TBus m_bus;

	struct TState
	{
		uint8_t ram[128];
		uint8_t sfr[128];		// index = address - 0x80
		uint16_t pc;
		uint8_t in_irq;			// 0 none, 1 low priority, 2 high (bit mask of levels)
		uint8_t irq_stack[2];
		uint8_t int_state[2];		// pin levels
		uint8_t pad;
	} m_s;
	uint8_t *const m_ram = m_s.ram;
	uint8_t *const m_sfr = m_s.sfr;
	uint16_t &m_pc = m_s.pc;
};

#endif
