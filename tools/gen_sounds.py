#!/usr/bin/env python3
"""Generates the sound effects in assets/sounds/ (mono 16-bit 22.05 kHz WAV, procedural, public domain / self-made).

Deterministic (fixed seed): re-running rewrites identical files. No downloaded content.
"""
import math, os, random, struct, wave

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "sounds")
SR = 22050
rng = random.Random(1234)


def n(sec):
    return int(sec * SR)


def env(i, total, attack=0.004, decay=4.0):
    t = i / SR
    a = min(1.0, t / attack) if attack > 0 else 1.0
    return a * math.exp(-decay * i / total)


def sine(freq, sec, decay=5.0, amp=1.0, attack=0.003):
    total = n(sec)
    return [amp * math.sin(2 * math.pi * freq * i / SR) * env(i, total, attack, decay) for i in range(total)]


def noise(sec, decay=6.0, amp=1.0, lp=0.5):
    # one-pole low-pass: lp near 0 = dark, 1 = white
    total, y, out = n(sec), 0.0, []
    for i in range(total):
        y += lp * (rng.uniform(-1, 1) - y)
        out.append(amp * y * env(i, total, 0.002, decay))
    return out


def saw(freq, sec, amp=1.0, attack=0.05, release=0.15):
    total, out = n(sec), []
    for i in range(total):
        t = i / SR
        ph = (freq * t) % 1.0
        v = (2 * ph - 1) * 0.6 + math.sin(2 * math.pi * freq * t) * 0.4
        a = min(1.0, t / attack) * min(1.0, (sec - t) / release)
        out.append(amp * v * a)
    return out


def mix(*parts, offsets=None):
    length = max(len(p) + (offsets[k] if offsets else 0) for k, p in enumerate(parts))
    out = [0.0] * length
    for k, p in enumerate(parts):
        o = offsets[k] if offsets else 0
        for i, v in enumerate(p):
            out[o + i] += v
    return out


def save(name, samples, gain=0.8):
    peak = max(1e-6, max(abs(v) for v in samples))
    data = b"".join(struct.pack("<h", int(max(-1, min(1, v / peak * gain)) * 32767)) for v in samples)
    with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data)


def chirp(f0, f1, sec, decay=4.0, amp=1.0):
    total, ph, out = n(sec), 0.0, []
    for i in range(total):
        f = f0 + (f1 - f0) * i / total
        ph += 2 * math.pi * f / SR
        out.append(amp * math.sin(ph) * env(i, total, 0.003, decay))
    return out


os.makedirs(OUT, exist_ok=True)
save("ui_click", mix(sine(1300, 0.05, 9), noise(0.01, 3, 0.4, 0.9)))
save("ui_hover", sine(820, 0.03, 8), 0.45)
save("chop", mix(noise(0.09, 7, 1.0, 0.35), sine(170, 0.1, 9, 0.8)))
save("mine", mix(sine(1850, 0.18, 12, 0.7), sine(2790, 0.12, 14, 0.4), noise(0.02, 5, 0.6, 0.9)))
save("build", mix(sine(85, 0.22, 9), noise(0.12, 8, 0.7, 0.25), sine(320, 0.06, 20, 0.3)))
save("hit", mix(noise(0.08, 8, 0.9, 0.5), sine(140, 0.1, 10, 0.9)))
save("horn", mix(saw(196, 0.9, 1.0), saw(294, 0.9, 0.7)), 0.7)
save("victory", mix(*[sine(f, 0.45, 4, 0.8, 0.005) for f in (523, 659, 784, 1047)],
                    offsets=[0, n(0.11), n(0.22), n(0.33)]))
save("coin", mix(sine(1318, 0.09, 12), sine(1760, 0.22, 7), offsets=[0, n(0.07)]))
save("ship_horn", mix(saw(98, 1.3, 1.0, 0.1, 0.4), saw(147, 1.3, 0.6, 0.1, 0.4)), 0.7)
save("explosion", mix(noise(0.9, 4, 1.0, 0.18), sine(52, 0.7, 4, 1.0)), 0.9)
save("destroy", mix(noise(0.6, 5, 1.0, 0.3), sine(70, 0.4, 7, 0.8), noise(0.25, 9, 0.5, 0.7)), 0.85)
save("arrow", chirp(1400, 380, 0.14, 5, 0.5))
save("splash", mix(noise(0.3, 7, 0.9, 0.7), chirp(500, 200, 0.12, 8, 0.3)), 0.7)
save("heal", mix(*[sine(f, 0.3, 6, 0.6) for f in (880, 1175, 1568)], offsets=[0, n(0.06), n(0.12)]), 0.6)
print("wrote", sorted(os.listdir(OUT)))
