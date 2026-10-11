#!/usr/bin/env python3
"""Compose and render Crown of Embers' music and sound effects into assets/audio/.

The map theme comes in two arrangements of the same score, kept sample-for-sample in sync:
"rain" (harp, pads and a soft lead) for moving around the map, and "blaze" (driving strings,
brass and war drums) that the game crossfades to whenever a battle starts.

Orchestral pieces are written here as MIDI and rendered with FluidSynth and a General MIDI
soundfont (MuseScore_General.sf3 works well). Short UI and combat effects are synthesized
directly. Only needed when changing the audio; the rendered files are committed.

    python3 tools/compose.py path/to/MuseScore_General.sf3
"""
import array
import math
import os
import random
import struct
import subprocess
import sys
import tempfile
import wave

RATE = 44100
TPB = 480
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "audio")

# ---------------------------------------------------------------------------
# MIDI
# ---------------------------------------------------------------------------

PITCH = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def n(name):
    """'C#5' -> 73. Middle C is C4 = 60."""
    pc = PITCH[name[0]]
    rest = name[1:]
    while rest and rest[0] in "#b":
        pc += 1 if rest[0] == "#" else -1
        rest = rest[1:]
    return 12 * (int(rest) + 1) + pc


class Track:
    def __init__(self, ch, program, volume=100, pan=64, reverb=70):
        self.ch = ch
        self.events = []  # (tick, order, bytes); note-offs sort before note-ons on the same tick
        if ch != 9:
            self.raw(0, 0, bytes([0xC0 | ch, program]))
        self.cc(0, 7, volume)
        self.cc(0, 10, pan)
        self.cc(0, 91, reverb)

    def raw(self, beat, order, data):
        self.events.append((round(beat * TPB), order, data))

    def cc(self, beat, num, val):
        self.raw(beat, 1, bytes([0xB0 | self.ch, num, max(0, min(127, int(val)))]))

    def note(self, beat, dur, pitch, vel):
        vel = max(1, min(127, int(vel)))
        self.raw(beat, 2, bytes([0x90 | self.ch, pitch, vel]))
        self.raw(beat + max(dur, 0.02), 0, bytes([0x80 | self.ch, pitch, 0]))


def vlq(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.append(0x80 | (v & 0x7F))
        v >>= 7
    return bytes(reversed(out))


def write_midi(path, tracks, tempo_us, end_beat):
    chunks = []
    meta = vlq(0) + b"\xff\x51\x03" + tempo_us.to_bytes(3, "big")
    meta += vlq(round(end_beat * TPB)) + b"\xff\x2f\x00"
    chunks.append(meta)
    for t in tracks:
        data, last = b"", 0
        for tick, _, ev in sorted(t.events, key=lambda e: (e[0], e[1])):
            data += vlq(tick - last) + ev
            last = tick
        # Hold every track open to the end so the renderer keeps going through reverb tails.
        data += vlq(max(0, round(end_beat * TPB) - last)) + b"\xff\x2f\x00"
        chunks.append(data)
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), TPB))
        for c in chunks:
            f.write(b"MTrk" + struct.pack(">I", len(c)) + c)


def render(soundfont, tracks, tempo_us, end_beat, gain=0.5):
    """Render tracks with FluidSynth; returns interleaved stereo float samples."""
    with tempfile.TemporaryDirectory() as tmp:
        mid, wav = os.path.join(tmp, "a.mid"), os.path.join(tmp, "a.wav")
        write_midi(mid, tracks, tempo_us, end_beat)
        subprocess.run(
            ["fluidsynth", "-ni", "-q", "-g", str(gain), "-r", str(RATE), "-F", wav,
             "-o", "synth.reverb.room-size=0.82", "-o", "synth.reverb.damp=0.35",
             "-o", "synth.reverb.width=1.0", "-o", "synth.reverb.level=0.7",
             "-o", "synth.chorus.active=0", soundfont, mid],
            check=True)
        return read_wav(wav)


# ---------------------------------------------------------------------------
# WAV helpers (stereo float lists in [-1, 1])
# ---------------------------------------------------------------------------


def read_wav(path):
    with wave.open(path, "rb") as w:
        assert w.getsampwidth() == 2
        a = array.array("h", w.readframes(w.getnframes()))
        if w.getnchannels() == 1:
            a = array.array("h", [s for s in a for _ in (0, 1)])
    return [s / 32768.0 for s in a]


def write_wav(path, samples, channels=2):
    a = array.array("h", (max(-32767, min(32767, int(s * 32767))) for s in samples))
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(a.tobytes())


def peak(samples):
    return max(abs(s) for s in samples) or 1.0


def rms(samples):
    return math.sqrt(sum(s * s for s in samples) / len(samples))


def trim_tail(samples, fade_s=0.4, floor=0.006):
    """Cut trailing silence, then fade the last fade_s seconds."""
    end = len(samples)
    while end > 2 and abs(samples[end - 1]) < floor and abs(samples[end - 2]) < floor:
        end -= 2
    end = min(len(samples), end + RATE // 10 * 2)
    out = samples[:end]
    nf = min(len(out) // 2, int(fade_s * RATE))
    for i in range(nf):
        g = i / nf
        out[end - 2 - 2 * i] *= g
        out[end - 1 - 2 * i] *= g
    return out


def scale(samples, g):
    return [s * g for s in samples]


# ---------------------------------------------------------------------------
# The map theme: "Embers of War"
# ---------------------------------------------------------------------------

TEMPO_US = 400000  # 150 BPM; exactly 17640 samples per beat, so the loop is sample-exact
CHORDS = {
    "Dm": ("D", (0, 3, 7)), "Gm": ("G", (0, 3, 7)), "Am": ("A", (0, 3, 7)),
    "Bb": ("Bb", (0, 4, 7)), "C": ("C", (0, 4, 7)), "F": ("F", (0, 4, 7)), "A": ("A", (0, 4, 7)),
}


def chord_tones(name, low, count):
    """The chord's tones ascending from `low`, `count` of them."""
    root, ivs = CHORDS[name]
    pcs = {(n(root + "0") + i) % 12 for i in ivs}
    out, p = [], low
    while len(out) < count:
        if p % 12 in pcs:
            out.append(p)
        p += 1
    return out


def root_in(name, lo, hi):
    root = n(CHORDS[name][0] + "0") % 12
    for p in range(lo, hi + 1):
        if p % 12 == root:
            return p
    raise ValueError(name)


def fifth_in(name, lo, hi):
    root = n(CHORDS[name][0] + "0")
    for p in range(lo, hi + 1):
        if p % 12 == (root + 7) % 12:
            return p
    raise ValueError(name)


def mel(text):
    """'D5:1.5 A4:.5 r:1' -> [(pitch or None, beats)]"""
    out = []
    for tok in text.split():
        name, dur = tok.split(":")
        out.append((None if name == "r" else n(name), float(dur)))
    return out


SECTIONS = [
    ("intro", ["Dm", "Dm", "Bb", "A"], None),
    ("A", ["Dm", "Bb", "C", "Dm", "Dm", "Bb", "Gm", "A"], mel(
        "D5:1.5 A4:.5 D5:.5 E5:.5 F5:1 "
        "G5:1.5 F5:.5 E5:1 D5:1 "
        "E5:3 C5:.5 D5:.5 "
        "D5:4 "
        "D5:1.5 A4:.5 D5:.5 E5:.5 F5:1 "
        "G5:1.5 A5:.5 Bb5:1 G5:1 "
        "A5:1.5 G5:.5 F5:1 E5:1 "
        "E5:2 C#5:2")),
    ("B", ["Bb", "C", "Am", "Dm", "Gm", "C", "F", "A"], mel(
        "F5:1 Bb5:1 A5:1 F5:1 "
        "G5:3 E5:1 "
        "E5:1 A5:1 G5:1 E5:1 "
        "F5:3 D5:1 "
        "D5:1 G5:1 Bb5:1 A5:1 "
        "G5:1.5 F5:.5 E5:1 C5:1 "
        "F5:1 A5:1 C6:2 "
        "C#6:2 A5:2")),
    ("C", ["Dm", "Bb", "F", "C", "Gm", "Bb", "A", "A"], mel(
        "D6:3 A5:1 "
        "Bb5:1.5 A5:.5 G5:1 F5:1 "
        "A5:3 F5:1 "
        "G5:2 E5:2 "
        "Bb5:1 A5:1 G5:1 D6:1 "
        "D6:1.5 C6:.5 Bb5:1 A5:1 "
        "A5:1 C#6:1 E6:1 C#6:1 "
        "A5:4")),
    ("A2", None, None),  # the A section again, reorchestrated
]
SECTIONS[4] = ("A2", SECTIONS[1][1], SECTIONS[1][2])

# Who carries the tune: (instrument, octave shift in semitones, velocity)
LEADS = {
    "rain": {"A": [("horn", 0, 82)], "B": [("flute", 0, 84), ("clarinet", -12, 62)],
             "C": [("violin", 0, 92), ("flute", 0, 62)], "A2": [("oboe", 0, 84), ("horn", -12, 64)]},
    "blaze": {"A": [("horn", 0, 108), ("trombone", -12, 84)], "B": [("violin", 0, 108), ("horn", -12, 96)],
              "C": [("trumpet", 0, 112), ("horn", -12, 108), ("violin", 0, 90)],
              "A2": [("trumpet", 0, 108), ("horn", -12, 102), ("violin", 0, 92)]},
}


def theme_tracks(version, loops=2):
    blaze = version == "blaze"
    rng = random.Random(7)
    T = {
        "pad": Track(0, 48, 92 if blaze else 88, 64, 80),
        "harp": Track(1, 46, 70 if blaze else 100, 40, 80),
        "horn": Track(2, 60, 110, 54, 80),
        "bass": Track(3, 43 if blaze else 45, 110, 70, 50),
        "timp": Track(4, 47, 118 if blaze else 96, 64, 60),
        "ostinato": Track(5, 48, 100, 80, 50),
        "cello": Track(6, 42, 105, 74, 50),
        "trombone": Track(7, 57, 100, 50, 60),
        "flute": Track(8, 73, 95, 84, 85),
        "drums": Track(9, 0, 120, 64, 55),
        "trumpet": Track(10, 56, 105, 72, 70),
        "violin": Track(11, 40, 105, 44, 80),
        "choir": Track(12, 52, 96, 64, 95),
        "oboe": Track(13, 68, 100, 76, 80),
        "clarinet": Track(15, 71, 95, 56, 80),
    }

    def hum(v, spread=6):
        return v + rng.randint(-spread, spread)

    bar_total = sum(len(s[1]) for s in SECTIONS)
    for loop in range(loops):
        bar = loop * bar_total
        for si, (sec, chords, melody) in enumerate(SECTIONS):
            first_bar = bar
            for bi, ch in enumerate(chords):
                b0 = bar * 4
                last_of_section = bi == len(chords) - 1
                intro = sec == "intro"
                big = sec in ("C", "A2")

                # Sustained string pad (and choir in the big sections).
                pad = chord_tones(ch, n("D3"), 4 if big else 3)
                for p in pad:
                    T["pad"].note(b0, 4, p, hum(52 if not blaze else 66, 3))
                if big and (blaze or sec == "C"):
                    for p in chord_tones(ch, n("A3"), 3):
                        T["choir"].note(b0, 4, p, hum(88 if blaze else 58, 3))

                # Harp arpeggio rising and falling in eighths.
                tones = chord_tones(ch, n("A3"), 6)
                for k, idx in enumerate([0, 1, 2, 3, 4, 5, 4, 2]):
                    vel = (62 if not blaze else 46) + (10 if k == 0 else 0)
                    if not (blaze and intro):
                        T["harp"].note(b0 + k * 0.5, 1.2, tones[idx], hum(vel, 4))

                root2 = root_in(ch, n("E2") - 2, n("D3"))
                if root2 > n("A2"):
                    root2 -= 12
                fifth = root2 + 7
                if not blaze:
                    # Pizzicato bass: a heartbeat under the harp.
                    for beat, p, v in [(0, root2, 92), (2, root2, 78), (3.5, fifth, 70)]:
                        T["bass"].note(b0 + beat, 0.5, p, hum(v))
                else:
                    # Driving eighths in the low strings, sixteenths above them.
                    for k in range(8):
                        T["bass"].note(b0 + k * 0.5, 0.42, root2, hum(100 if k % 2 == 0 else 80))
                    pattern = [0, 0, 7, 0, 12, 0, 7, 0]
                    for k in range(8):
                        T["cello"].note(b0 + k * 0.5, 0.4, root2 + 12 + pattern[k], hum(92 if k % 4 == 0 else 74))
                    r4 = root_in(ch, n("D4"), n("C#5"))
                    sixteenths = [0, 0, 7, 0, 0, 0, 7, 12, 0, 0, 7, 0, 12, 7, 3 if CHORDS[ch][1][1] == 3 else 4, 0]
                    for k, iv in enumerate(sixteenths):
                        T["ostinato"].note(b0 + k * 0.25, 0.18, r4 + iv - 12, hum(98 if k % 4 == 0 else 70))
                    if sec != "A" or bi >= 4:
                        for p in [root2 + 12, root2 + 19]:
                            T["trombone"].note(b0, 1.8, p, hum(84))
                            T["trombone"].note(b0 + 2, 1.8, p, hum(78))

                # Timpani, tuned to the chord root.
                tr = root_in(ch, n("F1"), n("E2"))
                low5 = tr - 5  # the fifth below, for a two-drum figure
                if blaze:
                    for beat, v in [(0, 118), (1.5, 88), (2, 104), (3, 96), (3.5, 90)]:
                        T["timp"].note(b0 + beat, 0.5, low5 if beat == 1.5 else tr, hum(v))
                else:
                    if bi == 0:
                        T["timp"].note(b0, 2, tr, hum(96))
                if last_of_section:
                    # Roll into the next section.
                    start, count = (2, 12) if blaze else (3, 8)
                    step = (4 - start) / count
                    for k in range(count):
                        T["timp"].note(b0 + start + k * step, step, tr, 50 + k * (6 if blaze else 5))

                # War drums (blaze only), and a cymbal swell on each new section.
                if blaze:
                    d = T["drums"]
                    for beat in range(4):
                        if beat in (0, 2):
                            d.note(b0 + beat, 0.2, 36, hum(112))
                        else:
                            d.note(b0 + beat, 0.2, 38, hum(98))
                        d.note(b0 + beat + 0.5, 0.1, 38, hum(48, 4))
                    d.note(b0 + 2.5, 0.2, 41, hum(90))
                    d.note(b0 + 3.75, 0.1, 38, hum(56, 4))
                    if last_of_section:
                        for k in range(8):
                            d.note(b0 + 2 + k * 0.25, 0.1, 38, 60 + k * 7)
                        d.note(b0 + 3.5, 0.2, 45, 104)
                        d.note(b0 + 3.75, 0.2, 43, 110)
                if bar == first_bar:
                    T["drums"].note(b0, 2, 49, 112 if blaze else 46)
                    if blaze:
                        T["drums"].note(b0, 0.3, 36, 124)

                bar += 1

            # The melody for this section.
            if melody:
                beat = first_bar * 4
                for pitch, dur in melody:
                    if pitch is not None:
                        for inst, shift, vel in LEADS[version][sec]:
                            legato = 0.88 if inst in ("trumpet", "trombone") else 0.97
                            T[inst].note(beat, dur * legato, pitch + shift, hum(vel, 4))
                    beat += dur
            elif blaze:
                # Brass stabs answer the intro's last bar.
                b0 = (bar - 1) * 4
                for off, v in [(0, 104), (1.5, 96), (2, 112)]:
                    for p in chord_tones("A", n("A3"), 3):
                        T["trombone"].note(b0 + off, 0.4 if off < 2 else 1.8, p - 12, v)
                        T["horn"].note(b0 + off, 0.4 if off < 2 else 1.8, p, v - 8)

    beats = bar_total * 4
    return list(T.values()), beats


# ---------------------------------------------------------------------------
# Jingles and orchestral one-shots
# ---------------------------------------------------------------------------


def victory_tracks():
    tr = Track(10, 56, 110, 70)
    hn = Track(2, 60, 110, 54)
    tb = Track(7, 57, 100, 50)
    st = Track(0, 48, 100)
    tp = Track(4, 47, 120)
    dr = Track(9, 0, 110)
    ch = Track(12, 52, 96)
    melody = mel("A4:.333 A4:.333 A4:.334 D5:1 C#5:.5 D5:.5 E5:1 "
                 "F#5:1.5 D5:.5 A4:1 F#5:1 "
                 "G5:1.5 E5:.5 C5:1 G5:1 "
                 "A5:4")
    beat = 0
    for p, d in melody:
        tr.note(beat, d * 0.9, p, 112)
        hn.note(beat, d * 0.95, p - 12, 104)
        beat += d
    chords = [("D", (0, 4, 7)), ("D", (0, 4, 7)), ("C", (0, 4, 7)), ("D", (0, 4, 7))]
    for bi, (r, ivs) in enumerate(chords):
        root = n(r + "3")
        for iv in ivs:
            st.note(bi * 4, 4 if bi < 3 else 6, root + iv, 92)
            tb.note(bi * 4, 1.8, root + iv - 12, 90)
            if bi == 3:
                ch.note(12, 6, root + iv + 12, 96)
    for k in range(16):
        tp.note(8 + 0.25 * k, 0.25, n("A1"), 60 + k * 3)
    tp.note(12, 2, n("D2"), 124)
    for b in (0, 4, 8, 12):
        dr.note(b, 2, 49, 110)
    dr.note(12, 3, 57, 120)
    return [tr, hn, tb, st, tp, dr, ch], 20


def defeat_tracks():
    ob = Track(13, 68, 105, 70)
    st = Track(0, 48, 100)
    vc = Track(6, 42, 100)
    tp = Track(4, 47, 110)
    melody = mel("D5:2 C5:1 Bb4:1 A4:2 G4:1 F4:1 E4:2 C#4:2 D4:4")
    beat = 0
    for p, d in melody:
        ob.note(beat, d * 0.97, p, 86)
        beat += d
    for bi, (r, ivs) in enumerate([("D", (0, 3, 7)), ("G", (0, 3, 7)), ("A", (0, 4, 7)), ("D", (0, 3, 7))]):
        root = n(r + "3")
        for iv in ivs:
            st.note(bi * 4, 4 if bi < 3 else 6, root + iv, 70)
        vc.note(bi * 4, 4 if bi < 3 else 6, root - 12, 80)
    tp.note(12, 3, n("D2"), 90)
    return [ob, st, vc, tp], 20


def crit_tracks():
    hit = Track(0, 55, 115)
    br = Track(7, 61, 110)
    tp = Track(4, 47, 120)
    dr = Track(9, 0, 120)
    for p in chord_tones("Dm", n("D3"), 5):
        hit.note(0, 0.6, p, 120)
        br.note(0, 0.5, p, 110)
    tp.note(0, 1, n("D2"), 127)
    dr.note(0, 1, 49, 120)
    dr.note(0, 0.3, 36, 127)
    return [hit, br, tp, dr], 5


def levelup_tracks():
    gl = Track(14, 9, 100)
    hp = Track(1, 46, 110)
    st = Track(0, 48, 90)
    for k, p in enumerate(["D5", "F#5", "A5", "D6", "F#6", "A6"]):
        gl.note(k * 0.25, 1.5, n(p), 90)
        hp.note(k * 0.25, 2, n(p) - 12, 96)
    for p in [n("D4"), n("F#4"), n("A4"), n("D5")]:
        st.note(1.5, 2, p, 84)
    return [gl, hp, st], 5


def heal_tracks():
    ce = Track(14, 8, 110)
    hp = Track(1, 46, 100)
    for k, p in enumerate(["A5", "C#6", "E6", "A6", "E6", "A6"]):
        ce.note(k * 0.16, 1.2, n(p), 84)
    for k, p in enumerate(["A3", "E4", "A4", "C#5", "E5", "A5", "C#6", "E6"]):
        hp.note(k * 0.08, 1.5, n(p), 70)
    return [ce, hp], 4


def phase_tracks(player):
    tps = []
    if player:
        br = Track(7, 61, 110)
        hn = Track(2, 60, 105)
        for p in chord_tones("Bb", n("F3"), 3):
            br.note(0, 0.6, p, 92)
        for p in chord_tones("F", n("F3"), 3):
            br.note(0.75, 1.6, p, 100)
            hn.note(0.75, 1.6, p + 12, 92)
        tps = [br, hn]
    else:
        lo = Track(6, 42, 110)
        bs = Track(3, 43, 110)
        tp = Track(4, 47, 120)
        for p in [n("D2"), n("D3")]:
            lo.note(0, 1.6, p + 12, 100)
            bs.note(0, 1.6, p, 104)
        for k in range(6):
            tp.note(k * 0.125, 0.125, n("D2"), 70 + k * 8)
        tp.note(0.75, 1, n("D2"), 120)
        tps = [lo, bs, tp]
    return tps, 5


# ---------------------------------------------------------------------------
# Synthesized effects (mono)
# ---------------------------------------------------------------------------


class Synth:
    def __init__(self, seconds, seed=1):
        self.buf = [0.0] * int(seconds * RATE)
        self.rng = random.Random(seed)

    def tone(self, start, dur, f0, f1=None, amp=0.3, attack=0.002, decay=None, shape="sine", partials=None):
        f1 = f0 if f1 is None else f1
        s0, ns = int(start * RATE), int(dur * RATE)
        phase = 0.0
        decay = decay or dur
        for i in range(ns):
            t = i / RATE
            f = f0 + (f1 - f0) * (i / ns)
            phase += 2 * math.pi * f / RATE
            env = min(1.0, t / attack) * math.exp(-t / decay * 3)
            if partials:
                v = sum(a * math.sin(phase * m) for m, a in partials)
            elif shape == "tri":
                v = 2 / math.pi * math.asin(math.sin(phase))
            else:
                v = math.sin(phase)
            if s0 + i < len(self.buf):
                self.buf[s0 + i] += amp * env * v

    def noise(self, start, dur, amp=0.3, attack=0.002, decay=None, lp0=0.5, lp1=None, hp=0.0, crackle=0.0):
        """Filtered noise; lp is a one-pole coefficient (0..1, higher = brighter), swept lp0 -> lp1."""
        lp1 = lp0 if lp1 is None else lp1
        s0, ns = int(start * RATE), int(dur * RATE)
        decay = decay or dur
        y = yh = 0.0
        for i in range(ns):
            t = i / RATE
            a = lp0 + (lp1 - lp0) * (i / ns)
            x = self.rng.uniform(-1, 1)
            if crackle and self.rng.random() < crackle:
                x *= 8
            y += a * (x - y)
            out = y - yh if hp else y
            yh += hp * (y - yh)
            env = min(1.0, t / attack) * math.exp(-t / decay * 3)
            if s0 + i < len(self.buf):
                self.buf[s0 + i] += amp * env * out

    def out(self, gain=0.9):
        g = gain / (max(abs(s) for s in self.buf) or 1)
        return [s * g for s in self.buf]


def sfx_cursor():
    s = Synth(0.06)
    s.tone(0, 0.05, 1500, amp=0.3, decay=0.02, shape="tri")
    return s.out(0.35)


def sfx_select():
    s = Synth(0.2)
    s.tone(0, 0.08, 880, amp=0.3, decay=0.06, shape="tri")
    s.tone(0.06, 0.13, 1320, amp=0.3, decay=0.09, shape="tri")
    return s.out(0.45)


def sfx_cancel():
    s = Synth(0.2)
    s.tone(0, 0.08, 660, amp=0.3, decay=0.06, shape="tri")
    s.tone(0.06, 0.12, 440, amp=0.3, decay=0.08, shape="tri")
    return s.out(0.4)


def sfx_hit():
    s = Synth(0.45, seed=3)
    s.noise(0, 0.08, amp=0.5, decay=0.03, lp0=0.9, lp1=0.4)                   # the bite of the blade
    s.tone(0, 0.25, 140, 55, amp=0.9, attack=0.001, decay=0.12)              # body thump
    s.noise(0, 0.3, amp=0.35, decay=0.12, lp0=0.15)                          # muffled impact
    for f in (1240, 2730, 3960):                                             # metal ring
        s.tone(0.004, 0.4, f, amp=0.05, decay=0.12)
    return s.out(0.8)


def sfx_arrow():
    s = Synth(0.4, seed=4)
    s.noise(0, 0.12, amp=0.25, attack=0.04, decay=0.08, lp0=0.2, lp1=0.6, hp=0.05)  # flight
    s.noise(0.1, 0.06, amp=0.5, decay=0.02, lp0=0.7)                                # thunk
    s.tone(0.1, 0.2, 180, 90, amp=0.7, decay=0.08)
    return s.out(0.75)


def sfx_fire():
    s = Synth(0.9, seed=5)
    s.noise(0, 0.85, amp=0.6, attack=0.12, decay=0.5, lp0=0.05, lp1=0.25, crackle=0.004)
    s.noise(0, 0.6, amp=0.25, attack=0.05, decay=0.3, lp0=0.6, hp=0.2, crackle=0.01)
    s.tone(0, 0.6, 70, 110, amp=0.5, attack=0.1, decay=0.4)
    return s.out(0.8)


def sfx_miss():
    s = Synth(0.35, seed=6)
    s.noise(0, 0.3, amp=0.5, attack=0.1, decay=0.15, lp0=0.08, lp1=0.5, hp=0.05)
    return s.out(0.5)


def sfx_death():
    s = Synth(1.2, seed=7)
    s.tone(0, 1.1, 330, 70, amp=0.5, attack=0.01, decay=0.7, shape="tri")
    s.tone(0, 1.1, 335, 72, amp=0.3, attack=0.01, decay=0.7)
    s.noise(0, 1.0, amp=0.3, attack=0.2, decay=0.6, lp0=0.08, lp1=0.02)
    return s.out(0.6)


def sfx_drop():
    s = Synth(0.25, seed=8)
    s.tone(0, 0.2, 220, 110, amp=0.6, decay=0.08)
    s.noise(0, 0.08, amp=0.3, decay=0.03, lp0=0.3)
    return s.out(0.5)


def sfx_stat():
    s = Synth(0.5)
    s.tone(0, 0.45, 1760, amp=0.3, decay=0.25, partials=[(1, 1), (2.76, 0.4), (5.4, 0.15)])
    return s.out(0.4)


# ---------------------------------------------------------------------------


def to_qoa(wav, qoa):
    subprocess.run([os.path.join(ROOT, "build", "qoaconv"), wav, qoa], check=True)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sf = sys.argv[1]
    os.makedirs(OUT, exist_ok=True)
    tmp = tempfile.mkdtemp()

    # Map theme: render two loops (plus room for the reverb tail) and keep the second, so the
    # start of the file already carries the end of the previous pass and loops seamlessly.
    rendered = {}
    for version in ("rain", "blaze"):
        tracks, beats = theme_tracks(version)
        print(f"rendering theme ({version})...", flush=True)
        audio = render(sf, tracks, TEMPO_US, beats * 2 + 8)
        spb = RATE * TEMPO_US // 1000000
        start, length = beats * spb, beats * spb
        rendered[version] = audio[2 * start: 2 * (start + length)]
    # Blaze peaks near full scale; rain sits a few dB under it so the crossfade swells without
    # the map music getting lost.
    gains = {"blaze": 0.89 / peak(rendered["blaze"])}
    target = rms(rendered["blaze"]) * gains["blaze"] * 10 ** (-5 / 20)
    gains["rain"] = min(target / rms(rendered["rain"]), 0.89 / peak(rendered["rain"]))
    for version, audio in rendered.items():
        path = os.path.join(tmp, f"theme_{version}.wav")
        write_wav(path, scale(audio, gains[version]))
        to_qoa(path, os.path.join(OUT, f"theme_{version}.qoa"))

    oneshots = {
        "victory": (victory_tracks, 120, 0.85), "defeat": (defeat_tracks, 70, 0.75),
        "crit": (crit_tracks, 120, 0.9), "levelup": (levelup_tracks, 120, 0.7),
        "heal": (heal_tracks, 120, 0.65), "phase_player": (lambda: phase_tracks(True), 120, 0.6),
        "phase_enemy": (lambda: phase_tracks(False), 120, 0.65),
    }
    for name, (make, bpm, level) in oneshots.items():
        print(f"rendering {name}...", flush=True)
        tracks, beats = make()
        audio = trim_tail(render(sf, tracks, 60000000 // bpm, beats))
        path = os.path.join(tmp, name + ".wav")
        write_wav(path, scale(audio, level / peak(audio)))
        to_qoa(path, os.path.join(OUT, name + ".qoa"))

    synths = {"cursor": sfx_cursor, "select": sfx_select, "cancel": sfx_cancel, "hit": sfx_hit,
              "arrow": sfx_arrow, "fire": sfx_fire, "miss": sfx_miss, "death": sfx_death,
              "drop": sfx_drop, "stat": sfx_stat}
    for name, make in synths.items():
        write_wav(os.path.join(OUT, name + ".wav"), make(), channels=1)
    print("done:", OUT)


if __name__ == "__main__":
    main()
