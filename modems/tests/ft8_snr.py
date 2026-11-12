#!/usr/bin/env python3
"""Deterministic FT8 WAV/AWGN sweep using upstream gen_ft8/decode_ft8.

SNR is signal mean-square / noise mean-square across full sampled Nyquist
bandwidth (12 kHz sample rate, 6 kHz one-sided), NOT WSJT-X 2500 Hz SNR.
Reports exact payload recovery; no assumed RF sensitivity threshold.
"""
import argparse
import math
import random
import re
import subprocess
import sys
import tempfile
import wave
from array import array
from pathlib import Path

def generate(src, message, frequency):
    with tempfile.TemporaryDirectory() as temp:
        original = Path(temp) / "clean.wav"
        generated = subprocess.run(
            [str(src / "gen_ft8"), message, str(original), str(frequency)],
            capture_output=True, text=True)
        if generated.returncode:
            raise RuntimeError("gen_ft8 failed: " + generated.stderr + generated.stdout)
        with wave.open(str(original), "rb") as f:
            if f.getnchannels() != 1 or f.getsampwidth() != 2 or f.getframerate() != 12000:
                raise RuntimeError("expected 12 kHz, mono, 16-bit PCM from gen_ft8")
            samples = array("h")
            samples.frombytes(f.readframes(f.getnframes()))
        if sys.byteorder != "little":
            samples.byteswap()
        clean = [x / 32768.0 for x in samples]
        if not clean:
            raise RuntimeError("empty FT8 fixture")
        power = sum(x*x for x in clean) / len(clean)
        if not power:
            raise RuntimeError("silent FT8 fixture")
        yield temp, clean, power

def run():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ft8-lib", type=Path, required=True)
    parser.add_argument("--message", default="CQ K7IHZ DM43")
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--start", type=int, default=5)
    parser.add_argument("--stop", type=int, default=-30)
    parser.add_argument("--step", type=int, default=5)
    args = parser.parse_args()
    if args.trials < 1 or args.step < 1 or args.start < args.stop:
        parser.error("invalid sweep arguments")
    src = args.ft8_lib.resolve()
    if not (src / "gen_ft8").is_file() or not (src / "decode_ft8").is_file():
        parser.error("build ft8_lib gen_ft8 and decode_ft8 first")
    with tempfile.TemporaryDirectory() as tmp:
        original = Path(tmp) / "clean.wav"
        fixture = subprocess.run(
            [str(src / "gen_ft8"), args.message, str(original), "1000"],
            capture_output=True, text=True)
        if fixture.returncode:
            raise RuntimeError("gen_ft8 failed: " + fixture.stderr + fixture.stdout)
        with wave.open(str(original), "rb") as srcwav:
            if (srcwav.getnchannels(), srcwav.getsampwidth(), srcwav.getframerate()) != (1, 2, 12000):
                raise RuntimeError("unexpected source WAV format")
            samples = array("h")
            samples.frombytes(srcwav.readframes(srcwav.getnframes()))
        if sys.byteorder != "little":
            samples.byteswap()
        clean = [s / 32768.0 for s in samples]
        power = sum(v*v for v in clean) / len(clean)
        if power <= 0:
            raise RuntimeError("silent source")
        output = Path(tmp) / "noisy.wav"
        print(f"FT8 AWGN, {args.trials} trial(s)/SNR, {len(clean)/12000:.1f}s, message={args.message!r}")
        print("SNR(dB)  exact payload/trials")
        for db in range(args.start, args.stop-1, -args.step):
            sigma = math.sqrt(power / (10 ** (db / 10)))
            decoded = 0
            for trial in range(args.trials):
                rng = random.Random(0xC0DE1234 + trial * 100003)
                # Avoid hard clipping: scale signal+noise together.
                raw = [clean[i] + sigma * rng.gauss(0, 1) for i in range(len(clean))]
                peak = max(1.0, max(abs(v) for v in raw))
                pcm = array("h", (round(v / peak * 32760) for v in raw))
                if sys.byteorder != "little":
                    pcm.byteswap()
                with wave.open(str(output), "wb") as dst:
                    dst.setnchannels(1)
                    dst.setsampwidth(2)
                    dst.setframerate(12000)
                    dst.writeframes(pcm.tobytes())
                result = subprocess.run([str(src / "decode_ft8"), str(output)],
                                        capture_output=True, text=True)
                if result.returncode:
                    raise RuntimeError("decode_ft8 failed: " + result.stderr)
                if any(line.rstrip().endswith("~  " + args.message) for line in result.stdout.splitlines()):
                    decoded += 1
            print(f"{db:+7d}  {decoded:2d}/{args.trials}", flush=True)

if __name__ == "__main__":
    try:
        run()
    except (RuntimeError, OSError) as exc:
        sys.exit(str(exc))
