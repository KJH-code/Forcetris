"""Synthesises the game's sound effects into sound/.

The effects are generated rather than sampled so that the repository carries no
third-party audio and every cue can be retuned by editing a number here and
re-running. Standard library only.

Run with: python tools/make_sounds.py
"""
import os
import math
import wave
import array
import random

RATE = 44100
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'sound')

# Seeded so that regenerating the files produces byte-identical noise.
rng = random.Random(20260812)


def envelope (i, count, attack=0.01, release=0.35):
    """Linear attack into an exponential-ish decay, as a 0.0 to 1.0 gain."""
    pos = i / count
    if pos < attack:
        return pos / attack
    tail = (pos - attack) / (1. - attack)
    return (1. - tail) ** (1. + 3. * release)


def tone (freq, dur, vol=0.4, shape='square', freq_end=None, attack=0.01, release=0.35, vibrato=0.):
    """One note. freq_end sweeps the pitch across the note; vibrato wobbles it."""
    count = max(1, int(RATE * dur))
    out = []
    phase = 0.
    for i in range(count):
        pos = i / count
        f = freq if freq_end is None else freq + (freq_end - freq) * pos
        if vibrato:
            f *= 1. + vibrato * math.sin(2. * math.pi * 38. * i / RATE)
        phase += 2. * math.pi * f / RATE
        if shape == 'square':
            sample = 1. if math.sin(phase) >= 0. else -1.
        elif shape == 'saw':
            sample = 2. * ((phase / (2. * math.pi)) % 1.) - 1.
        elif shape == 'noise':
            sample = rng.uniform(-1., 1.)
        else:
            sample = math.sin(phase)
        out.append(sample * vol * envelope(i, count, attack, release))
    return out


def silence (dur):
    return [0.] * int(RATE * dur)


def mix (*layers):
    """Overlay tracks of differing lengths."""
    out = [0.] * max(len(layer) for layer in layers)
    for layer in layers:
        for i, sample in enumerate(layer):
            out[i] += sample
    return out


def chain (*parts):
    out = []
    for part in parts:
        out.extend(part)
    return out


def write (name, samples):
    data = array.array('h')
    for sample in samples:
        # Clamp rather than wrap, so an over-enthusiastic mix distorts instead of tearing.
        data.append(int(max(-1., min(1., sample)) * 32767))
    path = os.path.join(OUT, name + '.wav')
    with wave.open(path, 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(data.tobytes())
    print('{:>10}.wav  {:>6} frames  {:.0f}ms'.format(name, len(data), 1000. * len(data) / RATE))


SOUNDS = {
    # Shifting and rotating fire constantly, so they stay short and quiet.
    'move': lambda: tone(180, 0.035, 0.22, 'square', release=0.6),
    'rotate': lambda: tone(300, 0.045, 0.24, 'square', freq_end=340, release=0.5),
    'hold': lambda: tone(260, 0.09, 0.26, 'square', freq_end=390, release=0.4),
    # Landing under gravity is a soft click; a hard drop is a thud with some body.
    'lock': lambda: mix(
        tone(130, 0.07, 0.28, 'square', release=0.7),
        tone(90, 0.05, 0.12, 'noise', release=0.9),
    ),
    'drop': lambda: mix(
        tone(400, 0.13, 0.38, 'square', freq_end=90, release=0.5),
        tone(120, 0.08, 0.18, 'noise', release=0.8),
    ),
    # The timer firing has to be unmistakably not a hard drop you chose to make:
    # a harsh two-tone buzz, pitched below every other cue.
    'forced': lambda: chain(
        tone(330, 0.085, 0.5, 'saw', release=0.15, vibrato=0.05),
        tone(165, 0.13, 0.5, 'saw', release=0.4, vibrato=0.07),
    ),
    # Clears climb; a tetris climbs further and rings out.
    'clear': lambda: chain(
        tone(523, 0.06, 0.34, 'sine', release=0.5),
        tone(659, 0.06, 0.34, 'sine', release=0.5),
        tone(784, 0.10, 0.34, 'sine', release=0.4),
    ),
    'tetris': lambda: chain(
        tone(523, 0.07, 0.4, 'sine', release=0.4),
        tone(659, 0.07, 0.4, 'sine', release=0.4),
        tone(784, 0.07, 0.4, 'sine', release=0.4),
        mix(
            tone(1047, 0.22, 0.4, 'sine', release=0.3),
            tone(1568, 0.22, 0.16, 'sine', release=0.3),
        ),
    ),
    # Rarer and better than a tetris, so it rings higher and longer.
    'perfect': lambda: chain(
        tone(659, 0.07, 0.4, 'sine', release=0.4),
        tone(880, 0.07, 0.4, 'sine', release=0.4),
        tone(1047, 0.07, 0.4, 'sine', release=0.4),
        tone(1319, 0.07, 0.4, 'sine', release=0.4),
        mix(
            tone(1760, 0.30, 0.36, 'sine', release=0.25),
            tone(2637, 0.30, 0.14, 'sine', release=0.25),
            tone(1319, 0.30, 0.22, 'sine', release=0.25),
        ),
    ),
    'tspin': lambda: mix(
        tone(784, 0.28, 0.3, 'sine', release=0.25),
        tone(1175, 0.28, 0.2, 'sine', release=0.2),
        tone(1568, 0.20, 0.12, 'sine', release=0.2),
    ),
    'gameover': lambda: chain(
        tone(392, 0.16, 0.4, 'square', release=0.3),
        tone(330, 0.16, 0.4, 'square', release=0.3),
        tone(262, 0.16, 0.4, 'square', release=0.3),
        tone(196, 0.40, 0.4, 'square', release=0.25, vibrato=0.02),
    ),
}

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    for name in sorted(SOUNDS):
        write(name, SOUNDS[name]())
    print('\nWrote {} effects to {}'.format(len(SOUNDS), OUT))
