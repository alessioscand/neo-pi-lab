/*
 * s16-pi - the encrypted CPUs of the System 16B boards
 *
 * Hitachi FD1094 and FD1089A/B (68000) and NEC MC-8123 (Z80): each carries a
 * key in battery-backed RAM and decrypts its program as it runs, opcodes and
 * data in different ways. The keys are the .key files of the MAME romsets.
 *
 * The algorithms and their tables are MAME's (BSD-3-Clause), the code below is
 * taken from it with the least changes:
 *   src/mame/sega/fd1094.cpp, fd1089.cpp - Nicola Salmoria, Andreas Naive,
 *                                          Charles MacDonald
 *   src/devices/cpu/z80/mc8123.cpp       - Nicola Salmoria, David Widel
 * What is around them (which page is decrypted when, and with which state) is
 * in s16.cpp.
 */
#include "s16.h"
#include <string.h>

namespace {

// MAME's helpers
template <typename T, typename U> constexpr T BIT (T x, U n)	{ return (x >> n) & 1; }
template <typename T> constexpr u8 BITS (T b)			{ return (u8) (1u << b); }
template <typename T, typename... U> constexpr u8 BITS (T b, U... c) { return (u8) ((1u << b) | BITS (c...)); }
template <unsigned N, typename T, typename... B> constexpr T bitswap (T val, B... b)
{
	unsigned r = 0;
	((r = (r << 1) | ((val >> b) & 1)), ...);
	return (T) r;
}
typedef u32 offs_t;

// ---- FD1094 ------------------------------------------------------------------

const u16 s_masked_opcodes[] =
{
	0x013a,0x033a,0x053a,0x073a,0x083a,0x093a,0x0b3a,0x0d3a,0x0f3a,

	0x103a,       0x10ba,0x10fa,    0x113a,0x117a,0x11ba,0x11fa,
	0x123a,       0x12ba,0x12fa,    0x133a,0x137a,0x13ba,0x13fa,
	0x143a,       0x14ba,0x14fa,    0x153a,0x157a,0x15ba,
	0x163a,       0x16ba,0x16fa,    0x173a,0x177a,0x17ba,
	0x183a,       0x18ba,0x18fa,    0x193a,0x197a,0x19ba,
	0x1a3a,       0x1aba,0x1afa,    0x1b3a,0x1b7a,0x1bba,
	0x1c3a,       0x1cba,0x1cfa,    0x1d3a,0x1d7a,0x1dba,
	0x1e3a,       0x1eba,0x1efa,    0x1f3a,0x1f7a,0x1fba,

	0x203a,0x207a,0x20ba,0x20fa,    0x213a,0x217a,0x21ba,0x21fa,
	0x223a,0x227a,0x22ba,0x22fa,    0x233a,0x237a,0x23ba,0x23fa,
	0x243a,0x247a,0x24ba,0x24fa,    0x253a,0x257a,0x25ba,
	0x263a,0x267a,0x26ba,0x26fa,    0x273a,0x277a,0x27ba,
	0x283a,0x287a,0x28ba,0x28fa,    0x293a,0x297a,0x29ba,
	0x2a3a,0x2a7a,0x2aba,0x2afa,    0x2b3a,0x2b7a,0x2bba,
	0x2c3a,0x2c7a,0x2cba,0x2cfa,    0x2d3a,0x2d7a,0x2dba,
	0x2e3a,0x2e7a,0x2eba,0x2efa,    0x2f3a,0x2f7a,0x2fba,

	0x303a,0x307a,0x30ba,0x30fa,    0x313a,0x317a,0x31ba,0x31fa,
	0x323a,0x327a,0x32ba,0x32fa,    0x333a,0x337a,0x33ba,0x33fa,
	0x343a,0x347a,0x34ba,0x34fa,    0x353a,0x357a,0x35ba,
	0x363a,0x367a,0x36ba,0x36fa,    0x373a,0x377a,0x37ba,
	0x383a,0x387a,0x38ba,0x38fa,    0x393a,0x397a,0x39ba,
	0x3a3a,0x3a7a,0x3aba,0x3afa,    0x3b3a,0x3b7a,0x3bba,
	0x3c3a,0x3c7a,0x3cba,0x3cfa,    0x3d3a,0x3d7a,0x3dba,
	0x3e3a,0x3e7a,0x3eba,0x3efa,    0x3f3a,0x3f7a,0x3fba,

	0x41ba,0x43ba,0x44fa,0x45ba,0x46fa,0x47ba,0x49ba,0x4bba,0x4cba,0x4cfa,0x4dba,0x4fba,

	0x803a,0x807a,0x80ba,0x80fa,    0x81fa,
	0x823a,0x827a,0x82ba,0x82fa,    0x83fa,
	0x843a,0x847a,0x84ba,0x84fa,    0x85fa,
	0x863a,0x867a,0x86ba,0x86fa,    0x87fa,
	0x883a,0x887a,0x88ba,0x88fa,    0x89fa,
	0x8a3a,0x8a7a,0x8aba,0x8afa,    0x8bfa,
	0x8c3a,0x8c7a,0x8cba,0x8cfa,    0x8dfa,
	0x8e3a,0x8e7a,0x8eba,0x8efa,    0x8ffa,

	0x903a,0x907a,0x90ba,0x90fa,    0x91fa,
	0x923a,0x927a,0x92ba,0x92fa,    0x93fa,
	0x943a,0x947a,0x94ba,0x94fa,    0x95fa,
	0x963a,0x967a,0x96ba,0x96fa,    0x97fa,
	0x983a,0x987a,0x98ba,0x98fa,    0x99fa,
	0x9a3a,0x9a7a,0x9aba,0x9afa,    0x9bfa,
	0x9c3a,0x9c7a,0x9cba,0x9cfa,    0x9dfa,
	0x9e3a,0x9e7a,0x9eba,0x9efa,    0x9ffa,

	0xb03a,0xb07a,0xb0ba,0xb0fa,    0xb1fa,
	0xb23a,0xb27a,0xb2ba,0xb2fa,    0xb3fa,
	0xb43a,0xb47a,0xb4ba,0xb4fa,    0xb5fa,
	0xb63a,0xb67a,0xb6ba,0xb6fa,    0xb7fa,
	0xb83a,0xb87a,0xb8ba,0xb8fa,    0xb9fa,
	0xba3a,0xba7a,0xbaba,0xbafa,    0xbbfa,
	0xbc3a,0xbc7a,0xbcba,0xbcfa,    0xbdfa,
	0xbe3a,0xbe7a,0xbeba,0xbefa,    0xbffa,

	0xc03a,0xc07a,0xc0ba,0xc0fa,    0xc1fa,
	0xc23a,0xc27a,0xc2ba,0xc2fa,    0xc3fa,
	0xc43a,0xc47a,0xc4ba,0xc4fa,    0xc5fa,
	0xc63a,0xc67a,0xc6ba,0xc6fa,    0xc7fa,
	0xc83a,0xc87a,0xc8ba,0xc8fa,    0xc9fa,
	0xca3a,0xca7a,0xcaba,0xcafa,    0xcbfa,
	0xcc3a,0xcc7a,0xccba,0xccfa,    0xcdfa,
	0xce3a,0xce7a,0xceba,0xcefa,    0xcffa,

	0xd03a,0xd07a,0xd0ba,0xd0fa,    0xd1fa,
	0xd23a,0xd27a,0xd2ba,0xd2fa,    0xd3fa,
	0xd43a,0xd47a,0xd4ba,0xd4fa,    0xd5fa,
	0xd63a,0xd67a,0xd6ba,0xd6fa,    0xd7fa,
	0xd83a,0xd87a,0xd8ba,0xd8fa,    0xd9fa,
	0xda3a,0xda7a,0xdaba,0xdafa,    0xdbfa,
	0xdc3a,0xdc7a,0xdcba,0xdcfa,    0xddfa,
	0xde3a,0xde7a,0xdeba,0xdefa,    0xdffa
};

// [0]: the opcodes with PC-relative addressing; [1]: more of them, for the
// addresses where the key says so. One bit per opcode (even ones only).
u8 m_masked_opcodes_lookup[2][65536 / 16];
bool s_bLookup;

void BuildLookup (void)
{
	memset (m_masked_opcodes_lookup, 0, sizeof m_masked_opcodes_lookup);
	for (auto opcode : s_masked_opcodes)
	{
		m_masked_opcodes_lookup[0][opcode >> 4] |= 1 << ((opcode >> 1) & 7);
		m_masked_opcodes_lookup[1][opcode >> 4] |= 1 << ((opcode >> 1) & 7);
	}
	for (int opcode = 0; opcode < 65536; opcode += 2)
		if ((opcode & 0xff80) == 0x4e80 || (opcode & 0xf0f8) == 0x50c8 || (opcode & 0xf000) == 0x6000)
			m_masked_opcodes_lookup[1][opcode >> 4] |= 1 << ((opcode >> 1) & 7);
	s_bLookup = true;
}

// the address is the word address (physical address / 2)
//  (physical address / 2)
//-------------------------------------------------

u16 fd1094_decrypt_one (offs_t address, u16 val, const u8 *main_key, u8 state, bool vector_fetch)
{
	// extract and adjust the global key
	u8 gkey1 = main_key[1];
	u8 gkey2 = main_key[2];
	u8 gkey3 = main_key[3];
	if (state & 0x0001)
	{
		gkey1 ^= 0x04;  // global_xor1
		gkey2 ^= 0x80;  // key_1a invert
		gkey3 ^= 0x80;  // key_2a invert
	}
	if (state & 0x0002)
	{
		gkey1 ^= 0x01;  // global_swap2
		gkey2 ^= 0x10;  // key_7a invert
		gkey3 ^= 0x01;  // key_4b invert
	}
	if (state & 0x0004)
	{
		gkey1 ^= 0x80;  // key_0b invert
		gkey2 ^= 0x40;  // key_6b invert
		gkey3 ^= 0x04;  // global_swap4
	}
	if (state & 0x0008)
	{
		gkey1 ^= 0x20;  // global_xor0
		gkey2 ^= 0x02;  // key_6a invert
		gkey3 ^= 0x20;  // key_5a invert
	}
	if (state & 0x0010)
	{
		gkey1 ^= 0x02;  // key_0c invert
		gkey1 ^= 0x40;  // key_5b invert
		gkey2 ^= 0x08;  // key_4a invert
	}
	if (state & 0x0020)
	{
		gkey1 ^= 0x08;  // key_1b invert
		gkey3 ^= 0x08;  // key_3b invert
		gkey3 ^= 0x10;  // global_swap1
	}
	if (state & 0x0040)
	{
		gkey1 ^= 0x10;  // key_2b invert
		gkey2 ^= 0x20;  // global_swap0a
		gkey2 ^= 0x04;  // global_swap0b
	}
	if (state & 0x0080)
	{
		gkey2 ^= 0x01;  // key_3a invert
		gkey3 ^= 0x02;  // key_0a invert
		gkey3 ^= 0x40;  // global_swap3
	}

	// for address xx0000-xx0006 (but only if >= 000008), use key xx2000-xx2006
	u8 mainkey;
	if ((address & 0x0ffc) == 0 && address >= 4)
		mainkey = main_key[(address & 0x1fff) | 0x1000];
	else
		mainkey = main_key[address & 0x1fff];

	u8 key_F;
	if (address & 0x1000)   key_F = BIT(mainkey,7);
	else                    key_F = BIT(mainkey,6);

	// the CPU has been verified to produce different results when fetching opcodes
	// from 0000-0006 than when fetching the initial SP and PC on reset.
	if (vector_fetch)
	{
		if (address <= 3) gkey3 = 0x00; // supposed to always be the case
		if (address <= 2) gkey2 = 0x00;
		if (address <= 1) gkey1 = 0x00;
		if (address <= 1) key_F = 0;
	}

	u8 global_xor0         = 1^BIT(gkey1,5);
	u8 global_xor1         = 1^BIT(gkey1,2);
	u8 global_swap2        = 1^BIT(gkey1,0);

	u8 global_swap0a       = 1^BIT(gkey2,5);
	u8 global_swap0b       = 1^BIT(gkey2,2);

	u8 global_swap3        = 1^BIT(gkey3,6);
	u8 global_swap1        = 1^BIT(gkey3,4);
	u8 global_swap4        = 1^BIT(gkey3,2);

	u8 key_0a = BIT(mainkey,0) ^ BIT(gkey3,1);
	u8 key_0b = BIT(mainkey,0) ^ BIT(gkey1,7);
	u8 key_0c = BIT(mainkey,0) ^ BIT(gkey1,1);

	u8 key_1a = BIT(mainkey,1) ^ BIT(gkey2,7);
	u8 key_1b = BIT(mainkey,1) ^ BIT(gkey1,3);

	u8 key_2a = BIT(mainkey,2) ^ BIT(gkey3,7);
	u8 key_2b = BIT(mainkey,2) ^ BIT(gkey1,4);

	u8 key_3a = BIT(mainkey,3) ^ BIT(gkey2,0);
	u8 key_3b = BIT(mainkey,3) ^ BIT(gkey3,3);

	u8 key_4a = BIT(mainkey,4) ^ BIT(gkey2,3);
	u8 key_4b = BIT(mainkey,4) ^ BIT(gkey3,0);

	u8 key_5a = BIT(mainkey,5) ^ BIT(gkey3,5);
	u8 key_5b = BIT(mainkey,5) ^ BIT(gkey1,6);

	u8 key_6a = BIT(mainkey,6) ^ BIT(gkey2,1);
	u8 key_6b = BIT(mainkey,6) ^ BIT(gkey2,6);

	u8 key_7a = BIT(mainkey,7) ^ BIT(gkey2,4);


	if (val & 0x8000)           // block invariant: val & 0x8000 != 0
	{
		val = bitswap<16>(val, 15, 9,10,13, 3,12, 0,14, 6, 5, 2,11, 8, 1, 4, 7);

		if (!global_xor1)   if (~val & 0x0800)  val ^= 0x3002;                                      // 1,12,13
		if (true)           if (~val & 0x0020)  val ^= 0x0044;                                      // 2,6
		if (!key_1b)        if (~val & 0x0400)  val ^= 0x0890;                                      // 4,7,11
		if (!global_swap2)  if (!key_0c)        val ^= 0x0308;                                      // 3,8,9
												val ^= 0x6561;

		if (!key_2b)        val = bitswap<16>(val,15,10,13,12,11,14,9,8,7,6,0,4,3,2,1,5);             // 0-5, 10-14
	}

	if (val & 0x4000)           // block invariant: val & 0x4000 != 0
	{
		val = bitswap<16>(val, 13,14, 7, 0, 8, 6, 4, 2, 1,15, 3,11,12,10, 5, 9);

		if (!global_xor0)   if (val & 0x0010)   val ^= 0x0468;                                      // 3,5,6,10
		if (!key_3a)        if (val & 0x0100)   val ^= 0x0081;                                      // 0,7
		if (!key_6a)        if (val & 0x0004)   val ^= 0x0100;                                      // 8
		if (!key_5b)        if (!key_0b)        val ^= 0x3012;                                      // 1,4,12,13
												val ^= 0x3523;

		if (!global_swap0b) val = bitswap<16>(val, 2,14,13,12, 9,10,11, 8, 7, 6, 5, 4, 3,15, 1, 0);   // 2-15, 9-11
	}

	if (val & 0x2000)           // block invariant: val & 0x2000 != 0
	{
		val = bitswap<16>(val, 10, 2,13, 7, 8, 0, 3,14, 6,15, 1,11, 9, 4, 5,12);

		if (!key_4a)        if (val & 0x0800)   val ^= 0x010c;                                      // 2,3,8
		if (!key_1a)        if (val & 0x0080)   val ^= 0x1000;                                      // 12
		if (!key_7a)        if (val & 0x0400)   val ^= 0x0a21;                                      // 0,5,9,11
		if (!key_4b)        if (!key_0a)        val ^= 0x0080;                                      // 7
		if (!global_swap0a) if (!key_6b)        val ^= 0xc000;                                      // 14,15
												val ^= 0x99a5;

		if (!key_5b)        val = bitswap<16>(val,15,14,13,12,11, 1, 9, 8, 7,10, 5, 6, 3, 2, 4, 0);   // 1,4,6,10
	}

	if (val & 0xe000)           // block invariant: val & 0xe000 != 0
	{
		val = bitswap<16>(val,15,13,14, 5, 6, 0, 9,10, 4,11, 1, 2,12, 3, 7, 8);

		val ^= 0x17ff;

		if (!global_swap4)  val = bitswap<16>(val, 15,14,13, 6,11,10, 9, 5, 7,12, 8, 4, 3, 2, 1, 0);  // 5-8, 6-12
		if (!global_swap3)  val = bitswap<16>(val, 13,15,14,12,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);  // 15-14-13
		if (!global_swap2)  val = bitswap<16>(val, 15,14,13,12,11, 2, 9, 8,10, 6, 5, 4, 3, 0, 1, 7);  // 10-2-0-7
		if (!key_3b)        val = bitswap<16>(val, 15,14,13,12,11,10, 4, 8, 7, 6, 5, 9, 1, 2, 3, 0);  // 9-4, 3-1
		if (!key_2a)        val = bitswap<16>(val, 13,14,15,12,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);  // 13-15

		if (!global_swap1)  val = bitswap<16>(val, 15,14,13,12, 9, 8,11,10, 7, 6, 5, 4, 3, 2, 1, 0);  // 11...8
		if (!key_5a)        val = bitswap<16>(val, 15,14,13,12,11,10, 9, 8, 4, 5, 7, 6, 3, 2, 1, 0);  // 7...4
		if (!global_swap0a) val = bitswap<16>(val, 15,14,13,12,11,10, 9, 8, 7, 6, 5, 4, 0, 3, 2, 1);  // 3...0
	}

	val = bitswap<16>(val, 12,15,14,13,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);

	if ((val & 0xb080) == 0x8000) val ^= 0x4000;
	if ((val & 0xf000) == 0xc000) val ^= 0x0080;
	if ((val & 0xb100) == 0x0000) val ^= 0x4000;

	// mask out opcodes doing PC-relative addressing, replace them with FFFF
	if ((m_masked_opcodes_lookup[key_F][val >> 4] >> ((val >> 1) & 7)) & 1)
		val = 0xffff;

	return val;
}

// ---- FD1089 ------------------------------------------------------------------

struct decrypt_parameters
{
	u8 xorval;
	u8 s7,s6,s5,s4,s3,s2,s1,s0;
};

// common base lookup table, shared between A and B variants
const u8 s_basetable_fd1089[0x100] =
{
	0x00,0x1c,0x76,0x6a,0x5e,0x42,0x24,0x38,0x4b,0x67,0xad,0x81,0xe9,0xc5,0x03,0x2f,
	0x45,0x69,0xaf,0x83,0xe7,0xcb,0x01,0x2d,0x02,0x1e,0x78,0x64,0x5c,0x40,0x2a,0x36,
	0x32,0x2e,0x44,0x58,0xe4,0xf8,0x9e,0x82,0x29,0x05,0xcf,0xe3,0x93,0xbf,0x79,0x55,
	0x3f,0x13,0xd5,0xf9,0x85,0xa9,0x63,0x4f,0xb8,0xa4,0xc2,0xde,0x6e,0x72,0x18,0x04,
	0x0c,0x10,0x7a,0x66,0xfc,0xe0,0x86,0x9a,0x47,0x6b,0xa1,0x8d,0xbb,0x97,0x51,0x7d,
	0x17,0x3b,0xfd,0xd1,0xeb,0xc7,0x0d,0x21,0xa0,0xbc,0xda,0xc6,0x50,0x4c,0x26,0x3a,
	0x3e,0x22,0x48,0x54,0x46,0x5a,0x3c,0x20,0x25,0x09,0xc3,0xef,0xc1,0xed,0x2b,0x07,
	0x6d,0x41,0x87,0xab,0x89,0xa5,0x6f,0x43,0x1a,0x06,0x60,0x7c,0x62,0x7e,0x14,0x08,
	0x0a,0x16,0x70,0x6c,0xdc,0xc0,0xaa,0xb6,0x4d,0x61,0xa7,0x8b,0xf7,0xdb,0x11,0x3d,
	0x5b,0x77,0xbd,0x91,0xe1,0xcd,0x0b,0x27,0x80,0x9c,0xf6,0xea,0x56,0x4a,0x2c,0x30,
	0xb0,0xac,0xca,0xd6,0xee,0xf2,0x98,0x84,0x37,0x1b,0xdd,0xf1,0x95,0xb9,0x73,0x5f,
	0x39,0x15,0xdf,0xf3,0x9b,0xb7,0x71,0x5d,0xb2,0xae,0xc4,0xd8,0xec,0xf0,0x96,0x8a,
	0xa8,0xb4,0xd2,0xce,0xd0,0xcc,0xa6,0xba,0x1f,0x33,0xf5,0xd9,0xfb,0xd7,0x1d,0x31,
	0x57,0x7b,0xb1,0x9d,0xb3,0x9f,0x59,0x75,0x8c,0x90,0xfa,0xe6,0xf4,0xe8,0x8e,0x92,
	0x12,0x0e,0x68,0x74,0xe2,0xfe,0x94,0x88,0x65,0x49,0x8f,0xa3,0x99,0xb5,0x7f,0x53,
	0x35,0x19,0xd3,0xff,0xc9,0xe5,0x23,0x0f,0xbe,0xa2,0xc8,0xd4,0x4e,0x52,0x34,0x28,
};

const decrypt_parameters s_addr_params[16] =
{
	{ 0x23, 6,4,5,7,3,0,1,2 },
	{ 0x92, 2,5,3,6,7,1,0,4 },
	{ 0xb8, 6,7,4,2,0,5,1,3 },
	{ 0x74, 5,3,7,1,4,6,0,2 },
	{ 0xcf, 7,4,1,0,6,2,3,5 },
	{ 0xc4, 3,1,6,4,5,0,2,7 },
	{ 0x51, 5,7,2,4,3,1,6,0 },
	{ 0x14, 7,2,0,6,1,3,4,5 },
	{ 0x7f, 3,5,6,0,2,1,7,4 },
	{ 0x03, 2,3,4,0,6,7,5,1 },
	{ 0x96, 3,1,7,5,2,4,6,0 },
	{ 0x30, 7,6,2,3,0,4,5,1 },
	{ 0xe2, 1,0,3,7,4,5,2,6 },
	{ 0x72, 1,6,0,5,7,2,4,3 },
	{ 0xf5, 0,4,1,2,6,5,7,3 },
	{ 0x5b, 0,7,5,3,1,4,2,6 },
};

const decrypt_parameters s_data_params_a[16] =
{
	{ 0x55, 6,5,1,0,7,4,2,3 },
	{ 0x94, 7,6,4,2,0,5,1,3 },
	{ 0x8d, 1,4,2,3,0,6,7,5 },
	{ 0x9a, 4,3,5,6,0,2,1,7 },
	{ 0x72, 4,3,7,0,5,6,1,2 },
	{ 0xff, 1,7,2,3,6,4,5,0 },
	{ 0x06, 6,5,3,2,4,1,0,7 },
	{ 0xc5, 3,5,1,4,2,7,0,6 },
	{ 0xec, 4,7,5,1,6,0,2,3 },
	{ 0x89, 3,5,0,6,1,2,7,4 },
	{ 0x5c, 1,3,0,7,5,2,4,6 },
	{ 0x3f, 7,3,0,2,4,6,1,5 },
	{ 0x57, 6,4,7,2,1,5,3,0 },
	{ 0xf7, 6,3,7,0,5,4,2,1 },
	{ 0x3a, 6,1,3,2,7,4,5,0 },
	{ 0xac, 1,6,3,5,0,7,4,2 },
};

u8 rearrange_key (u8 table, bool opcode)
{
	if (!opcode)
	{
		table ^= (1<<4);
		table ^= (1<<5);

		if (BIT(~table,3))
			table ^= (1<<1);

		table = bitswap<8>(table,1,0,6,4,3,5,2,7);

		if (BIT(table,6))
			table = bitswap<8>(table,7,6,2,4,5,3,1,0);
	}
	else
	{
		table ^= (1<<2);
		table ^= (1<<3);
		table ^= (1<<4);

		if (BIT(~table,3))
			table ^= (1<<5);

		if (BIT(table,7))
			table ^= (1<<6);

		table = bitswap<8>(table,5,7,6,4,2,3,1,0);

		if (BIT(table,6))
			table = bitswap<8>(table,7,6,5,3,2,4,1,0);
	}

	if (BIT(table,6))
	{
		if (BIT(table,5))
			table ^= (1<<4);
	}
	else
	{
		if (BIT(~table,4))
			table ^= (1<<5);
	}

	return table;
}

u8 fd1089a_decode (u8 val, u8 key, bool opcode)
{
	// special case - don't decrypt
	if (key == 0x00)
		return val;

	u8 table = rearrange_key(key, opcode);

	const decrypt_parameters &p = s_addr_params[table >> 4];
	val = bitswap<8>(val, p.s7,p.s6,p.s5,p.s4,p.s3,p.s2,p.s1,p.s0) ^ p.xorval;

	if (BIT(table,3)) val ^= 0x01;
	if (BIT(table,0)) val ^= 0xb1;
	if (opcode) val ^= 0x34;
	if (opcode == 0)
		if (BIT(table,6))
			val ^= 0x01;

	val = s_basetable_fd1089[val];

	u8 family = table & 0x07;
	if (opcode == 0)
	{
		if (BIT(~table,6) & BIT(table,2)) family ^= 8;
		if (BIT(table,4)) family ^= 8;
	}
	else
	{
		if (BIT(table,6) & BIT(table,2)) family ^= 8;
		if (BIT(table,5)) family ^= 8;
	}

	if (BIT(table,0))
	{
		if (BIT(val,0)) val ^= 0xc0;
		if (BIT(~val,6) ^ BIT(val,4))
			val = bitswap<8>(val, 7,6,5,4,1,0,2,3);
	}
	else
	{
		if (BIT(~val,6) ^ BIT(val,4))
			val = bitswap<8>(val, 7,6,5,4,0,1,3,2);
	}
	if (BIT(~val,6))
		val = bitswap<8>(val, 7,6,5,4,2,3,0,1);

	const decrypt_parameters &q = s_data_params_a[family];

	val ^= q.xorval;
	val = bitswap<8>(val, q.s7,q.s6,q.s5,q.s4,q.s3,q.s2,q.s1,q.s0);

	return val;
}

u8 fd1089b_decode (u8 val, u8 key, bool opcode)
{
	// special case - don't decrypt
	if (key == 0x00)
		return val;

	u8 table = rearrange_key(key, opcode);

	const decrypt_parameters &p = s_addr_params[table >> 4];
	val = bitswap<8>(val, p.s7,p.s6,p.s5,p.s4,p.s3,p.s2,p.s1,p.s0) ^ p.xorval;

	if (BIT(table,3)) val ^= 0x01;
	if (BIT(table,0)) val ^= 0xb1;
	if (opcode) val ^= 0x34;
	if (opcode == 0)
		if (BIT(table,6))
			val ^= 0x01;

	val = s_basetable_fd1089[val];

	u8 xorval = 0;
	if (opcode == 0)
	{
		if (BIT(~table,6) & BIT(table,2)) xorval ^= 0x01;
		if (BIT(table,4)) xorval ^= 0x01;
	}
	else
	{
		if (BIT(table,6) & BIT(table,2)) xorval ^= 0x01;
		if (BIT(table,5)) xorval ^= 0x01;
	}

	val ^= xorval;

	if (BIT(table,2))
	{
		val = bitswap<8>(val, 7,6,5,4,1,0,3,2);

		if (BIT(table,0) ^ BIT(table,1))
			val = bitswap<8>(val, 7,6,5,4,0,1,3,2);
	}
	else
	{
		val = bitswap<8>(val, 7,6,5,4,3,2,0,1);

		if (BIT(table,0) ^ BIT(table,1))
			val = bitswap<8>(val, 7,6,5,4,1,0,2,3);
	}

	return val;
}

u16 fd1089_decrypt_one (bool b, offs_t addr, u16 val, const u8 *key, bool opcode)
{
	// pick the translation table from bits ff022a of the address
	int tbl_num =   ((addr & 0x000002) >> 1) |
					((addr & 0x000008) >> 2) |
					((addr & 0x000020) >> 3) |
					((addr & 0x000200) >> 6) |
					((addr & 0xff0000) >> 12);

	u16 src =  ((val & 0x0008) >> 3) |
					((val & 0x0040) >> 5) |
					((val & 0xfc00) >> 8);

	src = (b ? fd1089b_decode : fd1089a_decode) ((u8) src, key[tbl_num + (opcode ? 0 : 1) * 0x1000], opcode);

	src =   ((src & 0x01) << 3) |
			((src & 0x02) << 5) |
			((src & 0xfc) << 8);

	return (val & ~0xfc48) | src;
}

// ---- MC-8123 -----------------------------------------------------------------

u8 mc8123_decrypt_type0(u8 val, u8 param, unsigned swap)
{
	if (swap == 0) val = bitswap<8>(val,7,5,3,1,2,0,6,4);
	if (swap == 1) val = bitswap<8>(val,5,3,7,2,1,0,4,6);
	if (swap == 2) val = bitswap<8>(val,0,3,4,6,7,1,5,2);
	if (swap == 3) val = bitswap<8>(val,0,7,3,2,6,4,1,5);

	if (BIT(param,3) && BIT(val,7))
		val ^= BITS(5,3,0);

	if (BIT(param,2) && BIT(val,6))
		val ^= BITS(7,2,1);

	if (BIT(val,6)) val ^= BITS(7);

	if (BIT(param,1) && BIT(val,7))
		val ^= BITS(6);

	if (BIT(val,2)) val ^= BITS(5,0);

	val ^= BITS(4,3,1);

	if (BIT(param,2)) val ^= BITS(5,2,0);
	if (BIT(param,1)) val ^= BITS(7,6);
	if (BIT(param,0)) val ^= BITS(5,0);

	if (BIT(param,0)) val = bitswap<8>(val,7,6,5,1,4,3,2,0);

	return val;
}

u8 mc8123_decrypt_type1a(u8 val, u8 param, unsigned swap)
{
	if (swap == 0) val = bitswap<8>(val,4,2,6,5,3,7,1,0);
	if (swap == 1) val = bitswap<8>(val,6,0,5,4,3,2,1,7);
	if (swap == 2) val = bitswap<8>(val,2,3,6,1,4,0,7,5);
	if (swap == 3) val = bitswap<8>(val,6,5,1,3,2,7,0,4);

	if (BIT(param,2)) val = bitswap<8>(val,7,6,1,5,3,2,4,0);

	if (BIT(val,1)) val ^= BITS(0);
	if (BIT(val,6)) val ^= BITS(3);
	if (BIT(val,7)) val ^= BITS(6,3);
	if (BIT(val,2)) val ^= BITS(6,3,1);
	if (BIT(val,4)) val ^= BITS(7,6,2);

	if (BIT(val,7) ^ BIT(val,2))
		val ^= BITS(4);

	val ^= BITS(6,3,1,0);

	if (BIT(param,3)) val ^= BITS(7,2);
	if (BIT(param,1)) val ^= BITS(6,3);

	if (BIT(param,0)) val = bitswap<8>(val,7,6,1,4,3,2,5,0);

	return val;
}

u8 mc8123_decrypt_type1b(u8 val, u8 param, unsigned swap)
{
	if (swap == 0) val = bitswap<8>(val,1,0,3,2,5,6,4,7);
	if (swap == 1) val = bitswap<8>(val,2,0,5,1,7,4,6,3);
	if (swap == 2) val = bitswap<8>(val,6,4,7,2,0,5,1,3);
	if (swap == 3) val = bitswap<8>(val,7,1,3,6,0,2,5,4);

	if (BIT(val,2) && BIT(val,0))
		val ^= BITS(7,4);

	if (BIT(val,7)) val ^= BITS(2);
	if (BIT(val,5)) val ^= BITS(7,2);
	if (BIT(val,1)) val ^= BITS(5);
	if (BIT(val,6)) val ^= BITS(1);
	if (BIT(val,4)) val ^= BITS(6,5);
	if (BIT(val,0)) val ^= BITS(6,2,1);
	if (BIT(val,3)) val ^= BITS(7,6,2,1,0);

	val ^= BITS(6,4,0);

	if (BIT(param,3)) val ^= BITS(4,1);
	if (BIT(param,2)) val ^= BITS(7,6,3,0);
	if (BIT(param,1)) val ^= BITS(4,3);
	if (BIT(param,0)) val ^= BITS(6,2,1,0);

	return val;
}

u8 mc8123_decrypt_type2a(u8 val, u8 param, unsigned swap)
{
	if (swap == 0) val = bitswap<8>(val,0,1,4,3,5,6,2,7);
	if (swap == 1) val = bitswap<8>(val,6,3,0,5,7,4,1,2);
	if (swap == 2) val = bitswap<8>(val,1,6,4,5,0,3,7,2);
	if (swap == 3) val = bitswap<8>(val,4,6,7,5,2,3,1,0);

	if (BIT(val,3) || (BIT(param,1) && BIT(val,2)))
		val = bitswap<8>(val,6,0,7,4,3,2,1,5);

	if (BIT(val,5)) val ^= BITS(7);
	if (BIT(val,6)) val ^= BITS(5);
	if (BIT(val,0)) val ^= BITS(6);
	if (BIT(val,4)) val ^= BITS(3,0);
	if (BIT(val,1)) val ^= BITS(2);

	val ^= BITS(7,6,5,4,1);

	if (BIT(param,2)) val ^= BITS(4,3,2,1,0);

	if (BIT(param,3))
	{
		if (BIT(param,0))
			val = bitswap<8>(val,7,6,5,3,4,1,2,0);
		else
			val = bitswap<8>(val,7,6,5,1,2,4,3,0);
	}
	else if (BIT(param,0))
	{
		val = bitswap<8>(val,7,6,5,2,1,3,4,0);
	}

	return val;
}

u8 mc8123_decrypt_type2b(u8 val, u8 param, unsigned swap)
{
	// only 0x20 possible encryptions for this method - all others have 0x40
	// this happens because BIT(param,2) cancels the other three

	if (swap == 0) val = bitswap<8>(val,1,3,4,6,5,7,0,2);
	if (swap == 1) val = bitswap<8>(val,0,1,5,4,7,3,2,6);
	if (swap == 2) val = bitswap<8>(val,3,5,4,1,6,2,0,7);
	if (swap == 3) val = bitswap<8>(val,5,2,3,0,4,7,6,1);

	if (BIT(val,7) && BIT(val,3))
		val ^= BITS(6,4,0);

	if (BIT(val,7)) val ^= BITS(2);
	if (BIT(val,5)) val ^= BITS(7,3);
	if (BIT(val,1)) val ^= BITS(5);
	if (BIT(val,4)) val ^= BITS(7,5,3,1);

	if (BIT(val,7) && BIT(val,5))
		val ^= BITS(4,0);

	if (BIT(val,5) && BIT(val,1))
		val ^= BITS(4,0);

	if (BIT(val,6)) val ^= BITS(7,5);
	if (BIT(val,3)) val ^= BITS(7,6,5,1);
	if (BIT(val,2)) val ^= BITS(3,1);

	val ^= BITS(7,3,2,1);

	if (BIT(param,3)) val ^= BITS(6,3,1);
	if (BIT(param,2)) val ^= BITS(7,6,5,3,2,1); // same as the other three combined
	if (BIT(param,1)) val ^= BITS(7);
	if (BIT(param,0)) val ^= BITS(5,2);

	return val;
}

u8 mc8123_decrypt_type3a(u8 val, u8 param, unsigned swap)
{
	if (swap == 0) val = bitswap<8>(val,5,3,1,7,0,2,6,4);
	if (swap == 1) val = bitswap<8>(val,3,1,2,5,4,7,0,6);
	if (swap == 2) val = bitswap<8>(val,5,6,1,2,7,0,4,3);
	if (swap == 3) val = bitswap<8>(val,5,6,7,0,4,2,1,3);

	if (BIT(val,2)) val ^= BITS(7,5,4);
	if (BIT(val,3)) val ^= BITS(0);

	if (BIT(param,0)) val = bitswap<8>(val,7,2,5,4,3,1,0,6);

	if (BIT(val,1)) val ^= BITS(6,0);
	if (BIT(val,3)) val ^= BITS(4,2,1);

	if (BIT(param,3)) val ^= BITS(4,3);

	if (BIT(val,3)) val = bitswap<8>(val,5,6,7,4,3,2,1,0);

	if (BIT(val,5)) val ^= BITS(2,1);

	val ^= BITS(6,5,4,3);

	if (BIT(param,2)) val ^= BITS(7);
	if (BIT(param,1)) val ^= BITS(4);
	if (BIT(param,0)) val ^= BITS(0);

	return val;
}

u8 mc8123_decrypt_type3b(u8 val, u8 param, unsigned swap)
{
	if (swap == 0) val = bitswap<8>(val,3,7,5,4,0,6,2,1);
	if (swap == 1) val = bitswap<8>(val,7,5,4,6,1,2,0,3);
	if (swap == 2) val = bitswap<8>(val,7,4,3,0,5,1,6,2);
	if (swap == 3) val = bitswap<8>(val,2,6,4,1,3,7,0,5);

	if (BIT(val,2)) val ^= BITS(7);

	if (BIT(val,7)) val = bitswap<8>(val,7,6,3,4,5,2,1,0);

	if (BIT(param,3)) val ^= BITS(7);

	if (BIT(val,4)) val ^= BITS(6);
	if (BIT(val,1)) val ^= BITS(6,4,2);

	if (BIT(val,7) && BIT(val,6))
		val ^= BITS(1);

	if (BIT(val,7)) val ^= BITS(1);

	if (BIT(param,3)) val ^= BITS(7);
	if (BIT(param,2)) val ^= BITS(0);

	if (BIT(param,3)) val = bitswap<8>(val,4,6,3,2,5,0,1,7);

	if (BIT(val,4)) val ^= BITS(1);
	if (BIT(val,5)) val ^= BITS(4);
	if (BIT(val,7)) val ^= BITS(2);

	val ^= BITS(5,3,2);

	if (BIT(param,1)) val ^= BITS(7);
	if (BIT(param,0)) val ^= BITS(3);

	return val;
}

u8 mc8123_decrypt_internal(u8 val, u8 key, bool opcode)
{
	unsigned type = 0;
	unsigned swap = 0;
	u8 param = 0;

	key ^= 0xff;

	// no encryption
	if (key == 0x00)
		return val;

	type ^= BIT(key,0) << 0;
	type ^= BIT(key,2) << 0;
	type ^= BIT(key,0) << 1;
	type ^= BIT(key,1) << 1;
	type ^= BIT(key,2) << 1;
	type ^= BIT(key,4) << 1;
	type ^= BIT(key,4) << 2;
	type ^= BIT(key,5) << 2;

	swap ^= BIT(key,0) << 0;
	swap ^= BIT(key,1) << 0;
	swap ^= BIT(key,2) << 1;
	swap ^= BIT(key,3) << 1;

	param ^= BIT(key,0) << 0;
	param ^= BIT(key,0) << 1;
	param ^= BIT(key,2) << 1;
	param ^= BIT(key,3) << 1;
	param ^= BIT(key,0) << 2;
	param ^= BIT(key,1) << 2;
	param ^= BIT(key,6) << 2;
	param ^= BIT(key,1) << 3;
	param ^= BIT(key,6) << 3;
	param ^= BIT(key,7) << 3;

	if (!opcode)
	{
		param ^= BITS(0);
		type ^= BITS(0);
	}

	switch (type)
	{
		default:
		case 0: return mc8123_decrypt_type0(val, param, swap);
		case 1: return mc8123_decrypt_type0(val, param, swap);
		case 2: return mc8123_decrypt_type1a(val, param, swap);
		case 3: return mc8123_decrypt_type1b(val, param, swap);
		case 4: return mc8123_decrypt_type2a(val, param, swap);
		case 5: return mc8123_decrypt_type2b(val, param, swap);
		case 6: return mc8123_decrypt_type3a(val, param, swap);
		case 7: return mc8123_decrypt_type3b(val, param, swap);
	}
}

u8 mc8123_decrypt(const u8 *key, offs_t addr, u8 val, bool opcode)
{
	// pick the translation table from bits fd57 of the address
	offs_t const tbl_num = bitswap<12>(addr,15,14,13,12,11,10,8,6,4,2,1,0);

	return mc8123_decrypt_internal(val, key[tbl_num | (opcode ? 0x0000 : 0x1000)], opcode);
}

}	// namespace

// ---- what s16.cpp calls --------------------------------------------------------

// The opcodes of nBytes of program for one state of the chip; nAddress is where
// the 68000 sees them. Words are big endian, as in the ROM.
void s16_fd1094_decrypt (const u8 *pKey, u8 nState, u32 nAddress, const u8 *pSrc, u8 *pDst, u32 nBytes)
{
	if (!s_bLookup)
	{
		BuildLookup ();
	}
	for (u32 i = 0; i < nBytes; i += 2)
	{
		const u32 a = nAddress + i;
		const u16 v = fd1094_decrypt_one (a / 2, (u16) (pSrc[i] << 8 | pSrc[i + 1]), pKey, nState, a < 8);
		pDst[i] = (u8) (v >> 8);
		pDst[i + 1] = (u8) v;
	}
}

// the state the chip is in while it serves an interrupt
u8 s16_fd1094_irq_state (const u8 *pKey)	{ return pKey[0]; }

// The same program as the 68000 fetches it (pOpcodes) and as it reads it
// (pData); bB: FD1089B, else FD1089A.
void s16_fd1089_decrypt (bool bB, const u8 *pKey, u32 nAddress, const u8 *pSrc, u8 *pOpcodes, u8 *pData, u32 nBytes)
{
	for (u32 i = 0; i < nBytes; i += 2)
	{
		const u16 nSrc = (u16) (pSrc[i] << 8 | pSrc[i + 1]);
		const u16 o = fd1089_decrypt_one (bB, nAddress + i, nSrc, pKey, true);
		const u16 d = fd1089_decrypt_one (bB, nAddress + i, nSrc, pKey, false);
		pOpcodes[i] = (u8) (o >> 8);	pOpcodes[i + 1] = (u8) o;
		pData[i] = (u8) (d >> 8);	pData[i + 1] = (u8) d;
	}
}

// The Z80 program: pRom becomes what the CPU reads as data, pOpcodes what it
// fetches as opcodes.
void s16_mc8123_decode (const u8 *pKey, u8 *pRom, u8 *pOpcodes, u32 nLength)
{
	for (u32 i = 0; i < nLength; i++)
	{
		const u32 nAdr = (i >= 0xc000) ? ((i & 0x3fff) | 0x8000) : i;
		const u8 nSrc = pRom[i];
		pOpcodes[i] = mc8123_decrypt (pKey, nAdr, nSrc, true);
		pRom[i] = mc8123_decrypt (pKey, nAdr, nSrc, false);
	}
}
