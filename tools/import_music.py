"""Import the user's licensed ZapSplat packs for embedding in the Windows client.

Usage: python tools/import_music.py <horror_stingers.zip> <ticking_tension.zip>
Requires NumPy. Originals remain untouched; generated WAVs are ignored by Git.
"""
import io
import math
from pathlib import Path
import sys
import wave
import zipfile

import numpy as np

TRACKS = (
    (1, "music_dark_harmonics_Ticking_Tension_02_Ticking_Tension_no1_Stripped_Back_Version.wav", "day_tension_1.wav"),
    (1, "music_dark_harmonics_Ticking_Tension_04_Ticking_Tension_no2_Stripped_Back_Version.wav", "day_tension_2.wav"),
    (0, "music_DeepTheme_Audio_stinger_Foreshadow_079.wav", "night_foreshadow.wav"),
    (0, "music_DeepTheme_Audio_Eyes_In_The_Dark_14.wav", "night_eyes_in_the_dark.wav"),
    (0, "music_DeepTheme_Audio_stinger_Creeping_Death_074.wav", "night_creeping_death.wav"),
)


def convert(data: bytes, destination: Path):
    with wave.open(io.BytesIO(data), "rb") as source:
        if source.getsampwidth() != 3 or source.getnchannels() != 2:
            raise ValueError("Expected the original 24-bit stereo WAV")
        source_rate = source.getframerate()
        raw = np.frombuffer(source.readframes(source.getnframes()), dtype=np.uint8).reshape(-1, 3)
    pcm = (raw[:, 0].astype(np.int32) | (raw[:, 1].astype(np.int32) << 8) | (raw[:, 2].astype(np.int32) << 16))
    pcm = ((pcm ^ 0x800000) - 0x800000).reshape(-1, 2).astype(np.float32) / 8388608.0
    # Band-limited resampling via FFT: remove frequencies above the new Nyquist limit.
    frame_count = round(len(pcm) * 44100 / source_rate)
    spectrum = np.fft.rfft(pcm, axis=0)
    spectrum = spectrum[:frame_count // 2 + 1].copy()
    if frame_count % 2 == 0:
        spectrum[-1] = spectrum[-1].real * 2
    converted = np.fft.irfft(spectrum, n=frame_count, axis=0) * (frame_count / len(pcm))
    # Preserve dynamics and stereo image, cap headroom, soften file boundaries.
    converted *= min(1.0, 0.85 / max(0.001, np.abs(converted).max()))
    fade = min(2205, frame_count // 2)
    converted[:fade] *= np.linspace(0, 1, fade)[:, None]
    converted[-fade:] *= np.linspace(1, 0, fade)[:, None]
    pcm16 = np.rint(np.clip(converted, -1, 1) * 32767).astype("<i2")
    with wave.open(str(destination), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(44100)
        output.writeframes(pcm16.tobytes())
    rms = math.sqrt(float(np.mean(converted * converted)))
    print(f"{destination.name}: {frame_count / 44100:.2f} s, stereo, peak {np.abs(converted).max():.3f}, RMS {rms:.3f}")


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    destination = Path(__file__).resolve().parents[1] / "game" / "assets" / "audio"
    destination.mkdir(parents=True, exist_ok=True)
    for pack, source_name, output_name in TRACKS:
        with zipfile.ZipFile(sys.argv[pack + 1]) as archive:
            entries = [n for n in archive.namelist() if "__MACOSX" not in n and Path(n).name == source_name]
            if len(entries) != 1:
                raise ValueError(f"Missing or ambiguous track: {source_name}")
            convert(archive.read(entries[0]), destination / output_name)


if __name__ == "__main__":
    main()
