"""Synthesises the game's sound effects into sound/.

The effects are generated rather than sampled so that the repository carries no
third-party audio and every cue can be retuned by editing a number here and
re-running. Standard library only.

The voice is a forge: struck metal, a room with stone walls, and weight
underneath. Three tiers, and the tier decides how much of that a cue gets.

  tight   move, rotate, hold, lock, finesse, fusewarn
          These fire several times a second. They stay as short and as quiet
          as they always were - only the tone is warmed, never the length.
          Written mono, because width on a 35ms tick is smear, not space.
  weight  drop, hit, burn, forced
          A body underneath and a small room around it. Something landed.
  grand   the clears, the spins, Overdrive, the combo ladder, game over
          Bell partials, sub, and a tail that rings out into the room.

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

# Re-seeded per effect (see __main__) so that regenerating produces
# byte-identical noise AND adding a new effect never shifts the noise of
# the ones already shipped - each file's randomness is its own.
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
        elif shape == 'tri':
            ramp = (phase / (2. * math.pi)) % 1.
            sample = 4. * abs(ramp - 0.5) - 1.
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


def after (dur, samples):
    """The same track, starting `dur` seconds in."""
    return silence(dur) + list(samples)


# --- The room ---------------------------------------------------------------
# Everything below is one-pole filters and delay lines, which is all a forge
# needs: something to eat the chip-era top end, and something to give the
# strike a wall to come back off.

def lowpass (samples, cutoff, poles=1):
    """A one-pole low pass, run `poles` times. Takes the glare off a square."""
    coeff = math.exp(-2. * math.pi * cutoff / RATE)
    out = list(samples)
    for _ in range(poles):
        held = 0.
        for i, sample in enumerate(out):
            held = sample * (1. - coeff) + held * coeff
            out[i] = held
    return out


def highpass (samples, cutoff):
    """A one-pole high pass. Keeps sub layers from muddying each other."""
    coeff = math.exp(-2. * math.pi * cutoff / RATE)
    out = [0.] * len(samples)
    last_in = 0.
    last_out = 0.
    for i, sample in enumerate(samples):
        last_out = coeff * (last_out + sample - last_in)
        last_in = sample
        out[i] = last_out
    return out


def comb (samples, delay, feedback):
    """A feedback comb: the same sound arriving again, quieter, forever."""
    out = list(samples)
    buffer = [0.] * delay
    at = 0
    for i, sample in enumerate(out):
        echo = buffer[at]
        buffer[at] = sample + echo * feedback
        at = (at + 1) % delay
        out[i] = echo
    return out


def allpass (samples, delay, feedback):
    """Smears the comb echoes so they read as a room and not as a slapback."""
    out = list(samples)
    buffer = [0.] * delay
    at = 0
    for i, sample in enumerate(out):
        held = buffer[at]
        buffer[at] = sample + held * feedback
        at = (at + 1) % delay
        out[i] = held - sample * feedback
    return out


# Schroeder's delay lengths, mutually prime so the echoes never line up.
COMB_DELAYS = (1116, 1188, 1277, 1356)
ALLPASS_DELAYS = (556, 441)


def room (samples, size=1., decay=0.72, wet=0.34, tail=0.55, damp=5200.):
    """The forge hall around a sound: four combs, two allpasses, a stone roof.

    `tail` is how much silence to hang off the end for the room to ring into;
    trim() shaves off whatever of it turns out to be inaudible.
    """
    work = list(samples) + silence(tail)
    wash = [0.] * len(work)
    for delay in COMB_DELAYS:
        part = comb(work, max(8, int(delay * size)), decay)
        for i, sample in enumerate(part):
            wash[i] += sample * 0.25
    for delay in ALLPASS_DELAYS:
        wash = allpass(wash, max(8, int(delay * size)), 0.5)
    wash = lowpass(wash, damp)
    return [work[i] + wash[i] * wet for i in range(len(work))]


def normalise (samples, level, window=0.1, ceiling=0.86):
    """Scale a track so the first `window` seconds average out to `level`.

    Inharmonic partials sum differently at every fundamental, so a ladder
    built by raising the pitch and the gain together still comes out lumpy -
    one rung nearly twice as loud as the next by accident. Matching the
    strike's own loudness means the ramp a rung is *given* is the ramp you
    hear. Accepts a mono track or a (left, right) pair, which it scales
    together so the stereo image survives. `ceiling` keeps the peak off the
    clamp no matter what the strike measures.
    """
    pair = samples if isinstance(samples, tuple) else (samples,)
    count = min(min(len(track) for track in pair), max(1, int(RATE * window)))
    energy = sum(sample * sample for track in pair for sample in track[:count])
    strike = math.sqrt(energy / (count * len(pair)))
    if strike <= 0.:
        return samples
    scale = level / strike
    loudest = max((abs(sample) for track in pair for sample in track), default=0.)
    if loudest * scale > ceiling:
        scale = ceiling / loudest
    scaled = tuple([sample * scale for sample in track] for track in pair)
    return scaled if isinstance(samples, tuple) else scaled[0]


def trim (samples, floor=0.0016):
    """Cut the dead tail. Reverb pads generously; the file need not."""
    last = 0
    for i, sample in enumerate(samples):
        if abs(sample) > floor:
            last = i
    return samples[:last + 1] if last else samples


# --- Voices -----------------------------------------------------------------

# Chowning's bell ratios: inharmonic, which is exactly why struck metal reads
# as metal and a stack of harmonics reads as an organ.
BELL = (1., 2.76, 5.40, 8.93, 13.34)
# Shorter, denser, more like a bar being hit than a bell being rung.
ANVIL = (1., 2.32, 3.83, 6.21, 9.10)


def metal (freq, dur, vol=0.34, ratios=BELL, release=0.45, strike=0.35):
    """Struck metal: partials that decay faster the higher they sit, over a
    noise strike short enough to read as the hammer rather than as hiss."""
    layers = []
    for step, ratio in enumerate(ratios):
        layers.append(tone(
            freq * ratio, dur * (1. - 0.11 * step), vol / (1.5 + 1.15 * step),
            'sine', attack=0.002, release=release + 0.3 * step))
    if strike > 0.:
        layers.append(lowpass(
            tone(freq * 4., 0.018, vol * strike, 'noise', release=1.4), 6500.))
    return mix(*layers)


# --- The mechanical family --------------------------------------------------
#
# What was missing was not richness - metal() has inharmonic partials and the
# room has a real reverb. What was missing is that every cue began with a ten
# millisecond ramp, which is a note being played rather than a thing being
# struck. A mechanism has a hard edge at sample zero and is over before the
# ear has finished deciding what it was.
#
# Three ingredients, and they are the whole difference:
#
#   snap()   - the transient. Two to four milliseconds of band-passed noise
#              at full amplitude from the first sample. This is the click,
#              and it is what makes a sound read as a machine rather than as
#              an instrument.
#   servo()  - a fast pitch sweep. A mechanism moving is a frequency falling
#              (or rising) far and quickly; a steady pitch is a bell.
#   detent() - the stop at the end of the travel. A short damped tone with
#              no attack at all, the ratchet catching.


def snap (vol=0.5, low=1400., high=7000., dur=0.004):
    """The transient: noise with no attack whatsoever, band-passed so it is a
    click rather than a hiss, and gone in a few milliseconds."""
    count = max(1, int(RATE * dur))
    raw = [rng.uniform(-1., 1.) * vol * ((1. - i / count) ** 2.)
           for i in range(count)]
    return highpass(lowpass(raw, high, 2), low)


def servo (f0, f1, dur, vol=0.3, shape='saw', release=0.55):
    """A mechanism travelling: pitch falling (or climbing) fast, and no ramp
    on the front - the movement starts the instant the key is pressed."""
    return lowpass(
        tone(f0, dur, vol, shape, freq_end=f1, attack=0.0004, release=release),
        5200., 2)


def detent (freq, dur=0.05, vol=0.3, release=0.9):
    """The catch at the end of the travel: a struck tone with no attack, damped
    hard, so it stops rather than rings."""
    return tone(freq, dur, vol, 'sine', attack=0.0004, release=release)


def sub (freq, dur, vol=0.3, freq_end=None, release=0.5):
    """The weight under a hit. Sine, low, and filtered so it stays felt."""
    return lowpass(
        tone(freq, dur, vol, 'sine', freq_end=freq_end, attack=0.004, release=release),
        340.)


def widen (samples, ms=9., spread=0.55):
    """Haas width: the same sound reaching one ear a hair later than the other."""
    delay = max(1, int(RATE * ms / 1000.))
    left = list(samples) + [0.] * delay
    right = [0.] * delay + list(samples)
    keep = 1. - 0.5 * spread
    bleed = 0.5 * spread
    return (
        [left[i] * keep + right[i] * bleed for i in range(len(left))],
        [right[i] * keep + left[i] * bleed for i in range(len(left))],
    )


def write (name, samples):
    """Write one effect. A plain list is mono; a (left, right) pair is stereo."""
    if isinstance(samples, tuple):
        left, right = samples
        channels = 2
    else:
        left = right = samples
        channels = 1
    count = max(len(left), len(right))
    data = array.array('h')
    for i in range(count):
        pair = (left[i] if i < len(left) else 0.,) if channels == 1 else (
            left[i] if i < len(left) else 0., right[i] if i < len(right) else 0.)
        for sample in pair:
            # Clamp rather than wrap, so an over-enthusiastic mix distorts
            # instead of tearing.
            data.append(int(max(-1., min(1., sample)) * 32767))
    path = os.path.join(OUT, name + '.wav')
    with wave.open(path, 'wb') as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(data.tobytes())
    peak = max((abs(sample) for sample in data), default=0) / 32767.
    print('{:>14}.wav  {:>6} frames  {:>6.0f}ms  {}  peak {:.2f}'.format(
        name, count, 1000. * count / RATE, 'stereo' if channels == 2 else '  mono', peak))


# How many rungs the combo ladder has before it stops climbing. Mirrored by
# engine.environment.COMBO_STEPS, which names the files this writes.
COMBO_STEPS = 10


def combo_step (step):
    """One rung of the ladder: the anvil struck a semitone higher than the
    last, ringing a little longer each time the chain survives."""
    freq = 261.63 * (2. ** (step / 12.))
    grow = step / (COMBO_STEPS - 1.)
    return normalise(widen(trim(room(
        mix(
            # The rung gets louder as well as higher: a chain that keeps
            # going should sound like it is winning, not just climbing.
            metal(freq, 0.30 + 0.34 * grow, 0.26 + 0.17 * grow, ANVIL, release=0.5),
            sub(freq * 0.5, 0.13 + 0.06 * grow, 0.16 + 0.12 * grow, release=0.7),
        ),
        size=0.85, decay=0.68 + 0.14 * grow, wet=0.26 + 0.16 * grow,
        tail=0.35 + 0.5 * grow)),
        ms=7. + 4. * grow, spread=0.35 + 0.3 * grow), 0.075 + 0.075 * grow)


SOUNDS = {'combo{}'.format(step + 1): (lambda s=step: combo_step(s)) for step in range(COMBO_STEPS)}
SOUNDS.update({
    # --- tight ---------------------------------------------------------------
    # Shifting and rotating fire constantly, so they stay short and quiet -
    # the same lengths and volumes they have always had. All that changed is
    # that the square is filtered, so it reads as a tap on metal rather than
    # as a beep.
    # A key press is a switch closing: the click first, the travel under it,
    # and nothing left ringing. Shorter than what they replaced, and they
    # start at full amplitude on sample zero - which is the whole
    # difference between a mechanism and a beep.
    'move': lambda: mix(
        snap(0.42, 1800., 8000., 0.0035),
        servo(340, 190, 0.030, 0.17, 'saw', release=0.8),
    ),
    'rotate': lambda: mix(
        snap(0.40, 2200., 9000., 0.0035),
        servo(520, 300, 0.038, 0.16, 'saw', release=0.75),
        after(0.026, detent(620, 0.030, 0.13, release=1.1)),
    ),
    # The box: a longer travel, and a heavier catch at the end of it.
    'hold': lambda: mix(
        snap(0.44, 1200., 7000., 0.005),
        servo(300, 520, 0.060, 0.18, 'saw', release=0.6),
        after(0.052, detent(392, 0.055, 0.20, release=0.8)),
        sub(84, 0.07, 0.12, release=0.7),
    ),
    # Landing under gravity: a click, a short fall, and the smallest thump.
    'lock': lambda: mix(
        snap(0.40, 900., 5200., 0.005),
        servo(230, 120, 0.045, 0.20, 'saw', release=0.75),
        lowpass(tone(90, 0.04, 0.09, 'noise', attack=0.0004, release=1.1),
            3000.),
        sub(62, 0.06, 0.16, release=0.8),
    ),
    # A wasted press. Deliberately small and dry - a fault is information, not a
    # punishment, and something triumphant here would be unbearable by the tenth
    # one. Two descending clicks, well under the clear cues so it never competes
    # with the placement that fired at the same moment.
    'finesse': lambda: lowpass(chain(
        mix(snap(0.22, 1600., 6000., 0.003),
            detent(370, 0.045, 0.16, release=0.9)),
        mix(snap(0.20, 1400., 5200., 0.003),
            detent(247, 0.075, 0.16, release=0.9)),
    ), 2600., 2),
    # The fuse running short: an urgent double tick, quiet enough to fire on
    # most pieces without wearing the ear down. Metal, so it belongs to the
    # forge, but tiny.
    'fusewarn': lambda: chain(
        metal(1175, 0.030, 0.15, ANVIL, release=1.1, strike=0.),
        silence(0.035),
        metal(1175, 0.030, 0.15, ANVIL, release=1.1, strike=0.),
    ),

    # --- the player's tools ---------------------------------------------
    # Three charges, and they have to be told apart with the eyes on the
    # board. So each is a different MECHANISM rather than a different note:
    # a blade, a valve, a striker. All three open with the same hard
    # transient the keys do, because they are all things the player pressed.
    #
    # The Shear: a blade drawn and closed. Metal sliding, then the cut.
    'tool_shear': lambda: widen(trim(room(mix(
        snap(0.55, 2400., 11000., 0.004),
        highpass(tone(2600, 0.10, 0.16, 'noise', attack=0.0006,
            release=0.8), 1800.),
        servo(1800, 420, 0.085, 0.22, 'saw', release=0.6),
        after(0.075, mix(
            snap(0.6, 1200., 9000., 0.006),
            metal(784., 0.16, 0.22, ANVIL, release=0.6, strike=0.5),
            sub(70, 0.10, 0.20, release=0.6))),
    ), size=0.8, decay=0.62, wet=0.20, tail=0.3)), ms=9., spread=0.45),
    # The Cull: a vent thrown open and the pressure leaving. A hiss that
    # falls away rather than a strike - nothing was hit, something was let
    # go.
    'tool_cull': lambda: widen(trim(room(mix(
        snap(0.5, 700., 5200., 0.006),
        servo(240, 96, 0.070, 0.24, 'square', release=0.7),
        after(0.05, lowpass(tone(1400, 0.30, 0.20, 'noise', attack=0.004,
            release=0.35), 3200., 2)),
        after(0.05, highpass(tone(2600, 0.26, 0.09, 'noise', attack=0.01,
            release=0.5), 2400.)),
        sub(52, 0.20, 0.24, freq_end=38, release=0.5),
    ), size=1.1, decay=0.7, wet=0.26, tail=0.45)), ms=13., spread=0.6),
    # The Flare: a striker, a catch, and the fire taking. The one of the
    # three that gets brighter as it goes instead of dying away.
    'tool_flare': lambda: widen(trim(room(mix(
        snap(0.5, 1800., 9000., 0.004),
        after(0.02, snap(0.45, 1600., 8000., 0.004)),
        after(0.04, mix(
            servo(180, 900, 0.22, 0.22, 'saw', release=0.2),
            lowpass(tone(600, 0.30, 0.22, 'noise', attack=0.18,
                release=0.3), 4200.),
            metal(659.25, 0.26, 0.20, ANVIL, release=0.5, strike=0.3),
            sub(58, 0.24, 0.26, freq_end=88, release=0.35))),
    ), size=1.3, decay=0.76, wet=0.30, tail=0.55)), ms=15., spread=0.7),

    # --- a boss winding up ---------------------------------------------------
    # The fuse's warning is deliberately tiny - it fires on most pieces and
    # would wear the ear down. A boss announcing itself is the opposite
    # problem: it happens once every twenty seconds and has to be the biggest
    # thing in the room, or the fight reads as nothing happening. A slow low
    # bell, struck twice and left to ring.
    'skillwarn': lambda: widen(trim(room(chain(
        metal(196., 0.34, 0.40, BELL, release=0.75),
        silence(0.10),
        metal(261.63, 0.60, 0.44, BELL, release=0.85),
    ), size=2.2, decay=0.9, wet=0.42, tail=1.2)), ms=21., spread=0.9),
    # The bolt leaving the foe's well, four hundred milliseconds before it
    # arrives. A rise, not a hit: pitch climbing while noise opens up under
    # it, so the ear is leaning forward by the time the thing lands. The
    # warning bell says something is coming; this says it is coming NOW.
    'skillcast': lambda: widen(trim(room(mix(
        tone(120, 0.42, 0.30, 'saw', freq_end=560, attack=0.06,
            release=0.12),
        highpass(tone(400, 0.42, 0.22, 'noise', attack=0.25, release=0.1),
            900.),
        sub(48, 0.40, 0.26, freq_end=96, release=0.14),
    ), size=1.4, decay=0.7, wet=0.28, tail=0.5)), ms=17., spread=0.8),
    # The gate coming down: iron sliding, then the stop at the bottom.
    'skillseal': lambda: widen(trim(room(mix(
        lowpass(tone(140, 0.26, 0.30, 'noise', attack=0.05, release=0.5),
            1400., 2),
        after(0.20, metal(147., 0.30, 0.38, ANVIL, release=0.5)),
        after(0.20, sub(55, 0.28, 0.34, release=0.35)),
    ), size=1.3, decay=0.8, wet=0.3, tail=0.7)), ms=15., spread=0.7),
    # The lamps going out, or the smoke rolling in: a hush that swallows.
    'skilldark': lambda: widen(trim(room(mix(
        lowpass(tone(220, 0.55, 0.26, 'noise', attack=0.3, release=0.45),
            700., 3),
        tone(110, 0.55, 0.20, 'saw', freq_end=62, attack=0.2, release=0.5),
    ), size=2., decay=0.9, wet=0.45, tail=1.)), ms=23., spread=1.),
    # The hammer falling: all weight, no ring.
    'skillheavy': lambda: widen(trim(room(mix(
        sub(41, 0.42, 0.42, freq_end=33, release=0.3),
        lowpass(tone(70, 0.30, 0.30, 'noise', release=0.35), 600., 3),
        metal(98., 0.22, 0.20, ANVIL, release=0.3),
    ), size=1.6, decay=0.85, wet=0.34, tail=0.8)), ms=13., spread=0.6),

    # --- weight --------------------------------------------------------------
    # A hard drop is a thud with body: the old click, a sub under it, and just
    # enough room that the floor sounds like it is made of something.
    'drop': lambda: trim(room(mix(
        snap(0.55, 800., 6500., 0.006),
        servo(700, 120, 0.055, 0.24, 'saw', release=0.7),
        tone(400, 0.13, 0.34, 'square', freq_end=90, release=0.5),
        lowpass(tone(120, 0.08, 0.18, 'noise', release=0.8), 3800.),
        sub(74, 0.17, 0.30, freq_end=52, release=0.55),
    ), size=0.7, decay=0.6, wet=0.18, tail=0.3)),
    # Garbage landing on your floor: blunt, gritty, and clearly not yours.
    'hit': lambda: widen(trim(room(mix(
        snap(0.5, 500., 4200., 0.008),
        tone(80, 0.14, 0.34, 'square', freq_end=55, release=0.5),
        lowpass(tone(150, 0.12, 0.24, 'noise', release=0.7), 2600.),
        sub(46, 0.24, 0.34, freq_end=38, release=0.45),
    ), size=1.1, decay=0.7, wet=0.24, tail=0.4)), ms=11., spread=0.4),
    # The backdraft eating a garbage row: a crackle that collapses inward.
    'burn': lambda: widen(trim(room(mix(
        lowpass(tone(140, 0.20, 0.30, 'noise', release=0.55), 2200.),
        highpass(tone(900, 0.10, 0.09, 'noise', release=1.0), 2000.),
        tone(196, 0.14, 0.16, 'saw', freq_end=88, release=0.5),
        sub(58, 0.16, 0.22, release=0.6),
    ), size=0.95, decay=0.72, wet=0.3, tail=0.45)), ms=13., spread=0.5),
    # Loaded dice landing: the same anvil struck twice, fast, the second
    # blow harder and brighter - a doubled hit that sounds doubled.
    'crit': lambda: widen(trim(room(mix(
        metal(587.33, 0.10, 0.26, ANVIL, release=0.7, strike=0.5),
        after(0.09, metal(880., 0.30, 0.34, ANVIL, release=0.45, strike=0.7)),
        after(0.09, tone(1760., 0.16, 0.08, 'sine', release=0.5)),
        after(0.08, sub(73.4, 0.22, 0.28, release=0.5)),
    ), size=1.1, decay=0.8, wet=0.34, tail=0.7)), ms=11., spread=0.55),
    # Cold iron taking hold: a high glassy ring with a frosted shimmer over
    # it - unmistakably not a clear, because nothing fell. The line locked.
    'freeze': lambda: widen(trim(room(mix(
        metal(1568., 0.30, 0.20, BELL, release=0.5, strike=0.2),
        after(0.05, metal(2093., 0.28, 0.13, BELL, release=0.55, strike=0.)),
        highpass(tone(4200, 0.18, 0.05, 'noise', attack=0.02, release=0.6), 3000.),
        tone(784., 0.12, 0.09, 'sine', release=0.5),
    ), size=1.0, decay=0.78, wet=0.34, tail=0.6)), ms=12., spread=0.6),
    # A cascade: the stack giving way. Three blunt falls in quick succession,
    # each lower than the last, over a gritty rumble - rubble, not metal, so
    # it reads as the board collapsing rather than as something you struck.
    'cascade': lambda: widen(trim(room(mix(
        lowpass(tone(120, 0.36, 0.28, 'noise', release=0.4), 1800.),
        tone(220, 0.10, 0.24, 'square', freq_end=90, release=0.5),
        after(0.09, tone(180, 0.10, 0.26, 'square', freq_end=75, release=0.5)),
        after(0.18, tone(150, 0.12, 0.28, 'square', freq_end=62, release=0.5)),
        after(0.18, sub(52, 0.30, 0.32, freq_end=40, release=0.4)),
    ), size=1.2, decay=0.78, wet=0.32, tail=0.7)), ms=13., spread=0.6),
    # The timer firing has to be unmistakably not a hard drop you chose to
    # make. It is the one accident in the game, so it is the one clang: a bar
    # struck hard, low, with the room behind it.
    'forced': lambda: widen(trim(room(mix(
        metal(196, 0.34, 0.42, ANVIL, release=0.34, strike=0.7),
        tone(165, 0.13, 0.22, 'saw', freq_end=98, release=0.45, vibrato=0.05),
        sub(65, 0.22, 0.32, freq_end=49, release=0.5),
    ), size=1.15, decay=0.76, wet=0.34, tail=0.6)), ms=10., spread=0.5),

    # --- grand ---------------------------------------------------------------
    # Clears climb; a tetris climbs further and rings out; a perfect clear
    # rings longest of all. All three are struck metal now, over a sub, in a
    # room big enough to hear.
    'clear': lambda: widen(trim(room(mix(
        metal(523.25, 0.26, 0.30, BELL, release=0.5),
        after(0.055, metal(659.25, 0.26, 0.26, BELL, release=0.5)),
        after(0.110, metal(783.99, 0.34, 0.28, BELL, release=0.42)),
        sub(130.8, 0.22, 0.22, release=0.6),
    ), size=1., decay=0.76, wet=0.32, tail=0.7)), ms=9., spread=0.45),
    'tetris': lambda: widen(trim(room(mix(
        metal(523.25, 0.22, 0.30, ANVIL, release=0.55),
        after(0.055, metal(659.25, 0.22, 0.28, ANVIL, release=0.55)),
        after(0.110, metal(783.99, 0.22, 0.28, ANVIL, release=0.55)),
        after(0.165, metal(1046.5, 0.60, 0.34, BELL, release=0.3)),
        after(0.165, tone(1568., 0.44, 0.10, 'sine', attack=0.01, release=0.35)),
        sub(65.4, 0.42, 0.32, release=0.4),
    ), size=1.2, decay=0.82, wet=0.36, tail=0.9)), ms=12., spread=0.6),
    # Rarer and better than a tetris, so it rings higher and longer.
    'perfect': lambda: widen(trim(room(mix(
        metal(659.25, 0.20, 0.28, ANVIL, release=0.6),
        after(0.055, metal(880.00, 0.20, 0.28, ANVIL, release=0.6)),
        after(0.110, metal(1046.5, 0.20, 0.28, ANVIL, release=0.6)),
        after(0.165, metal(1318.5, 0.80, 0.34, BELL, release=0.24)),
        after(0.165, metal(1760.0, 0.70, 0.16, BELL, release=0.26, strike=0.)),
        after(0.165, tone(2637., 0.40, 0.07, 'sine', attack=0.02, release=0.4)),
        sub(82.4, 0.55, 0.32, release=0.3),
    ), size=1.35, decay=0.86, wet=0.40, tail=1.2)), ms=14., spread=0.7),
    # A spin: the same metal, but rung rather than struck - it should feel
    # like the piece slid into somewhere it had no business fitting.
    'tspin': lambda: widen(trim(room(mix(
        metal(784., 0.55, 0.26, BELL, release=0.3, strike=0.15),
        tone(1175., 0.40, 0.10, 'sine', attack=0.03, release=0.3, vibrato=0.012),
        after(0.09, metal(1568., 0.34, 0.10, BELL, release=0.4, strike=0.)),
        sub(98., 0.30, 0.22, release=0.5),
    ), size=1.15, decay=0.8, wet=0.36, tail=0.8)), ms=13., spread=0.6),
    # Keeping a back to back going: a fifth, with weight under it, so it reads
    # as separate from the clear it arrives with.
    'b2b': lambda: widen(trim(room(mix(
        tone(392., 0.06, 0.24, 'tri', release=0.5),
        after(0.06, metal(587.33, 0.34, 0.24, BELL, release=0.4)),
        after(0.06, tone(293.66, 0.26, 0.12, 'tri', release=0.45)),
        sub(73.4, 0.28, 0.24, release=0.5),
    ), size=1.05, decay=0.78, wet=0.3, tail=0.65)), ms=10., spread=0.5),
    # A lock inside the Flash window. This one fires often, so it stays a
    # spark: bright, small, and gone - just with a room behind it now.
    'flash': lambda: widen(trim(room(mix(
        metal(1568., 0.12, 0.30, BELL, release=0.7, strike=0.3),
        tone(2349., 0.06, 0.12, 'sine', release=0.6),
        sub(196., 0.09, 0.14, release=0.7),
    ), size=0.8, decay=0.7, wet=0.3, tail=0.35)), ms=8., spread=0.5),
    # Overdrive igniting: air dragged in, then the whole hall lit at once.
    'overdrive': lambda: widen(trim(room(mix(
        lowpass(tone(200, 0.30, 0.22, 'noise', attack=0.6, release=0.15), 2400.),
        tone(196, 0.30, 0.24, 'saw', freq_end=784, attack=0.05, release=0.2),
        after(0.28, metal(392., 0.85, 0.32, ANVIL, release=0.3, strike=0.6)),
        after(0.28, metal(587.33, 0.75, 0.20, BELL, release=0.34, strike=0.)),
        after(0.28, metal(783.99, 0.70, 0.16, BELL, release=0.36, strike=0.)),
        after(0.26, sub(49., 0.75, 0.36, release=0.25)),
    ), size=1.4, decay=0.87, wet=0.42, tail=1.3)), ms=15., spread=0.75),
    # ...and guttering out: the same chord folding back down into the floor.
    'overdrive_end': lambda: widen(trim(room(mix(
        tone(784., 0.16, 0.20, 'saw', freq_end=392, release=0.3),
        after(0.12, metal(392., 0.50, 0.20, BELL, release=0.4, strike=0.15)),
        after(0.12, sub(65.4, 0.45, 0.26, freq_end=49, release=0.4)),
    ), size=1.2, decay=0.8, wet=0.34, tail=0.8)), ms=12., spread=0.55),
    # The other board's Overdrive bearing down: a swell with no strike in it
    # at all, so it arrives as pressure rather than as an event you caused.
    'pressure': lambda: widen(trim(room(mix(
        lowpass(tone(72, 0.75, 0.34, 'noise', attack=0.4, release=0.2), 900., 2),
        tone(98, 0.75, 0.22, 'saw', freq_end=147, attack=0.35, release=0.25, vibrato=0.05),
        sub(49, 0.8, 0.26, freq_end=62, release=0.22),
    ), size=1.5, decay=0.85, wet=0.38, tail=1.)), ms=17., spread=0.8),
    # Game over: the fire going out. Four steps down, the last one held.
    'gameover': lambda: widen(trim(room(mix(
        metal(392., 0.20, 0.26, ANVIL, release=0.6),
        after(0.16, metal(329.63, 0.20, 0.26, ANVIL, release=0.6)),
        after(0.32, metal(261.63, 0.20, 0.26, ANVIL, release=0.6)),
        after(0.48, metal(196., 0.90, 0.30, BELL, release=0.26)),
        after(0.48, tone(196., 0.60, 0.12, 'saw', release=0.3, vibrato=0.02)),
        after(0.46, sub(49., 0.85, 0.30, release=0.25)),
    ), size=1.45, decay=0.86, wet=0.40, tail=1.2)), ms=14., spread=0.65),
})

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    total = 0
    for name in sorted(SOUNDS):
        rng.seed('forcetris-' + name)
        write(name, SOUNDS[name]())
        total += os.path.getsize(os.path.join(OUT, name + '.wav'))
    print('\nWrote {} effects to {}  ({:.1f} MB)'.format(
        len(SOUNDS), OUT, total / (1024. * 1024.)))
