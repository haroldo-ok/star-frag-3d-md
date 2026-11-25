"""Sound: Brutal Doom effects -> 13.3 kHz PCM WAVs, and music -> VGM (YM2612 FM +
SN76489 PSG + YM2612 DAC drums) for SGDK's XGM2 driver.

Music sources: four Brutal Doom deathmatch MIDIs (D_DM05/06/07/10), and the
three Star Frag themes (title, arena, win) re-orchestrated for FM + PSG + DAC.
"""
import os, math, struct, subprocess, random, wave, tempfile
from common import *

BD = os.environ.get('BDPK3', '/home/claude/untrusted/bd/pk3')
RATE = 13300

# ---------------------------------------------------------------- effects
SFX = [  # enum name, source (relative to the pk3), max seconds, gain
    ('PISTOL', 'SOUNDS/COMBAT/WEAPONS/DSPISTOL.ogg', 0.40, 1.0),
    ('SHOTGUN', 'SOUNDS/COMBAT/WEAPONS/SGFIRE.ogg', 0.60, 1.0),
    ('SSG', 'SOUNDS/COMBAT/WEAPONS/SSHFIRE.ogg', 0.70, 1.0),
    ('RIFLE', 'SOUNDS/COMBAT/WEAPONS/DSRIFLE.ogg', 0.28, 1.0),
    ('MINIGUN', 'SOUNDS/COMBAT/WEAPONS/MINIGN1.ogg', 0.20, 1.0),
    ('GREN', 'SOUNDS/COMBAT/WEAPONS/GLAUNCH.ogg', 0.45, 1.0),
    ('ROCKET', 'SOUNDS/COMBAT/WEAPONS/RLFIRE.ogg', 0.60, 1.0),
    ('PLASMA', 'SOUNDS/COMBAT/WEAPONS/plsm9.ogg', 0.35, 1.0),
    ('FLAME', 'SOUNDS/COMBAT/WEAPONS/FLAMER', 0.30, 1.0),
    ('RAIL', 'SOUNDS/COMBAT/WEAPONS/RAILF1', 0.80, 1.0),
    ('BFG', 'SOUNDS/COMBAT/WEAPONS/BFGFIRE.ogg', 0.90, 1.0),
    ('EXPLODE', 'SOUNDS/COMBAT/EXPLOSIONS/Explode1.ogg', 0.90, 1.0),
    ('PICK', 'SOUNDS/ITEMS/DSITEMUP', 0.30, 1.0),
    ('WEAPON', 'SOUNDS/HD/DSWPNUP', 0.45, 1.0),
    ('HURT', 'SOUNDS/DOOMGUY/PAIN1', 0.40, 1.0),
    ('DIE', 'SOUNDS/GORE/DSXDTH1A', 0.80, 1.0),
    ('RESPAWN', 'SOUNDS/HD/DSTELEPT', 0.60, 1.0),
    ('POWER', 'SOUNDS/DSGETPOW', 0.70, 1.0),
    ('MENU', 'SOUNDS/HD/DSPSTOP', 0.15, 1.0),
    ('BOUNCE', 'SOUNDS/COMBAT/WEAPONS/GRNBNCE', 0.15, 0.8),
    ('PUFF', 'SOUNDS/COMBAT/RICOCHE1', 0.20, 0.7),
    ('ARMOR', 'SOUNDS/ITEMS/ARMOR.wav', 0.40, 1.0),
    ('GIB', 'SOUNDS/GORE/DSXDTH2A.ogg', 0.80, 1.0),
    ('EMPTY', 'SOUNDS/COMBAT/WEAPONS/DRYFIRE', 0.14, 1.0),
    ('SELECT', 'SOUNDS/HD/DSSWTCHN', 0.25, 1.0),
]

def find_src(rel):
    p = os.path.join(BD, rel)
    if os.path.exists(p):
        return p
    d, b = os.path.split(p)
    for f in os.listdir(d):
        if os.path.splitext(f)[0].upper() == b.upper():
            return os.path.join(d, f)
    raise FileNotFoundError(rel)

def decode(path):
    """-> list of float samples at RATE (mono)"""
    data = open(path, 'rb').read()
    tmp = None
    if data[:2] == b'\x03\x00' and data[:4] != b'RIFF':
        rate, n = struct.unpack_from('<HI', data, 2)
        pcm = data[8 + 16: 8 + n - 16]
        tmp = tempfile.NamedTemporaryFile(suffix='.wav', delete=False)
        w = wave.open(tmp.name, 'wb')
        w.setnchannels(1); w.setsampwidth(1); w.setframerate(rate)
        w.writeframes(pcm)
        w.close()
        path = tmp.name
    out = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-ac', '1', '-ar', str(RATE),
                          '-af', 'highpass=f=40', '-f', 's16le', '-'], capture_output=True, check=True).stdout
    if tmp:
        os.unlink(tmp.name)
    n = len(out) // 2
    return [v / 32768.0 for v in struct.unpack('<%dh' % n, out)]

def finish(sm, maxs, gain):
    # trim leading silence, cut, fade, normalise with soft clipping
    i = 0
    while i < len(sm) and abs(sm[i]) < 0.02:
        i += 1
    sm = sm[i:i + int(maxs * RATE)]
    fade = min(len(sm), int(0.06 * RATE))
    for k in range(fade):
        sm[len(sm) - fade + k] *= 1 - k / fade
    peak = max(1e-6, max(abs(v) for v in sm))
    g = gain / peak * 1.6
    return [math.tanh(v * g) / math.tanh(1.6) for v in sm]

def write_wav8(path, sm):
    w = wave.open(path, 'wb')
    w.setnchannels(1); w.setsampwidth(1); w.setframerate(RATE)
    w.writeframes(bytes(max(0, min(255, int(round(128 + v * 120)))) for v in sm))
    w.close()

def gen_sfx():
    os.makedirs(os.path.join(RES, 'sfx'), exist_ok=True)
    res = ''
    total = 0
    for name, rel, maxs, gain in SFX:
        sm = finish(decode(find_src(rel)), maxs, gain)
        write_wav8(os.path.join(RES, 'sfx', name.lower() + '.wav'), sm)
        res += 'WAV sfx_%s "sfx/%s.wav" XGM2\n' % (name.lower(), name.lower())
        total += len(sm)
    return res, total

# ---------------------------------------------------------------- drum samples (synthesised)
def drum(kind):
    rnd = random.Random(kind)
    n = {'kick': 0.28, 'snare': 0.22, 'tom': 0.26, 'crash': 0.55}[kind]
    out = []
    ph = 0.0
    lp = 0.0
    for i in range(int(n * RATE)):
        t = i / RATE
        if kind == 'kick':
            f = 45 + 110 * math.exp(-t * 30)
            ph += 2 * math.pi * f / RATE
            v = math.sin(ph) * math.exp(-t * 9) + (rnd.uniform(-1, 1) * math.exp(-t * 300) * 0.5)
        elif kind == 'snare':
            f = 190 * (1 + 0.3 * math.exp(-t * 40))
            ph += 2 * math.pi * f / RATE
            nz = rnd.uniform(-1, 1)
            lp += (nz - lp) * 0.6
            v = 0.55 * math.sin(ph) * math.exp(-t * 25) + 0.9 * (nz - lp * 0.5) * math.exp(-t * 14)
        elif kind == 'tom':
            f = 95 + 90 * math.exp(-t * 14)
            ph += 2 * math.pi * f / RATE
            v = math.sin(ph) * math.exp(-t * 8)
        else:
            nz = rnd.uniform(-1, 1)
            lp += (nz - lp) * 0.35
            v = (nz - lp) * math.exp(-t * 5.5) * 1.3
        out.append(v)
    peak = max(abs(v) for v in out)
    return [math.tanh(1.4 * v / peak) / math.tanh(1.4) for v in out]

DRUMS = ['kick', 'snare', 'tom', 'crash']

# ---------------------------------------------------------------- FM patches
# op: (DT, MUL, TL, KS, AR, AM, DR, SR, SL, RR) in logical order OP1..OP4
PATCHES = {
 'dist':  (0, 7, [(3, 1, 30, 0, 31, 0, 10, 2, 2, 7), (0, 3, 40, 0, 31, 0, 8, 2, 3, 7),
                  (0, 1, 24, 0, 31, 0, 6, 1, 2, 7), (0, 1, 0, 0, 31, 0, 3, 2, 2, 8)]),
 'mute':  (0, 6, [(3, 1, 28, 0, 31, 0, 14, 6, 6, 9), (0, 3, 40, 0, 31, 0, 12, 6, 6, 9),
                  (0, 1, 26, 0, 31, 0, 12, 6, 6, 9), (0, 1, 0, 0, 31, 0, 12, 8, 8, 12)]),
 'bass':  (0, 5, [(0, 0, 28, 1, 31, 0, 12, 4, 4, 9), (0, 1, 38, 1, 31, 0, 10, 4, 4, 9),
                  (0, 0, 32, 1, 31, 0, 10, 3, 4, 9), (0, 1, 0, 1, 31, 0, 10, 4, 3, 10)]),
 'strings': (4, 3, [(0, 1, 30, 0, 18, 0, 4, 1, 2, 6), (0, 1, 2, 0, 14, 0, 2, 1, 1, 6),
                    (1, 2, 36, 0, 18, 0, 4, 1, 2, 6), (3, 1, 4, 0, 14, 0, 2, 1, 1, 6)]),
 'brass': (4, 5, [(0, 1, 22, 0, 24, 0, 6, 2, 2, 7), (0, 1, 0, 0, 22, 0, 5, 2, 2, 8),
                  (1, 1, 28, 0, 26, 0, 6, 2, 2, 7), (3, 1, 3, 0, 22, 0, 5, 2, 2, 8)]),
 'lead':  (4, 6, [(0, 2, 30, 0, 31, 0, 5, 2, 2, 7), (0, 1, 2, 0, 31, 0, 4, 1, 1, 8),
                  (1, 1, 26, 0, 31, 0, 5, 2, 2, 7), (3, 2, 10, 0, 31, 0, 4, 1, 1, 8)]),
 'organ': (7, 0, [(0, 1, 6, 0, 31, 0, 0, 0, 0, 8), (0, 2, 12, 0, 31, 0, 0, 0, 0, 8),
                  (0, 4, 18, 0, 31, 0, 0, 0, 0, 8), (0, 8, 26, 0, 31, 0, 0, 0, 0, 8)]),
 'piano': (4, 2, [(0, 1, 32, 1, 31, 0, 9, 3, 4, 7), (0, 1, 2, 1, 31, 0, 7, 3, 5, 8),
                  (0, 7, 44, 1, 31, 0, 11, 4, 4, 7), (0, 1, 6, 1, 31, 0, 8, 3, 5, 8)]),
 'pluck': (2, 4, [(0, 1, 34, 1, 31, 0, 12, 4, 5, 8), (0, 3, 40, 1, 31, 0, 12, 4, 5, 8),
                  (0, 1, 30, 1, 31, 0, 10, 4, 5, 8), (0, 1, 0, 1, 31, 0, 9, 5, 6, 9)]),
 'bell':  (4, 3, [(0, 7, 32, 0, 31, 0, 8, 3, 5, 6), (0, 1, 4, 0, 31, 0, 7, 3, 5, 6),
                  (0, 3, 36, 0, 31, 0, 8, 3, 5, 6), (0, 2, 8, 0, 31, 0, 7, 3, 5, 6)]),
}
CARRIERS = {0: [3], 1: [3], 2: [3], 3: [3], 4: [1, 3], 5: [1, 2, 3], 6: [1, 2, 3], 7: [0, 1, 2, 3]}
SLOT = [0, 8, 4, 12]   # register offset of logical OP1..OP4

def patch_for(prog):
    if prog < 8: return 'piano'
    if prog < 16: return 'bell'
    if prog < 24: return 'organ'
    if prog < 28: return 'pluck'
    if prog == 28: return 'mute'
    if prog < 32: return 'dist'
    if prog < 40: return 'bass'
    if prog < 56: return 'strings'
    if prog < 64: return 'brass'
    if prog < 80: return 'lead'
    if prog < 88: return 'lead'
    if prog < 96: return 'strings'
    return 'lead'

FM_CLOCK = 7670453
PSG_CLOCK = 3579545

def fm_freq(note):
    f = 440.0 * 2 ** ((note - 69) / 12.0)
    for block in range(8):
        fn = f * 144 * (1 << 20) / FM_CLOCK / (1 << max(0, block - 1)) if block else f * 144 * (1 << 21) / FM_CLOCK
        if fn < 0x800:
            if block == 0 and fn < 0x400:
                return block, int(round(fn))
            if fn >= 0x400 or block == 7:
                return block, int(round(fn))
    return 7, 0x7FF

def fm_fnum(note):
    """block/fnum so that fnum is in the upper half of its range (best resolution)"""
    f = 440.0 * 2 ** ((note - 69) / 12.0)
    for block in range(8):
        fn = f * 144 * (1 << 20) / FM_CLOCK / (1 << block) * 2
        if fn < 0x800:
            return block, max(1, int(round(fn)))
    return 7, 0x7FF

def psg_period(note):
    f = 440.0 * 2 ** ((note - 69) / 12.0)
    return max(1, min(1023, int(round(PSG_CLOCK / (32 * f)))))

# ---------------------------------------------------------------- VGM writer
class Vgm:
    def __init__(self):
        self.cmd = bytearray()
        self.samples = 0
        self.loop_ofs = None
        self.loop_samples = 0
        self.blocks = []

    def ym(self, port, reg, val):
        self.cmd += bytes([0x52 + port, reg & 0xFF, val & 0xFF])

    def psg(self, val):
        self.cmd += bytes([0x50, val & 0xFF])

    def wait(self, n):
        self.samples += n
        while n > 0:
            if n == 735:
                self.cmd += b'\x62'; n = 0
            elif n == 882:
                self.cmd += b'\x63'; n = 0
            else:
                k = min(n, 65535)
                self.cmd += bytes([0x61]) + struct.pack('<H', k)
                n -= k

    def data_block(self, pcm8):
        self.cmd += bytes([0x67, 0x66, 0x00]) + struct.pack('<I', len(pcm8)) + bytes(pcm8)
        self.blocks.append(len(pcm8))
        return len(self.blocks) - 1

    def stream_setup(self):
        self.cmd += bytes([0x90, 0x00, 0x02, 0x00, 0x2A])
        self.cmd += bytes([0x91, 0x00, 0x00, 0x01, 0x00])
        self.cmd += bytes([0x92, 0x00]) + struct.pack('<I', RATE)

    def stream_start(self, block):
        self.cmd += bytes([0x95, 0x00]) + struct.pack('<HB', block, 0)

    def mark_loop(self):
        self.loop_ofs = len(self.cmd)
        self.loop_start_samples = self.samples

    def save(self, path):
        self.cmd += b'\x66'
        hdr = bytearray(0x40)
        hdr[0:4] = b'Vgm '
        struct.pack_into('<I', hdr, 0x08, 0x150)
        struct.pack_into('<I', hdr, 0x0C, PSG_CLOCK)
        struct.pack_into('<I', hdr, 0x18, self.samples)
        if self.loop_ofs is not None:
            struct.pack_into('<I', hdr, 0x1C, 0x40 + self.loop_ofs - 0x1C)
            struct.pack_into('<I', hdr, 0x20, self.samples - self.loop_start_samples)
        struct.pack_into('<I', hdr, 0x24, 60)
        struct.pack_into('<H', hdr, 0x28, 0x0009)
        hdr[0x2A] = 16
        struct.pack_into('<I', hdr, 0x2C, FM_CLOCK)
        struct.pack_into('<I', hdr, 0x34, 0x40 - 0x34)
        data = hdr + self.cmd
        struct.pack_into('<I', data, 0x04, len(data) - 4)
        open(path, 'wb').write(data)

# ---------------------------------------------------------------- synth state -> register writes
class Synth:
    """5 FM voices (ch 1-5; ch 6 is the DAC), 3 PSG tones + noise, 1 PCM drum stream."""
    def __init__(self, vgm):
        self.v = vgm
        self.fm_patch = [None] * 5
        self.fm_note = [None] * 5       # (chan, note) sounding
        self.fm_age = [0] * 5
        self.psg_note = [None] * 3
        self.psg_age = [0] * 3
        self.psg_vol = [15] * 3
        self.psg_env = [0] * 3
        self.noise_vol = 15
        self.noise_dec = 0
        self.clock = 0
        self.drum_blocks = {}
        v = self.v
        v.ym(0, 0x22, 0)                # LFO off
        v.ym(0, 0x27, 0)                # ch3 normal
        v.ym(0, 0x2B, 0x80)             # DAC on (drums)
        for ch in range(6):
            v.ym(0, 0x28, ch if ch < 3 else ch + 1)
        for c in range(4):
            v.psg(0x90 | (c << 5) | 15)

    def fm_reg(self, ch, base, val):
        port = 0 if ch < 3 else 1
        self.v.ym(port, base + (ch % 3), val)

    def load_patch(self, ch, name):
        alg, fb, ops = PATCHES[name]
        port = 0 if ch < 3 else 1
        c = ch % 3
        self.v.ym(0, 0x28, ch if ch < 3 else ch + 1)     # key off first
        for i, (dt, mul, tl, ks, ar, am, dr, sr, sl, rr) in enumerate(ops):
            o = SLOT[i] + c
            self.v.ym(port, 0x30 + o, (dt << 4) | mul)
            self.v.ym(port, 0x40 + o, tl)
            self.v.ym(port, 0x50 + o, (ks << 6) | ar)
            self.v.ym(port, 0x60 + o, (am << 7) | dr)
            self.v.ym(port, 0x70 + o, sr)
            self.v.ym(port, 0x80 + o, (sl << 4) | rr)
            self.v.ym(port, 0x90 + o, 0)
        self.v.ym(port, 0xB0 + c, (fb << 3) | alg)
        self.v.ym(port, 0xB4 + c, 0xC0)
        self.fm_patch[ch] = name

    def fm_on(self, ch, name, note, vol):
        """vol 0..127"""
        if self.fm_patch[ch] != name:
            self.load_patch(ch, name)
        alg, fb, ops = PATCHES[name]
        port = 0 if ch < 3 else 1
        c = ch % 3
        keyreg = ch if ch < 3 else ch + 1
        self.v.ym(0, 0x28, keyreg)
        att = int((127 - vol) * 0.33)
        for i in CARRIERS[alg]:
            self.v.ym(port, 0x40 + SLOT[i] + c, min(127, ops[i][2] + att))
        block, fn = fm_fnum(note)
        self.v.ym(port, 0xA4 + c, (block << 3) | (fn >> 8))
        self.v.ym(port, 0xA0 + c, fn & 0xFF)
        self.v.ym(0, 0x28, 0xF0 | keyreg)

    def fm_off(self, ch):
        self.v.ym(0, 0x28, ch if ch < 3 else ch + 1)

    def psg_on(self, ch, note, vol):
        p = psg_period(note)
        self.v.psg(0x80 | (ch << 5) | (p & 15))
        self.v.psg((p >> 4) & 63)
        a = max(0, min(15, int((127 - vol) / 9) + 1))
        self.psg_vol[ch] = a
        self.psg_env[ch] = 0
        self.v.psg(0x90 | (ch << 5) | a)

    def psg_off(self, ch):
        self.psg_vol[ch] = 15
        self.v.psg(0x90 | (ch << 5) | 15)

    def noise(self, kind):
        # hats: white noise, highest fixed rate; open hats ring longer
        self.v.psg(0xE4)
        self.noise_vol = 3 if kind != 'ride' else 5
        self.noise_dec = {'closed': 4, 'open': 1, 'ride': 2}[kind]
        self.v.psg(0xF0 | self.noise_vol)

    def frame_tick(self):
        # PSG envelopes (slow decay), noise decay
        for ch in range(3):
            if self.psg_note[ch] is not None and self.psg_vol[ch] < 15:
                self.psg_env[ch] += 1
                if self.psg_env[ch] % 10 == 0 and self.psg_vol[ch] < 11:
                    self.psg_vol[ch] += 1
                    self.v.psg(0x90 | (ch << 5) | self.psg_vol[ch])
        if self.noise_vol < 15:
            self.noise_vol = min(15, self.noise_vol + self.noise_dec)
            self.v.psg(0xF0 | self.noise_vol)

# ---------------------------------------------------------------- arranger: note events -> synth
class Arranger:
    """events: (time_s, kind, chan, note, vel, prog); allocates voices per frame"""
    def __init__(self, vgm, synth, bass_chans=()):
        self.v, self.s = vgm, synth
        self.bass_chans = set(bass_chans)

    def run(self, events, length_s, loop_at=0.0):
        s = self.s
        events.sort(key=lambda e: (e[0], 0 if e[1] == 'off' else 1))
        frames = int(length_s * 60)
        ei = 0
        active_fm = [None] * 5     # (chan, note)
        active_psg = [None] * 3
        start_age = [0] * 8
        for fr in range(frames):
            if fr == int(loop_at * 60):
                self.v.mark_loop()
            t1 = (fr + 1) / 60.0
            drum_hit = None
            while ei < len(events) and events[ei][0] < t1:
                t, kind, ch, note, vel, prog = events[ei]
                ei += 1
                if kind == 'drum':
                    d = drum_kind(note)
                    if d in ('closed', 'open', 'ride'):
                        s.noise(d)
                    elif d:
                        pr = {'kick': 4, 'snare': 3, 'crash': 2, 'tom': 1}[d]
                        if drum_hit is None or pr > drum_hit[0]:
                            drum_hit = (pr, d)
                    continue
                if kind == 'off':
                    for k in range(5):
                        if active_fm[k] == (ch, note):
                            s.fm_off(k); active_fm[k] = None
                    for k in range(3):
                        if active_psg[k] == (ch, note):
                            s.psg_off(k); active_psg[k] = None; s.psg_note[k] = None
                    continue
                name = patch_for(prog)
                is_bass = ch in self.bass_chans or name == 'bass'
                # FM voice: bass prefers voice 0, others voices 1..4
                cands = [0] if is_bass else [1, 2, 3, 4]
                free = [k for k in cands if active_fm[k] is None]
                if free:
                    k = min(free, key=lambda k: (s.fm_patch[k] != name, start_age[k]))
                    active_fm[k] = (ch, note); start_age[k] = fr
                    s.fm_on(k, name, note, vel)
                    continue
                if not is_bass and note >= 60:
                    freep = [k for k in range(3) if active_psg[k] is None]
                    if freep:
                        k = freep[0]
                        active_psg[k] = (ch, note); s.psg_note[k] = note
                        s.psg_on(k, note, vel)
                        continue
                # steal the oldest FM voice of the group
                k = min(cands, key=lambda k: start_age[k])
                active_fm[k] = (ch, note); start_age[k] = fr
                s.fm_on(k, name, note, vel)
            if drum_hit:
                s.v.stream_start(s.drum_blocks[drum_hit[1]])
            s.frame_tick()
            self.v.wait(735)

def drum_kind(n):
    if n in (35, 36): return 'kick'
    if n in (37, 38, 39, 40): return 'snare'
    if n in (41, 43, 45, 47, 48, 50): return 'tom'
    if n in (49, 52, 55, 57): return 'crash'
    if n in (42, 44): return 'closed'
    if n in (46,): return 'open'
    if n in (51, 53, 59): return 'ride'
    return None

def setup_song():
    v = Vgm()
    s = Synth(v)
    v.stream_setup()
    for d in DRUMS:
        pcm = [max(0, min(255, int(128 + x * 118))) for x in drum(d)]
        s.drum_blocks[d] = v.data_block(pcm)
    return v, s

# ---------------------------------------------------------------- MIDI
def midi_events(path, max_s):
    import mido
    m = mido.MidiFile(path)
    t = 0.0
    prog = [0] * 16
    vol = [100] * 16
    ev = []
    for msg in m:
        t += msg.time
        if t > max_s:
            break
        if msg.type == 'program_change':
            prog[msg.channel] = msg.program
        elif msg.type == 'control_change' and msg.control == 7:
            vol[msg.channel] = msg.value
        elif msg.type == 'note_on' and msg.velocity:
            v = int(msg.velocity * vol[msg.channel] / 127)
            if msg.channel == 9:
                ev.append((t, 'drum', 9, msg.note, v, 0))
            else:
                ev.append((t, 'on', msg.channel, msg.note, max(40, v), prog[msg.channel]))
        elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
            if msg.channel != 9:
                ev.append((t, 'off', msg.channel, msg.note, 0, 0))
    return ev, min(max_s, t)

def bass_channels(ev):
    """channels whose average pitch is low -> the dedicated bass voice"""
    acc = {}
    for t, k, ch, n, v, p in ev:
        if k == 'on':
            a = acc.setdefault(ch, [0, 0, p])
            a[0] += n; a[1] += 1
    out = [ch for ch, (s, c, p) in acc.items() if c and (s / c < 45 or 32 <= p < 40)]
    return out[:1] or [min(acc, key=lambda ch: acc[ch][0] / acc[ch][1])]

# ---------------------------------------------------------------- Star Frag themes
NOTE_NAMES = {'C': 0, 'C#': 1, 'D': 2, 'D#': 3, 'E': 4, 'F': 5, 'F#': 6, 'G': 7, 'G#': 8, 'A': 9, 'A#': 10, 'B': 11}

def midi_note(tok):
    n, o = tok[:-1], int(tok[-1])
    return 12 * (o + 1) + NOTE_NAMES[n]

SF_SONGS = {   # melody, bass, seconds per unit, repeats
 'title': (
  "E4:2 B4:2 E5:2 D5:1 B4:1 A4:2 B4:2 G4:4 "
  "E4:2 B4:2 E5:2 F#5:1 G5:1 F#5:2 D5:2 B4:4 "
  "C5:2 E5:2 G5:2 F#5:1 E5:1 D5:2 B4:2 A4:4 "
  "G4:2 A4:2 B4:2 D5:2 E5:6 R:2",
  "E2:2 E3:2 E2:2 E3:2 C3:2 C3:2 D3:2 D3:2 E2:2 E3:2 E2:2 E3:2 B2:2 B2:2 D3:2 D3:2 "
  "C3:2 C3:2 C3:2 C3:2 D3:2 D3:2 D3:2 D3:2 E2:2 E3:2 B2:2 D3:2 E2:4 E3:4", 9 / 60.0, 4),
 'arena': (
  "A4:1 R:1 A4:1 C5:1 R:1 A4:1 D5:1 E5:1 R:2 E5:1 G5:1 E5:1 D5:1 C5:2 "
  "A4:1 R:1 A4:1 C5:1 R:1 A4:1 D5:1 E5:1 R:2 G5:1 F5:1 E5:1 D5:1 E5:2 "
  "F5:1 R:1 F5:1 E5:1 D5:1 C5:1 D5:2 R:1 E5:1 D5:1 C5:1 B4:1 G4:1 A4:2 "
  "C5:1 B4:1 A4:1 B4:1 C5:1 D5:1 E5:2 R:2 E4:1 G#4:1 B4:1 E5:1 A4:2",
  "A2:1 A2:1 A3:1 A2:1 A2:1 A3:1 G2:1 G3:1 A2:1 A2:1 A3:1 A2:1 C3:1 C3:1 D3:1 E3:1 "
  "A2:1 A2:1 A3:1 A2:1 A2:1 A3:1 G2:1 G3:1 A2:1 A2:1 A3:1 A2:1 E3:1 D3:1 C3:1 B2:1 "
  "F2:1 F2:1 F3:1 F2:1 F2:1 F3:1 G2:1 G3:1 C3:1 C3:1 C4:1 C3:1 G2:1 G2:1 G3:1 G2:1 "
  "F2:1 F2:1 F3:1 F2:1 G2:1 G2:1 G3:1 G2:1 E2:1 E2:1 E3:1 E2:1 E2:1 G#2:1 B2:1 E3:1", 7 / 60.0, 4),
 'win': ("E5:1 G5:1 B5:1 E6:3 D6:1 E6:6 R:8", "E3:3 B3:3 E4:6 R:8", 6 / 60.0, 1),
}

def sf_events(key):
    mel, bas, unit, reps = SF_SONGS[key]
    ev = []
    def add(text, ch, prog, octave=0, vel=110, stacc=1.0):
        t = 0.0
        for _ in range(reps):
            for tok in text.split():
                nt, ln = tok.split(':')
                d = int(ln) * unit
                if nt != 'R':
                    n = midi_note(nt) + octave
                    ev.append((t, 'on', ch, n, vel, prog))
                    ev.append((t + d * stacc - 0.001, 'off', ch, n, 0, 0))
                t += d
        return t
    total = add(mel, 0, 81 if key != 'title' else 62)          # lead / brass
    add(mel, 1, 49, octave=-12, vel=80)                          # strings an octave below
    add(bas, 2, 33, vel=120, stacc=0.9)                          # bass
    add(mel, 3, 80, octave=12, vel=70, stacc=0.5)                # PSG sparkle (high, short)
    # drums: kick on beats, snare on backbeats, hats on every unit
    beat = unit * (2 if key == 'title' else 2)
    t = 0.0
    k = 0
    while t < total - 0.01 and key != 'win':
        ev.append((t, 'drum', 9, 36 if k % 4 in (0, 2) and k % 8 != 6 else 38 if k % 4 in (1, 3) else 36, 100, 0))
        ev.append((t + beat / 2, 'drum', 9, 42, 80, 0))
        if k % 16 == 0:
            ev.append((t + 0.017, 'drum', 9, 49, 100, 0))
        t += beat
        k += 1
    if key == 'win':
        ev.append((0.0, 'drum', 9, 49, 100, 0))
        ev.append((unit * 6, 'drum', 9, 36, 100, 0))
        ev.append((unit * 6 + 0.034, 'drum', 9, 49, 100, 0))
    return ev, total

MUSIC = [  # name, source
    ('title', 'sf:title'), ('win', 'sf:win'), ('arena', 'sf:arena'),
    ('dm05', 'Music/D_DM05'), ('dm06', 'Music/D_DM06'), ('dm07', 'Music/D_DM07'), ('dm10', 'Music/D_DM10'),
]
MAX_SONG_S = 150

def gen_music():
    os.makedirs(os.path.join(RES, 'music'), exist_ok=True)
    res = ''
    for name, srcp in MUSIC:
        v, s = setup_song()
        if srcp.startswith('sf:'):
            ev, length = sf_events(srcp[3:])
            bass = [2]
        else:
            ev, length = midi_events(os.path.join(BD, srcp), MAX_SONG_S)
            bass = bass_channels(ev)
        if name == 'win':
            length += 0.5
        Arranger(v, s, bass).run(ev, length, 0.0)
        v.save(os.path.join(RES, 'music', name + '.vgm'))
        res += 'XGM2 mus_%s "music/%s.vgm"\n' % (name, name)
    return res
