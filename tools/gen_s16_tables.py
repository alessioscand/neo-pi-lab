#!/usr/bin/env python3
"""s16-pi: build core/s16_games.inc from MAME's Sega System 16B and System 18 drivers.

    python3 tools/gen_s16_tables.py <mame>/src/mame/sega [list.txt] > core/s16_games.inc

With a second argument it also writes the names for the game list
(dist/sd/s16pi/list.txt).

Reads segas16b.cpp and segas18.cpp (ROM_START blocks, INPUT_PORTS, GAME
lines) and segaipt.h, and writes plain C++ tables: ROM lists with CRCs, DIP
switches with their settings and defaults, what each input bit is, and the
per-game hardware (ROM board, CPU, 8751, MC8123). The C preprocessor expands
the input port macros.

Left out, with the reason on stderr: bootlegs and the Korean boards, sets MAME
marks as not working, System 16C, the sets with controls a gamepad does not
have (trackball, dial, analog stick, mahjong panel, two sticks, light guns)
and, for now, the games on a vertical monitor.
"""
import os, re, subprocess, sys, tempfile

src = sys.argv[1]
ipt = open(os.path.join(src, 'segaipt.h')).read()

def port_source(drv, first):
    a = drv.index('INPUT_PORTS_START( %s )' % first)
    b = drv.index('ROM_START(', a)
    b = drv.rindex('INPUT_PORTS_END', a, b) + len('INPUT_PORTS_END')
    return drv[a:b].replace('static INPUT_PORTS_START', 'INPUT_PORTS_START')

drv16 = open(os.path.join(src, 'segas16b.cpp')).read()
drv18 = open(os.path.join(src, 'segas18.cpp')).read()
# (the two drivers' input sets have different names but for none in common)
ports_src = port_source(drv16, 'system16b_generic') + '\n' + port_source(drv18, 'system18_generic')
ports_src = re.sub(r'//.*$', '', ports_src, flags=re.M)
ports_src = re.sub(r'/\*.*?\*/', '', ports_src, flags=re.S)
defs = r'''
#define INPUT_PORTS_START(n) @START n
#define INPUT_PORTS_END @END
#define PORT_START(t) @PORT t
#define PORT_MODIFY(t) @PORT t
#define PORT_INCLUDE(n) @INCLUDE n
#define PORT_BIT(m,s,t) @BIT m s t
#define PORT_DIPNAME(m,d,n) @DIP m d n
#define PORT_DIPSETTING(v,n) @SET v n
#define DEF_STR(x) #x
#define PORT_DIPLOCATION(x) @LOC x
#define PORT_CONDITION(a,b,c,d) @COND a b c d
#define PORT_DIPUNKNOWN_DIPLOC(m,d,l) @DIP m d "Unknown" @LOC l @SET d "Off" @SET 0 "On"
#define PORT_DIPUNUSED_DIPLOC(m,d,l) @DIP m m "Unused" @LOC l @SET m "Off" @SET 0 "On"
#define PORT_SERVICE_DIPLOC(m,s,l) @DIP m m "Service_Mode" @LOC l @SET m "Off" @SET 0 "On"
#define PORT_SERVICE_NO_TOGGLE(m,s) @BIT m s IPT_SERVICE
#define PORT_PLAYER(n) @PLAYER n
#define PORT_COCKTAIL @PLAYER 2
#define PORT_8WAY
#define PORT_4WAY
#define PORT_NAME(x)
#define PORT_SENSITIVITY(x)
#define PORT_KEYDELTA(x)
#define PORT_CENTERDELTA(x)
#define PORT_REVERSE
#define PORT_INVERT
#define PORT_WRAPS
#define PORT_POSITIONS(x)
#define PORT_REMAP_TABLE(x)
#define PORT_MINMAX(a,b)
#define PORT_FULL_TURN_COUNT(x)
#define PORT_CODE_INC(x)
#define PORT_CODE_DEC(x)
#define PORT_CUSTOM_MEMBER(...)
#define PORT_CHANGED_MEMBER(...)
#define IP_ACTIVE_LOW LOW
#define IP_ACTIVE_HIGH HIGH
'''
body = re.sub(r'^#(ifndef|define MAME_|endif|pragma).*$', '', ipt, flags=re.M)
with tempfile.NamedTemporaryFile('w', suffix='.c', delete=False) as f:
    f.write(defs + body + '\n' + ports_src)
    tmp = f.name
out = subprocess.run(['cpp', '-P', tmp], capture_output=True, text=True, check=True).stdout
os.unlink(tmp)
toks = out.replace('\n', ' ').split('@')[1:]

inputs = {}
cur = None
for t in toks:
    t = t.strip()
    if not t:
        continue
    kw, _, rest = t.partition(' ')
    rest = rest.strip()
    if kw == 'START':
        cur = {'name': rest, 'items': []}
        inputs[rest] = cur
    elif kw == 'END':
        cur = None
    elif cur is not None:
        cur['items'].append((kw, rest))

def resolve(name):
    """flatten includes; returns {port: {'bits': [[mask, type, player, state, cond]], 'dips': [...]}}

    A new field removes the fields it overlaps, as MAME's ioport_port::insert_field,
    unless both carry conditions that differ."""
    ports = {}
    port = None
    dip = None
    bit = None
    pending = None              # (port, new field) waiting for its condition

    def overlap(x_mask, x_cond, mask, cond):
        return (x_mask & mask) and not (x_cond and cond and x_cond != cond)

    def settle():
        nonlocal pending
        if pending:
            p, d = pending
            if isinstance(d, dict):
                mask, cond = d['mask'], d['cond']
            else:
                mask, cond = d[0], d[4]
            p['dips'] = [x for x in p['dips'] if x is d or not overlap(x['mask'], x['cond'], mask, cond)]
            p['bits'] = [x for x in p['bits'] if x is d or not overlap(x[0], x[4], mask, cond)]
            pending = None

    for kw, rest in inputs[name]['items']:
        if kw not in ('SET', 'LOC', 'COND'):
            settle()
        if kw == 'INCLUDE':
            for pn, p in resolve(rest).items():
                ports[pn] = {'bits': [list(v) for v in p['bits']],
                             'dips': [dict(d, settings=list(d['settings'])) for d in p['dips']]}
        elif kw == 'PORT':
            pn = rest.strip('"')
            port = ports.setdefault(pn, {'bits': [], 'dips': []})
        elif kw == 'BIT':
            m, s_, t = rest.replace(',', ' ').split(None, 2)
            m = int(m, 16)
            bit = [m, t.strip().split()[0], 1, s_, None]
            port['bits'].append(bit)
            pending = (port, bit)
            dip = None
        elif kw == 'PLAYER':
            bit[2] = int(rest.split()[0])
        elif kw == 'DIP':
            m, d, n = re.match(r'(\S+)\s*,?\s*(\S+)\s*,?\s*(.*)', rest.replace(',', ' ', 2)).groups()
            m = int(m.strip(','), 16); d = int(d.strip(','), 16)
            n = n.strip().strip('"')
            dip = {'mask': m, 'default': d, 'name': n, 'settings': [], 'cond': None, 'loc': ''}
            port['dips'].append(dip)
            pending = (port, dip)
        elif kw == 'SET':
            v, n = rest.split(None, 1)
            dip['settings'].append((int(v.strip(','), 16), n.strip().strip(',').strip().strip('"')))
        elif kw == 'LOC':
            dip['loc'] = rest.replace('" "', '').replace('"', '').replace(' ', '')
        elif kw == 'COND':
            p_, b_, c_, d_ = rest.replace(',', ' ').split()
            cond = (p_.strip('"'), int(b_, 16), c_, int(d_, 16))
            if dip is not None:
                dip['cond'] = cond
            else:
                bit[4] = cond
    settle()
    return ports

NICE = {
    'Allow_Continue': 'Allow Continue', 'Demo_Sounds': 'Demo Sounds', 'Flip_Screen': 'Flip Screen',
    'Service_Mode': 'Service Mode', 'Bonus_Life': 'Bonus Life', 'Coin_A': 'Coin A', 'Coin_B': 'Coin B',
    'Very_Easy': 'Very Easy', 'Free_Play': 'Free Play', 'Very_Hard': 'Very Hard',
}
def nice(s):
    s = NICE.get(s, s)
    m = re.fullmatch(r'(\d+)C_(\d+)C', s)
    if m:
        s = '%s Coin%s/%s Credit%s' % (m.group(1), 's' if m.group(1) != '1' else '', m.group(2), 's' if m.group(2) != '1' else '')
    # shortened so that name and value fit on one line of the DIP SWITCHES panel
    s = re.sub(r'^(\d) Coins?/(\d) Credits?, (\d/\d), (\d/\d)$', r'\1/\2 \3 \4', s)
    s = re.sub(r'^(\d) Credits? To Start$', r'\1 To Start', s)
    s = s.replace('/1000K', '/1M').replace('Advertise Sound Interval', 'Advert Sound')
    s = s.replace('Advertise Coin Indication', 'Advert Coin')
    s = re.sub(r'^Free Play \(if Coin ([AB]) too\) or 1/1$', r'Free Play (with \1)', s)
    s = re.sub(r'^(\d) Coins?/(\d) Credits? (\d)/(\d) (\d)/(\d) (\d)/(\d)$', r'\1/\2 \3/\4 \5/\6 \7/\8', s)
    s = re.sub(r'^(\d) Coins?/(\d) Credits? (\d)/(\d) (\d)/(\d)$', r'\1/\2 \3/\4 \5/\6', s)
    s = re.sub(r'^(\d) Coins?/(\d) Credits? (\d)/(\d)$', r'\1/\2 \3/\4', s)
    return s.replace('_', ' ')

def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

# what a bit of the SERVICE, P1, UNUSED and P2 ports is, for the core:
# 0 nothing, 0x40 | bit of the front-end's system byte (starts 0-3, coins 4-7),
# 0x80 | player << 4 | bit of the player's byte (right left down up A B C D)
SYSTEM = {'IPT_START1': 0, 'IPT_START2': 1, 'IPT_START3': 2, 'IPT_START4': 3,
          'IPT_COIN1': 4, 'IPT_COIN2': 5, 'IPT_COIN3': 6, 'IPT_COIN4': 7}
PLAYER = {'IPT_JOYSTICK_RIGHT': 0, 'IPT_JOYSTICK_LEFT': 1, 'IPT_JOYSTICK_DOWN': 2, 'IPT_JOYSTICK_UP': 3,
          'IPT_BUTTON1': 4, 'IPT_BUTTON2': 5, 'IPT_BUTTON3': 6, 'IPT_BUTTON4': 7}
NOTHING = ('IPT_UNUSED', 'IPT_UNKNOWN', 'IPT_SERVICE', 'IPT_SERVICE1', 'IPT_SERVICE2', 'IPT_SERVICE3')
# the core's input ports, by system; then the two DIP switch ports (SW1, SW2)
IO_PORTS = {16: ('SERVICE', 'P1', 'UNUSED', 'P2'), 18: ('P1', 'P2', 'P3', 'SERVICE', 'EXP3', 'EXP4', 'EXSERVICE')}
DIP_PORTS = {16: ('DSW1', 'DSW2'), 18: ('COINAGE', 'DSW')}
NPORTS = 8

def pad_ports(inp):
    """the input ports without Laser Ghost's gun axes (GUNX1...) and MAME's own setting (FAKE):
    the guns are positions read by an A/D converter, which the core emulates on its own"""
    return {k: v for k, v in resolve(inp).items() if not k.startswith(('GUN', 'FAKE'))}

def controls(ports, system):
    """returns (codes[8][8], conds, players, buttons) or the reason it can not be played with a pad

    conds: (port, bit, code, dsw mask, dsw value) for the bits that depend on a switch"""
    for pn in ports:
        if pn not in IO_PORTS[system] + DIP_PORTS[system]:
            return 'port ' + pn
    codes = []
    conds = []
    players = 1
    buttons = 0
    for pi, pn in enumerate(IO_PORTS[system] + ('',) * (NPORTS - len(IO_PORTS[system]))):
        row = [0] * 8
        for mask, t, pl, state, cond in ports.get(pn, {'bits': []})['bits']:
            if state != 'LOW' and t not in NOTHING:
                return 'active high ' + t
            if t in SYSTEM:
                code = 0x40 | SYSTEM[t]
                if t in ('IPT_START3', 'IPT_START4'):
                    players = max(players, SYSTEM[t] + 1)
                elif t == 'IPT_START2':
                    players = max(players, 2)
            elif t in PLAYER:
                code = 0x80 | (pl - 1) << 4 | PLAYER[t]
                players = max(players, pl)
                if PLAYER[t] >= 4:
                    buttons = max(buttons, PLAYER[t] - 3)
            elif t in NOTHING:
                code = 0
            else:
                return t
            for bit in range(8):
                if not mask & (1 << bit):
                    continue
                if cond:
                    cport, cmask, cop, cval = cond
                    if cop != 'EQUALS' or cport not in DIP_PORTS[system]:
                        return 'condition ' + repr(cond)
                    shift = 8 * DIP_PORTS[system].index(cport)
                    conds.append((pi, bit, code, cmask << shift, cval << shift))
                else:
                    row[bit] = code
        codes.append(row)
    return codes, conds, players, buttons

# ---------------------------------------------------------------- ROMs
REGIONS = {'maincpu': 'RGN_MAIN', 'tiles': 'RGN_TILES', 'sprites': 'RGN_SPRITES', 'soundcpu': 'RGN_SOUND',
           'mcu': 'RGN_MCU', 'maincpu:key': 'RGN_KEY', 'soundcpu:key': 'RGN_SOUNDKEY'}
roms = {}
for m in re.finditer(r'ROM_START\(\s*(\w+)\s*\)(.*?)ROM_END', drv16 + drv18, re.S):
    name, body_ = m.group(1), m.group(2)
    regions = []
    reg = None
    last = None
    for line in body_.splitlines():
        line = line.split('//')[0].strip()
        r = re.match(r'ROM_REGION(?:16_BE)?\(\s*(0x[0-9a-fA-F]+)\s*,\s*"([\w:]+)"', line)
        if r:
            reg = {'name': r.group(2), 'size': int(r.group(1), 16), 'loads': []}
            regions.append(reg)
            continue
        r = re.match(r'(ROM_LOAD16_BYTE|ROM_LOAD)\(\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(?:BAD_DUMP\s+)?(?:CRC\((\w+)\)|NO_DUMP)', line)
        if r:
            # [file, CRC, size of the file, 16-bit halves, parts: (offset, from, length)]
            last = [r.group(2), int(r.group(5), 16) if r.group(5) else 0, int(r.group(4), 16),
                    r.group(1) == 'ROM_LOAD16_BYTE', [(int(r.group(3), 16), 0, int(r.group(4), 16))]]
            if r.group(5):
                reg['loads'].append(last)
            elif reg['name'] != 'mcu':      # (a missing 8751 is simulated, as in MAME)
                reg['nodump'] = True
            continue
        r = re.match(r'(ROM_CONTINUE|ROM_RELOAD)\(\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)', line)
        if r:
            if r.group(1) == 'ROM_CONTINUE':
                last[4].append((int(r.group(2), 16), last[2], int(r.group(3), 16)))
                last[2] += int(r.group(3), 16)
            else:
                last[4].append((int(r.group(2), 16), 0, int(r.group(3), 16)))
        elif line.startswith('ROM_'):
            raise SystemExit('ROM macro not handled in %s: %s' % (name, line))
    roms[name] = regions

# ---------------------------------------------------------------- games
MACHINES = {    # machine configuration: CPU, 8751, MC8123, 171-5797 expected
    'system16b': ('CPU_68000', False, False), 'system16b_i8751': ('CPU_68000', True, False),
    'system16b_fd1094': ('CPU_FD1094', False, False), 'system16b_fd1089a': ('CPU_FD1089A', False, False),
    'system16b_fd1089b': ('CPU_FD1089B', False, False), 'system16b_mc8123': ('CPU_68000', False, True),
    'system16b_5797': ('CPU_68000', False, False), 'system16b_i8751_5797': ('CPU_68000', True, False),
    'system16b_fd1094_5797': ('CPU_FD1094', False, False),
    # System 18
    'system18': ('CPU_68000', False, False), 'system18_fd1094': ('CPU_FD1094', False, False),
    'system18_fd1094_i8751': ('CPU_FD1094', True, False), 'system18_i8751': ('CPU_68000', True, False),
    'ddcrew4p': ('CPU_68000', False, False), 'ddcrew4p_fd1094': ('CPU_FD1094', False, False),
    'lghost_fd1094': ('CPU_FD1094', False, False),
}
INITS = {       # driver init: ROM board, what stands for the 8751
    'init_generic_5358_small': ('BOARD_5358_SMALL', None), 'init_generic_5358': ('BOARD_5358', None),
    'init_generic_5521': ('BOARD_5521', None), 'init_generic_5704': ('BOARD_5704', None),
    'init_generic_5797': ('BOARD_5797', None),
    'init_aliensyn7_5358_small': ('BOARD_5358_SMALL', None), 'init_altbeas4_5521': ('BOARD_5521', None),
    'init_shinobi3_5358': ('BOARD_5358', None), 'init_shinobi4_5521': ('BOARD_5521', None),
    'init_tturf_5704': ('BOARD_5704', 'MCU_SIM_TTURF'), 'init_wb3_5704': ('BOARD_5704', 'MCU_SIM_WB3'),
    # System 18 (D. D. Crew's 4-player sets: the 171-5987 with an 8255)
    'init_generic_shad': ('BOARD_S18_SHADOW', None), 'init_generic_5874': ('BOARD_S18_5874', None),
    'init_generic_5987': ('BOARD_S18_5987', None), 'init_ddcrew4p': ('BOARD_S18_5987', None), 'init_lghost': ('BOARD_S18_5987', None),
}
STATES = {'segas16b_state': 16, 'segas18_state': 18}

def title(desc):
    """'Golden Axe (set 6, US) (8751 317-123A)' -> 'Golden Axe (set 6, US)'"""
    name, *groups = re.split(r'\s*\(', desc)
    keep = []
    for g in groups:
        for part in g.rstrip(') ').split(', '):
            if re.match(r'(System 16B|unprotected|FD10|8751|MC-8123|no security|new$|old$)', part):
                continue
            keep.append(part)
    return name + (' (' + ', '.join(keep) + ')' if keep else '')

games = []
skipped = []
for m in re.finditer(r'^GAME\(\s*(\d+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*([^)]*)\)', drv16 + '\n' + drv18, re.M):
    year, name, parent, mcfg, inp, cls, init, rot, manu, desc, flags = m.groups()
    why = None
    system = STATES.get(cls)
    if 'bootleg' in manu.lower() or 'bootleg' in desc.lower():
        why = 'bootleg'
    elif 'NOT_WORKING' in flags:
        why = 'not working in MAME'
    elif not system or mcfg not in MACHINES or init not in INITS:
        why = 'other hardware or controls (%s, %s)' % (mcfg, init)
    elif rot != 'ROT0':
        why = 'vertical monitor'
    elif any(r.get('nodump') for r in roms[name] if r['name'] in REGIONS):
        why = 'a ROM is not dumped'
    else:
        c = controls(pad_ports(inp), system)
        if isinstance(c, str):
            why = 'controls (%s)' % c
    if why:
        skipped.append((name, why))
        continue
    cpu, mcu, mc8123 = MACHINES[mcfg]
    board, sim = INITS[init]
    games.append(dict(year=year, name=name, parent=parent, inp=inp, manu=manu, desc=title(desc), cpu=cpu,
                      mcu=sim or ('MCU_8751' if mcu else 'MCU_NONE'), mc8123=mc8123, board=board,
                      system=system, ppi=init == 'init_ddcrew4p', guns=init == 'init_lghost'))
for name, why in skipped:
    print('left out: %-12s %s' % (name, why), file=sys.stderr)
print('%d games' % len(games), file=sys.stderr)

# ---------------------------------------------------------------- output
o = []
w = o.append
w('// Generated by tools/gen_s16_tables.py from MAME src/mame/sega/segas16b.cpp and segas18.cpp')
w('// (BSD-3-Clause, copyright-holders: Aaron Giles). Do not edit by hand.')
w('')
for inp, system in sorted({(g['inp'], g['system']) for g in games}):
    ports = resolve(inp)
    dips = []
    for pn, shift in zip(DIP_PORTS[system], (0, 8)):
        for d in ports.get(pn, {}).get('dips', []):
            if d['name'] in ('Unused', 'Unknown'):
                continue
            dips.append((shift, d))
    def lockey(item):
        m = re.match(r'SW(\d+):(\d+)', item[1]['loc'])
        return (int(m.group(1)), int(m.group(2))) if m else (9, 9)
    dips.sort(key=lockey)
    w('static const TS16DipSetting dipset_%s[] =\n{' % inp)
    for shift, d in dips:
        for v, n in d['settings']:
            w('\t{0x%04x, %s},' % (v << shift, cstr(nice(n))))
    w('\t{0, 0}\n};')
    w('static const TS16DipDef dips_%s[] =\n{' % inp)
    idx = 0
    for shift, d in dips:
        cond = d['cond']
        if cond:
            raise SystemExit('a condition on a DIP switch (%s): the port it reads must be worked out' % inp)
        w('\t{0x%04x, 0x%04x, %s, %s, %d, %d},' % (
            d['mask'] << shift, d['default'] << shift, cstr(nice(d['name'])), cstr(d['loc']), idx, len(d['settings'])))
        idx += len(d['settings'])
    w('\t{0, 0, 0, 0, 0, 0}\n};')
    dsw = 0xffff
    for pn, shift in zip(DIP_PORTS[system], (0, 8)):
        for d in ports.get(pn, {}).get('dips', []):
            dsw = (dsw & ~(d['mask'] << shift)) | (d['default'] << shift)
    codes, conds, players, buttons = controls(pad_ports(inp), system)
    if conds:
        w('static const TS16CondBit conds_%s[] =\n{' % inp)
        for c in conds:
            w('\t{%d, %d, 0x%02x, 0x%04x, 0x%04x},' % c)
        w('};')
    w('static const TS16Inputs inputs_%s =\n{\n\tdips_%s, dipset_%s, 0x%04x, %d, %d,\n\t{%s},\n\t%s, %d\n};\n' % (
        inp, inp, inp, dsw, players, buttons,
        ',\n\t '.join('{' + ', '.join('0x%02x' % c for c in row) + '}' for row in codes),
        'conds_' + inp if conds else '0', len(conds)))

for g in games:
    w('static const TS16Rom roms_%s[] =\n{' % g['name'])
    n = 0
    for r in roms[g['name']]:
        if r['name'] not in REGIONS:
            continue
        for fn, crc, fsize, b16, parts in r['loads']:
            for off, frm, length in parts:
                whole = len(parts) == 1
                load = 'LOAD_BYTES' if not b16 else ('LOAD_ODD' if off & 1 else 'LOAD_EVEN')
                w('\t{%s, 0x%08x, 0x%05x, %s, %s, 0x%06x, 0x%05x, 0x%05x},' % (
                    cstr(fn), crc, fsize, REGIONS[r['name']], load, off, frm, 0 if whole else length))
                n += 1
    g['nroms'] = n
    w('};')
w('')
w('static const TS16Game s_Games[] =\n{')
for g in games:
    regs = {REGIONS[r['name']]: r['size'] for r in roms[g['name']] if r['name'] in REGIONS}
    order = ('RGN_MAIN', 'RGN_TILES', 'RGN_SPRITES', 'RGN_SOUND', 'RGN_MCU', 'RGN_KEY', 'RGN_SOUNDKEY')
    w('\t{%s, %s, %s, %s, %s,\n\t roms_%s, %d, {%s},\n\t &inputs_%s, %s, %s, %s, %s, %s, %s},' % (
        cstr(g['name']), cstr(g['parent'] if g['parent'] != '0' else ''), cstr(g['desc']), cstr(g['year']), cstr(g['manu']),
        g['name'], g['nroms'], ', '.join('0x%x' % regs.get(k, 0) for k in order),
        g['inp'], g['board'], g['cpu'], g['mcu'], 'true' if g['mc8123'] else 'false', 'true' if g['ppi'] else 'false', 'true' if g['guns'] else 'false'))
w('};')
print('\n'.join(o))

# ---------------------------------------------------------------- list.txt
if len(sys.argv) > 2:
    with open(sys.argv[2], 'w') as f:
        f.write("""# s16-pi - names in the game list
# One line per MAME set: set=Title, or set=Title|Region to choose the text
# shown under the list too. Without |Region, the region comes from MAME's
# name ("Golden Axe (set 6, US)" -> SET 6 US). A set missing here shows
# MAME's full name. Titles longer than 33 characters are cut in the list.
# Written by tools/gen_s16_tables.py; change it as you like.

""")
        for g in sorted(games, key=lambda g: g['name']):
            f.write('%s=%s\n' % (g['name'], g['desc'].split(' (')[0]))
