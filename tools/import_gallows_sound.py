"""Import the neck-break recording for the gallows (standard library only, no NumPy needed).

Usage: python tools/import_gallows_sound.py <rtsb_melee_designed_bone_break_neck_0144.wav>
The file holds several takes separated by silence; each becomes game/assets/audio/gallows_neck_N.wav
(mono, 16 bit, 44.1 kHz), and the game picks one at random per execution. The original stays untouched;
generated WAVs are ignored by Git and embedded into werwolf.exe at build time.
"""
import math
from pathlib import Path
import sys
import wave

TARGET_RATE = 44100
MAX_TAKES = 4


def read_mono(path: Path):
    with wave.open(str(path), "rb") as source:
        channels, width, rate = source.getnchannels(), source.getsampwidth(), source.getframerate()
        data = source.readframes(source.getnframes())
    if width not in (2, 3) or channels not in (1, 2):
        raise ValueError("Expected a 16- or 24-bit mono or stereo WAV")
    scale = float(1 << (8 * width - 1))
    step = width * channels
    mono = []
    for i in range(0, len(data) - step + 1, step):
        total = 0
        for c in range(channels):
            at = i + c * width
            total += int.from_bytes(data[at:at + width], "little", signed=True)
        mono.append(total / (channels * scale))
    return mono, rate


def find_takes(mono, rate):
    """Spans louder than 5 % of the peak; gaps under 0.3 s belong to the same take."""
    peak = max(abs(x) for x in mono)
    window = rate // 100
    loud = [max(abs(x) for x in mono[i:i + window]) > peak * 0.05 for i in range(0, len(mono), window)]
    spans, start, quiet = [], None, 0
    for index, on in enumerate(loud):
        if on:
            if start is None:
                start = index
            quiet = 0
        elif start is not None:
            quiet += 1
            if quiet * window > 0.3 * rate:
                spans.append((start * window, (index - quiet + 1) * window))
                start, quiet = None, 0
    if start is not None:
        spans.append((start * window, len(mono)))
    return spans


def trim(mono, rate, begin, end):
    """Keep a few milliseconds before the crack and the audible tail after it."""
    segment = mono[begin:end]
    peak = max(abs(x) for x in segment)
    first = next(i for i, x in enumerate(segment) if abs(x) > peak * 0.02)
    last = max(i for i, x in enumerate(segment) if abs(x) > peak * 0.005)
    lo = max(0, begin + first - int(0.003 * rate))
    hi = min(len(mono), begin + last + int(0.06 * rate))
    return mono[lo:hi]


def resample(samples, rate):
    """Windowed-sinc low-pass resampling to 44.1 kHz."""
    if rate == TARGET_RATE:
        return list(samples)
    ratio = rate / TARGET_RATE
    cutoff = 0.45 * min(1.0, 1.0 / ratio)  # cycles per input sample, a little below the new Nyquist limit
    half = 16
    out = []
    for n in range(int(len(samples) / ratio)):
        center = n * ratio
        base = int(math.floor(center))
        total = weight = 0.0
        for k in range(base - half + 1, base + half + 1):
            if k < 0 or k >= len(samples):
                continue
            x = k - center
            sinc = 2.0 * cutoff if x == 0 else math.sin(2.0 * math.pi * cutoff * x) / (math.pi * x)
            window = 0.5 + 0.5 * math.cos(math.pi * x / half)
            total += samples[k] * sinc * window
            weight += sinc * window
        out.append(total / weight if weight else 0.0)
    return out


def write(samples, destination: Path):
    peak = max(abs(x) for x in samples) or 1.0
    gain = 0.85 / peak
    fade_in, fade_out = min(88, len(samples)), min(1323, len(samples))
    frames = bytearray()
    for i, x in enumerate(samples):
        x *= gain
        if i < fade_in:
            x *= i / fade_in
        if i >= len(samples) - fade_out:
            x *= (len(samples) - i) / fade_out
        frames += int(round(max(-1.0, min(1.0, x)) * 32767)).to_bytes(2, "little", signed=True)
    with wave.open(str(destination), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(TARGET_RATE)
        output.writeframes(bytes(frames))
    print(f"{destination.name}: {len(samples) / TARGET_RATE:.3f} s, mono")


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    mono, rate = read_mono(Path(sys.argv[1]))
    takes = find_takes(mono, rate)[:MAX_TAKES]
    if not takes:
        raise ValueError("No audible take found")
    destination = Path(__file__).resolve().parents[1] / "game" / "assets" / "audio"
    destination.mkdir(parents=True, exist_ok=True)
    for old in destination.glob("gallows_neck_*.wav"):
        old.unlink()
    for index, (begin, end) in enumerate(takes, start=1):
        write(resample(trim(mono, rate, begin, end), rate), destination / f"gallows_neck_{index}.wav")


if __name__ == "__main__":
    main()
