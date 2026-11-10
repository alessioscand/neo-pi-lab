// TEMPORARY, written by hand: to be generated from MAME's segas16b.cpp
// (tools/gen_s16_tables.py)

static const TS16Rom roms_altbeast[] =
{
	{"epr-11907.a7",  0x29e0c3ad, 0x20000, RGN_MAIN, LOAD_EVEN, 0x000000},
	{"epr-11906.a5",  0x4c9e9cd8, 0x20000, RGN_MAIN, LOAD_ODD,  0x000001},
	{"opr-11674.a14", 0xa57a66d5, 0x20000, RGN_TILES, LOAD_BYTES, 0x00000},
	{"opr-11675.a15", 0x2ef2f144, 0x20000, RGN_TILES, LOAD_BYTES, 0x20000},
	{"opr-11676.a16", 0x0c04acac, 0x20000, RGN_TILES, LOAD_BYTES, 0x40000},
	{"epr-11677.b1",  0xa01425cd, 0x20000, RGN_SPRITES, LOAD_ODD,  0x00001},
	{"epr-11681.b5",  0xd9e03363, 0x20000, RGN_SPRITES, LOAD_EVEN, 0x00000},
	{"epr-11678.b2",  0x17a9fc53, 0x20000, RGN_SPRITES, LOAD_ODD,  0x40001},
	{"epr-11682.b6",  0xe3f77c5e, 0x20000, RGN_SPRITES, LOAD_EVEN, 0x40000},
	{"epr-11679.b3",  0x14dcc245, 0x20000, RGN_SPRITES, LOAD_ODD,  0x80001},
	{"epr-11683.b7",  0xf9a60f06, 0x20000, RGN_SPRITES, LOAD_EVEN, 0x80000},
	{"epr-11680.b4",  0xf43dcdec, 0x20000, RGN_SPRITES, LOAD_ODD,  0xc0001},
	{"epr-11684.b8",  0xb20c0edb, 0x20000, RGN_SPRITES, LOAD_EVEN, 0xc0000},
	{"epr-11671.a10", 0x2b71343b, 0x08000, RGN_SOUND, LOAD_BYTES, 0x00000},
	{"opr-11672.a11", 0xbbd7f460, 0x20000, RGN_SOUND, LOAD_BYTES, 0x10000},
	{"opr-11673.a12", 0x400c4a36, 0x20000, RGN_SOUND, LOAD_BYTES, 0x30000},
	{"317-0078.c2",   0x8101925f, 0x01000, RGN_MCU, LOAD_BYTES, 0x00000},
};

static const TS16Game s_Games[] =
{
	{"altbeast", "", "Altered Beast (set 8) (8751 317-0078)", "1988", "Sega",
	 roms_altbeast, (int) (sizeof roms_altbeast / sizeof roms_altbeast[0]),
	 {0x40000, 0x60000, 0x100000, 0x50000, 0x1000, 0}, BOARD_5521, CPU_68000, true, 0xfdff, 2, 3},
};
