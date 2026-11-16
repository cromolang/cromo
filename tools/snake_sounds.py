"""Writes the sounds of examples/25_snake.cro in examples/sounds: four effects as WAV
and a short music as Ogg Vorbis, all made here from sums of waves, so that they are
of the project. The music needs ffmpeg with libvorbis.

    python tools/snake_sounds.py
"""
import math
import os
import struct
import subprocess
import wave

RATE = 22050
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'examples', 'sounds')

NOTES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def hertz(name):
    """'A4' is 440; '-' is a rest."""
    if name == '-':
        return 0.0
    step = NOTES[name[0]] + (1 if '#' in name else 0)
    octave = int(name[-1])
    return 440.0 * 2 ** ((step - 9) / 12 + octave - 4)


def voice(kind, phase):
    """A wave at a phase in turns: a soft square, a triangle or a sine."""
    turn = phase % 1.0
    if kind == 'square':
        return (math.sin(2 * math.pi * turn) + math.sin(6 * math.pi * turn) / 3 + math.sin(10 * math.pi * turn) / 5) * 0.9
    if kind == 'triangle':
        return 4 * abs(turn - 0.5) - 1
    return math.sin(2 * math.pi * turn)


def note(samples, start, name, seconds, kind, volume, glide=1.0, decay=4.0):
    """Adds a note to the samples: its pitch can glide to `glide` times, and it fades."""
    base = hertz(name)
    if base == 0:
        return
    count = int(seconds * RATE)
    phase = 0.0
    for i in range(count):
        part = i / count
        phase += base * (1 + (glide - 1) * part) / RATE
        attack = min(1.0, i / (RATE * 0.004))
        release = min(1.0, (count - i) / (RATE * 0.02))
        at = start + i
        if at < len(samples):
            samples[at] += volume * attack * release * math.exp(-decay * part) * voice(kind, phase)


def write(name, samples):
    peak = max(1.0, max(abs(s) for s in samples))
    data = b''.join(struct.pack('<h', int(32767 * 0.85 * s / peak)) for s in samples)
    path = os.path.join(OUT, name)
    with wave.open(path, 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(data)
    return path


def effect(name, notes, kind='square'):
    """notes: (name, start in seconds, seconds, volume, glide)."""
    length = max(start + seconds for _, start, seconds, _, _ in notes)
    samples = [0.0] * int(length * RATE + 1)
    for pitch, start, seconds, volume, glide in notes:
        note(samples, int(start * RATE), pitch, seconds, kind, volume, glide)
    write(name, samples)


def music():
    """Sixteen bars in C major, a tune over a bass and a soft chord, which ends where it starts."""
    beat = 60 / 132
    tune = ('E5 G5 C6 G5 | A5 G5 E5 - | D5 F5 A5 F5 | G5 E5 D5 - | E5 G5 C6 E6 | D6 C6 A5 - | F5 A5 G5 E5 | D5 E5 C5 - | '
            'G5 E5 G5 C6 | B5 A5 G5 - | A5 F5 A5 D6 | C6 B5 A5 - | E5 G5 C6 G5 | A5 C6 B5 G5 | F5 A5 G5 D5 | E5 D5 C5 -')
    chords = 'C C F G C A F G C G F F C A G C'.split()
    roots = {'C': ('C3', 'E4', 'G4'), 'F': ('F2', 'F4', 'A4'), 'G': ('G2', 'D4', 'G4'), 'A': ('A2', 'E4', 'A4')}
    bars = [bar.split() for bar in tune.split('|')]
    samples = [0.0] * int(len(bars) * 4 * beat * RATE + 1)
    for b, bar in enumerate(bars):
        bass, low, high = roots[chords[b]]
        for q, pitch in enumerate(bar):
            at = int((b * 4 + q) * beat * RATE)
            note(samples, at, pitch, beat * 0.95, 'square', 0.36, decay=3.0)
            note(samples, at, bass if q % 2 == 0 else bass[0] + str(int(bass[1]) + 1), beat * 0.9, 'triangle', 0.5, decay=2.0)
            note(samples, at + int(beat * RATE / 2), low if q % 2 == 0 else high, beat * 0.45, 'sine', 0.16, decay=2.0)
    path = write('music.wav', samples)
    made = os.path.join(OUT, 'music.ogg')
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', path, '-c:a', 'libvorbis', '-q:a', '2', made], check=True)
    os.remove(path)


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    effect('eat.wav', [('E5', 0, 0.07, 0.8, 1.0), ('A5', 0.05, 0.11, 0.8, 1.25)])
    effect('gem.wav', [('C6', 0, 0.09, 0.6, 1), ('E6', 0.07, 0.09, 0.6, 1), ('G6', 0.14, 0.09, 0.6, 1), ('C7', 0.21, 0.28, 0.7, 1)], 'triangle')
    effect('start.wav', [('C5', 0, 0.09, 0.7, 1), ('E5', 0.09, 0.09, 0.7, 1), ('G5', 0.18, 0.2, 0.7, 1)])
    effect('over.wav', [('G4', 0, 0.16, 0.8, 0.9), ('E4', 0.16, 0.16, 0.8, 0.9), ('C4', 0.32, 0.5, 0.8, 0.6)], 'triangle')
    music()
    for name in sorted(os.listdir(OUT)):
        print(name, os.path.getsize(os.path.join(OUT, name)))
