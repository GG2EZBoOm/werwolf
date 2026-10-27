"""Import 20 recorded footsteps for embedding in the game (requires NumPy).

Usage: python tools/import_footsteps.py <zapsplat_pack_footsteps_on_paper_wav.zip>
Original recordings remain untouched; generated WAVs are ignored by Git.
"""
import io
from pathlib import Path
import sys
import wave
import zipfile

import numpy as np


def convert(data: bytes, destination: Path):
    with wave.open(io.BytesIO(data), "rb") as source:
        if source.getnchannels() != 1 or source.getsampwidth() != 3 or source.getframerate() != 96000:
            raise ValueError("Expected the original mono, 24-bit, 96 kHz recording")
        raw = np.frombuffer(source.readframes(source.getnframes()), dtype=np.uint8).reshape(-1, 3)
    values = raw[:, 0].astype(np.int32) | (raw[:, 1].astype(np.int32) << 8) | (raw[:, 2].astype(np.int32) << 16)
    pcm = ((values ^ 0x800000) - 0x800000).astype(np.float64) / 8388608.0
    # Trim dead air, retaining the attack and the natural paper rustle after the step.
    active = np.flatnonzero(np.abs(pcm) > max(0.001, float(np.abs(pcm).max()) * 0.012))
    if not len(active):
        raise ValueError("Silent footstep recording")
    pcm = pcm[max(0, active[0] - 768):min(len(pcm), active[-1] + 3840)]
    frames = round(len(pcm) * 44100 / 96000)
    spectrum = np.fft.rfft(pcm)[:frames // 2 + 1].copy()
    if frames % 2 == 0:
        spectrum[-1] = spectrum[-1].real * 2
    converted = np.fft.irfft(spectrum, n=frames) * (frames / len(pcm))
    # Gentle level matching without compression or a synthetic impact layered on top.
    rms = float(np.sqrt(np.mean(converted * converted)))
    gain = min(1.5, 0.05 / max(rms, 0.001), 0.72 / max(float(np.abs(converted).max()), 0.001))
    converted *= gain
    fade_in, fade_out = min(88, frames), min(882, frames)
    converted[:fade_in] *= np.linspace(0, 1, fade_in)
    converted[-fade_out:] *= np.linspace(1, 0, fade_out)
    with wave.open(str(destination), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(44100)
        output.writeframes(np.rint(converted * 32767).astype("<i2").tobytes())
    print(f"{destination.name}: {frames / 44100:.3f} s, mono, peak {np.abs(converted).max():.3f}")


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    destination = Path(__file__).resolve().parents[1] / "game" / "assets" / "audio"
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(sys.argv[1]) as archive:
        for index in range(1, 21):
            source_name = f"zapsplat_foley_paper_footstep_single_several_sheets_{index:03d}_{57603 + index}.wav"
            entries = [name for name in archive.namelist() if "__MACOSX" not in name and Path(name).name == source_name]
            if len(entries) != 1:
                raise ValueError(f"Missing or ambiguous recording: {source_name}")
            convert(archive.read(entries[0]), destination / f"footstep_{index:02d}.wav")


if __name__ == "__main__":
    main()
